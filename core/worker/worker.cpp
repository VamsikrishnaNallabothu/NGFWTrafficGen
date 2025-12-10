#include "core/worker/worker.hpp"
#include "core/stateful/tcp_state.hpp"
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <unistd.h>
#include <pthread.h>
#include <algorithm>
#include <cstring>
#include <chrono>

namespace trafficgen {

Worker::Worker(const Config& config) : config_(config) {
}

Worker::~Worker() {
    stop();
    join();
}

bool Worker::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return true;  // Already running
    }
    
    should_stop_.store(false, std::memory_order_relaxed);
    running_.store(true, std::memory_order_relaxed);
    
    worker_thread_ = std::make_unique<std::thread>(&Worker::worker_loop, this);
    
    return true;
}

void Worker::stop() {
    should_stop_.store(true, std::memory_order_relaxed);
}

void Worker::join() {
    if (worker_thread_ && worker_thread_->joinable()) {
        worker_thread_->join();
    }
    running_.store(false, std::memory_order_relaxed);
}

bool Worker::is_running() const {
    return running_.load(std::memory_order_relaxed);
}

CoreStats Worker::get_stats() const {
    if (config_.metrics_collector) {
        return config_.metrics_collector->get_core_stats(config_.core_id);
    }
    return CoreStats();
}

uint32_t Worker::get_core_id() const {
    return config_.core_id;
}

void Worker::worker_loop() {
    // Set CPU affinity to specified core
    set_cpu_affinity();
    
    // Pre-allocate mbuf array for bursts
    std::vector<rte_mbuf*> tx_mbufs(config_.tx_burst_size, nullptr);
    std::vector<rte_mbuf*> rx_mbufs(config_.rx_burst_size, nullptr);
    
    // Get local mempool
    rte_mempool* mempool = nullptr;
    if (config_.mempool_manager) {
        mempool = config_.mempool_manager->get_local_mempool();
    }
    
    if (mempool == nullptr) {
        // Error: no mempool available
        running_.store(false, std::memory_order_relaxed);
        return;
    }
    
    // Initialize packet mutator
    PacketMutator mutator;
    mutator.initialize(static_cast<uint64_t>(config_.core_id));
    
    while (!should_stop_.load(std::memory_order_relaxed)) {
        // Process flows and build packets for transmission
        if (config_.flow_scheduler) {
            process_flows();
        }
        
        // Handle RX if enabled
        if (config_.enable_rx) {
            uint16_t rx_count = receive_burst(rx_mbufs.data(), config_.rx_burst_size);
            if (rx_count > 0) {
                process_received_packets(rx_mbufs.data(), rx_count);
            }
        }
        
        // Small delay to prevent busy-waiting
        // In production, this might be optimized or removed based on performance requirements
        rte_pause();
    }
    
    // Drain any remaining TX packets
    // Final cleanup handled by stop()
    running_.store(false, std::memory_order_relaxed);
}

uint16_t Worker::transmit_burst(rte_mbuf** mbufs, uint16_t count) {
    if (mbufs == nullptr || count == 0) {
        return 0;
    }
    
    uint16_t sent = rte_eth_tx_burst(config_.port_id, config_.queue_id, mbufs, count);
    
    // Update statistics
    if (sent > 0 && config_.metrics_collector) {
        uint64_t total_bytes = 0;
        for (uint16_t i = 0; i < sent; ++i) {
            total_bytes += rte_pktmbuf_pkt_len(mbufs[i]);
        }
        
        config_.metrics_collector->update_tx_stats(
            config_.core_id, sent, total_bytes);
    }
    
    // Free mbufs that weren't sent
    if (sent < count && config_.mempool_manager) {
        for (uint16_t i = sent; i < count; ++i) {
            if (mbufs[i] != nullptr) {
                rte_pktmbuf_free(mbufs[i]);
                mbufs[i] = nullptr;
            }
        }
    }
    
    return sent;
}

