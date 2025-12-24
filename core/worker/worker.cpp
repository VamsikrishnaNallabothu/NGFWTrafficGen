#include "core/worker/worker.hpp"
#include "core/stateful/tcp_state.hpp"
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_mbuf_dyn.h>
#include <unistd.h>
#include <pthread.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <chrono>

namespace trafficgen {

#define TIMESTAMP_DYNFIELD(mbuf) \
    RTE_MBUF_DYNFIELD((mbuf), config_.timestamp_dynfield_offset, uint64_t*)

Worker::Worker(const Config& config) : config_(config) {
}

Worker::~Worker() {
    stop();
    join();
}

bool Worker::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return true;
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

CoreStatsSnapshot Worker::get_stats() const {
    if (config_.metrics_collector) {
        return config_.metrics_collector->get_core_stats(config_.core_id);
    }
    return CoreStatsSnapshot();
}

uint32_t Worker::get_core_id() const {
    return config_.core_id;
}

void Worker::worker_loop() {
    if (rte_thread_register() < 0) {
        std::cerr << "Error: Failed to register worker thread for core " << config_.core_id << std::endl;
        running_.store(false, std::memory_order_relaxed);
        return;
    }
    
    std::vector<rte_mbuf*> tx_mbufs(config_.tx_burst_size, nullptr);
    std::vector<rte_mbuf*> rx_mbufs(config_.rx_burst_size, nullptr);
    
    rte_mempool* mempool = nullptr;
    if (config_.mempool_manager) {
        unsigned int socket_id = rte_lcore_to_socket_id(config_.core_id);
        mempool = config_.mempool_manager->get_mempool(socket_id);
    }
    
    if (mempool == nullptr) {
        running_.store(false, std::memory_order_relaxed);
        return;
    }
    
    PacketMutator mutator;
    mutator.initialize(static_cast<uint64_t>(config_.core_id));
    
    while (!should_stop_.load(std::memory_order_relaxed)) {
        if (config_.flow_scheduler) {
            process_flows();
        }
        
        if (config_.enable_rx) {
            uint16_t rx_count = receive_burst(rx_mbufs.data(), config_.rx_burst_size);
            if (rx_count > 0) {
                process_received_packets(rx_mbufs.data(), rx_count);
            }
        }
        
        rte_pause();
    }
    
    running_.store(false, std::memory_order_relaxed);
}

uint16_t Worker::transmit_burst(rte_mbuf** mbufs, uint16_t count) {
    if (mbufs == nullptr || count == 0) {
        return 0;
    }
    
    uint16_t sent = rte_eth_tx_burst(config_.port_id, config_.queue_id, mbufs, count);
    
    if (sent > 0 && config_.metrics_collector) {
        uint64_t total_bytes = 0;
        for (uint16_t i = 0; i < sent; ++i) {
            total_bytes += rte_pktmbuf_pkt_len(mbufs[i]);
        }
        
        // --- DEBUGGING ---
        std::cout << "[Worker " << config_.core_id << "] transmit_burst: calling update_tx_stats with " << sent << " packets." << std::endl;
        config_.metrics_collector->update_tx_stats(
            config_.core_id, sent, total_bytes);
    }
    
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
    
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    uint64_t total_bytes = 0;

    for (uint16_t i = 0; i < count; ++i) {
        rte_mbuf* mbuf = mbufs[i];
        if (mbuf == nullptr) {
            continue;
        }

        total_bytes += rte_pktmbuf_pkt_len(mbuf);

        if (config_.metrics_collector && config_.timestamp_dynfield_offset != -1) {
            uint64_t* ts_ptr = TIMESTAMP_DYNFIELD(mbuf);
            if (ts_ptr != nullptr) {
                uint64_t sent_ns = *ts_ptr;
                if (sent_ns != 0 && now_ns > sent_ns) {
                    uint64_t delta_ns = now_ns - sent_ns;
                    config_.metrics_collector->record_latency(
                        config_.core_id, delta_ns);
                }
            }
        }

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

        rte_pktmbuf_free(mbuf);
        mbufs[i] = nullptr;
    }

    if (config_.metrics_collector && total_bytes > 0) {
        config_.metrics_collector->update_rx_stats(
            config_.core_id, count, total_bytes);
    }
}

void Worker::process_flows() {
    if (!config_.flow_scheduler || !config_.mempool_manager) {
        return;
    }
    
    std::vector<uint32_t> ready_flows = config_.flow_scheduler->get_ready_flows(
        config_.tx_burst_size);

    if (ready_flows.empty()) {
        return;
    }
    
    unsigned int socket_id = rte_lcore_to_socket_id(config_.core_id);
    rte_mempool* mempool = config_.mempool_manager->get_mempool(socket_id);
    if (mempool == nullptr) {
        return;
    }
    
    std::vector<rte_mbuf*> mbufs(config_.tx_burst_size, nullptr);
    uint16_t mbuf_count = 0;
    
    PacketMutator mutator;
    mutator.initialize(static_cast<uint64_t>(config_.core_id));
    
    for (uint32_t flow_id : ready_flows) {
        if (mbuf_count >= config_.tx_burst_size) {
            break;
        }
        
        std::shared_ptr<FlowConfigInternal> flow = config_.flow_scheduler->get_flow(flow_id);
        if (!flow || !flow->active.load(std::memory_order_relaxed)) {
            continue;
        }
        
        if (config_.mempool_manager->allocate_burst(mempool, &mbufs[mbuf_count], 1) == 0) {
            continue;
        }
        
        rte_mbuf* mbuf = mbufs[mbuf_count];
        if (mbuf == nullptr) {
            continue;
        }

        uint32_t packet_size = flow->packet_size;
        if (packet_size == 0 && config_.imix_engine) {
            packet_size = config_.imix_engine->select_next_packet_size();
        }
        if (packet_size == 0) {
            packet_size = 64;
        }

        PacketTemplate final_template = flow->template_packet;
        final_template.total_size = packet_size;

        if (mutator.clone_and_mutate(mbuf, final_template, 0, true)) {
            if (!flow->stateless && flow->protocol == "tcp" &&
                flow->tcp_state && flow->tcp_state_machine) {
                rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
                rte_ipv4_hdr* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(
                    reinterpret_cast<uint8_t*>(eth_hdr) + sizeof(rte_ether_hdr));
                rte_tcp_hdr* tcp_hdr = reinterpret_cast<rte_tcp_hdr*>(
                    reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr));

                TCPConnectionState& conn = *flow->tcp_state;

                tcp_hdr->tcp_flags = RTE_TCP_ACK_FLAG;
                tcp_hdr->sent_seq = rte_cpu_to_be_32(conn.send_seq);
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

            if (config_.timestamp_dynfield_offset != -1) {
                uint64_t* ts_ptr = TIMESTAMP_DYNFIELD(mbuf);
                if (ts_ptr != nullptr) {
                    *ts_ptr = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
                }
            }

            mbuf_count++;

            uint64_t pkt_len = rte_pktmbuf_pkt_len(mbuf);
            flow->packets_sent.fetch_add(1, std::memory_order_relaxed);
            flow->bytes_sent.fetch_add(pkt_len, std::memory_order_relaxed);
        } else {
            rte_pktmbuf_free(mbuf);
            mbufs[mbuf_count] = nullptr;
        }
    }
    
    if (mbuf_count > 0) {
        transmit_burst(mbufs.data(), mbuf_count);
    }
}

rte_mbuf* Worker::build_packet_from_template(const PacketTemplate& template_in) {
    if (!config_.mempool_manager) {
        return nullptr;
    }
    
    unsigned int socket_id = rte_lcore_to_socket_id(config_.core_id);
    rte_mempool* mempool = config_.mempool_manager->get_mempool(socket_id);
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

} // namespace trafficgen
