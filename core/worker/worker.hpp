#pragma once

#include "core/common/types.hpp"
#include "core/mempools/mempool_manager.hpp"
#include "core/packet/packet_mutator.hpp"
#include "core/scheduler/flow_scheduler.hpp"
#include "core/scheduler/imix_engine.hpp"
#include "core/stats/metrics_collector.hpp"
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <atomic>
#include <thread>
#include <cstdint>
#include <vector>
#include <memory>

namespace trafficgen {

/**
 * Per-core worker for TX/RX packet processing
 * Handles packet transmission bursts and optional RX processing
 */
class Worker {
public:
    struct Config {
        uint32_t core_id;
        uint16_t port_id;
        uint16_t queue_id;
        uint16_t tx_burst_size;
        uint16_t rx_burst_size;
        bool enable_rx;
        std::shared_ptr<MempoolManager> mempool_manager;
        std::shared_ptr<FlowScheduler> flow_scheduler;
        std::shared_ptr<IMIXEngine> imix_engine;
        std::shared_ptr<MetricsCollector> metrics_collector;
        int timestamp_dynfield_offset;
    };

    Worker(const Config& config);
    ~Worker();

    // Start worker thread
    bool start();

    // Stop worker thread
    void stop();

    // Wait for worker to finish
    void join();

    // Check if worker is running
    bool is_running() const;

    // Get worker statistics snapshot
    CoreStatsSnapshot get_stats() const;

    // Get core ID
    uint32_t get_core_id() const;

private:
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    std::unique_ptr<std::thread> worker_thread_;

    // Worker main loop
    void worker_loop();

    // Transmit burst of packets
    uint16_t transmit_burst(rte_mbuf** mbufs, uint16_t count);

    // Receive burst of packets (optional)
    uint16_t receive_burst(rte_mbuf** mbufs, uint16_t max_count);

    // Process received packets
    void process_received_packets(rte_mbuf** mbufs, uint16_t count);

    // Build and transmit packets for flows
    void process_flows();

    // Build packet from template
    rte_mbuf* build_packet_from_template(const PacketTemplate& template_in);
};

} // namespace trafficgen