uint16_t Worker::receive_burst(rte_mbuf** mbufs, uint16_t max_count) {
    if (mbufs == nullptr || max_count == 0) {
        return 0;
    }
    
    uint16_t received = rte_eth_rx_burst(config_.port_id, config_.queue_id, mbufs, max_count);
    
    return received;
}

void Worker::process_received_packets(rte_mbuf** mbufs, uint16_t count) {
    if (mbufs == nullptr || count == 0) {
        return;
    }
    
    // Timestamp for latency calculation
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    uint64_t total_bytes = 0;

    for (uint16_t i = 0; i < count; ++i) {
        rte_mbuf* mbuf = mbufs[i];
        if (mbuf == nullptr) {
            continue;
        }

        total_bytes += rte_pktmbuf_pkt_len(mbuf);

        // Latency measurement (if udata carries TX timestamp)
        if (config_.metrics_collector && mbuf->udata != nullptr) {
            uint64_t sent_ns = reinterpret_cast<uint64_t>(mbuf->udata);
            if (sent_ns != 0 && now_ns > sent_ns) {
                uint64_t delta_ns = now_ns - sent_ns;
                config_.metrics_collector->record_latency(
                    config_.core_id, delta_ns);
            }
        }

        // TCP RX handling for stateful flows
        if (config_.flow_scheduler) {
            rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
            (void)eth_hdr;
            rte_ipv4_hdr* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(
                reinterpret_cast<uint8_t*>(eth_hdr) + sizeof(rte_ether_hdr));

            if ((ip_hdr->version_ihl >> 4) == 4 && ip_hdr->next_proto_id == IPPROTO_TCP) {
                rte_tcp_hdr* tcp_hdr = reinterpret_cast<rte_tcp_hdr*>(
                    reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr));

                uint16_t l4_len = rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(rte_ipv4_hdr);
                uint16_t tcp_hdr_len = (tcp_hdr->data_off >> 4) * 4;
                uint32_t payload_len = 0;
                if (l4_len > tcp_hdr_len) {
                    payload_len = static_cast<uint32_t>(l4_len - tcp_hdr_len);
                }

                FlowKey key;
                key.src_ip = ip_hdr->src_addr;
                key.dst_ip = ip_hdr->dst_addr;
                key.src_port = rte_be_to_cpu_16(tcp_hdr->src_port);
                key.dst_port = rte_be_to_cpu_16(tcp_hdr->dst_port);
                key.protocol = IPPROTO_TCP;

                config_.flow_scheduler->handle_tcp_rx(key, tcp_hdr, payload_len);
            }
        }

        // Free received packet
        rte_pktmbuf_free(mbuf);
        mbufs[i] = nullptr;
    }

    // Update RX statistics (aggregate)
    if (config_.metrics_collector && total_bytes > 0) {
        config_.metrics_collector->update_rx_stats(
            config_.core_id, count, total_bytes);
    }
}

void Worker::process_flows() {
    if (!config_.flow_scheduler || !config_.mempool_manager) {
        return;
    }
    
    // Get ready flows
    std::vector<uint32_t> ready_flows = config_.flow_scheduler->get_ready_flows(
        config_.tx_burst_size);
    
    if (ready_flows.empty()) {
        return;
    }
    
    // Get mempool
    rte_mempool* mempool = config_.mempool_manager->get_local_mempool();
    if (mempool == nullptr) {
        return;
    }
    
    // Pre-allocate mbufs for burst
    std::vector<rte_mbuf*> mbufs(config_.tx_burst_size, nullptr);
    uint16_t mbuf_count = 0;
    
    PacketMutator mutator;
    mutator.initialize(static_cast<uint64_t>(config_.core_id));
    
    // Build packets for each ready flow
    for (uint32_t flow_id : ready_flows) {
        if (mbuf_count >= config_.tx_burst_size) {
            break;
        }
        
        std::shared_ptr<FlowConfigInternal> flow = config_.flow_scheduler->get_flow(flow_id);
        if (!flow || !flow->active.load(std::memory_order_relaxed)) {
            continue;
        }
        
        // Allocate mbuf
        if (config_.mempool_manager->allocate_burst(mempool, &mbufs[mbuf_count], 1) == 0) {
            continue;  // Failed to allocate
        }
        
        rte_mbuf* mbuf = mbufs[mbuf_count];
        if (mbuf == nullptr) {
            continue;
        }
        
        // Clone template and mutate
        if (mutator.clone_and_mutate(mbuf, flow->template_packet, 0, true)) {
            // For stateful TCP flows, set sequence/ACK numbers and flags
            if (!flow->stateless && flow->protocol == "tcp" &&
                flow->tcp_state && flow->tcp_state_machine) {
                rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
                rte_ipv4_hdr* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(
                    reinterpret_cast<uint8_t*>(eth_hdr) + sizeof(rte_ether_hdr));
                rte_tcp_hdr* tcp_hdr = reinterpret_cast<rte_tcp_hdr*>(
                    reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr));

                TCPConnectionState& conn = *flow->tcp_state;

                // For now we assume ESTABLISHED state and send pure ACK packets.
                tcp_hdr->tcp_flags = RTE_TCP_ACK_FLAG;
                tcp_hdr->sent_seq = rte_cpu_to_be_32(conn.send_seq);
                // ACK should reflect what we are acknowledging to the peer
                tcp_hdr->recv_ack = rte_cpu_to_be_32(conn.send_ack);

                uint16_t l4_len = rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(rte_ipv4_hdr);
                uint16_t tcp_hdr_len = (tcp_hdr->data_off >> 4) * 4;
                uint32_t payload_len = 0;
                if (l4_len > tcp_hdr_len) {
                    payload_len = static_cast<uint32_t>(l4_len - tcp_hdr_len);
                }
                conn.send_seq += payload_len;

                mutator.recalculate_tcp_checksum(ip_hdr, tcp_hdr);
            }

            // Stamp TX timestamp for latency measurement (store in udata pointer)
            uint64_t tx_timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count();
            mbuf->udata = reinterpret_cast<void*>(tx_timestamp);

            mbuf_count++;

            // Update flow statistics
            uint64_t pkt_len = rte_pktmbuf_pkt_len(mbuf);
            flow->packets_sent.fetch_add(1, std::memory_order_relaxed);
            flow->bytes_sent.fetch_add(pkt_len, std::memory_order_relaxed);
        } else {
            // Failed to build packet, free mbuf
            rte_pktmbuf_free(mbuf);
            mbufs[mbuf_count] = nullptr;
        }
    }
    
    // Transmit burst
    if (mbuf_count > 0) {
        transmit_burst(mbufs.data(), mbuf_count);
    }
}

rte_mbuf* Worker::build_packet_from_template(const PacketTemplate& template_in) {
    if (!config_.mempool_manager) {
        return nullptr;
    }
    
    rte_mempool* mempool = config_.mempool_manager->get_local_mempool();
    if (mempool == nullptr) {
        return nullptr;
    }
    
    rte_mbuf* mbuf = rte_pktmbuf_alloc(mempool);
    if (mbuf == nullptr) {
        return nullptr;
    }
    
    PacketMutator mutator;
    mutator.initialize(static_cast<uint64_t>(config_.core_id));
    
    if (!mutator.clone_and_mutate(mbuf, template_in, 0, true)) {
        rte_pktmbuf_free(mbuf);
        return nullptr;
    }
    
    return mbuf;
}

void Worker::set_cpu_affinity() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(config_.core_id, &cpuset);
    
    pthread_t current_thread = pthread_self();
    int rc = pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
    
    if (rc != 0) {
        // Failed to set affinity, but continue anyway
    }
}

} // namespace trafficgen

