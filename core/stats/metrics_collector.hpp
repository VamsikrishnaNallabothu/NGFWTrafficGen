#pragma once

#include "core/common/types.hpp"
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdint>
#include <map>

namespace trafficgen {

/**
 * Metrics collector for per-core and global statistics
 * Thread-safe statistics aggregation
 */
class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();
    
    // Initialize collector with number of cores
    bool initialize(uint32_t num_cores);
    
    // Update TX statistics for a core
    void update_tx_stats(uint32_t core_id, uint64_t packets, uint64_t bytes);
    
    // Update RX statistics for a core
    void update_rx_stats(uint32_t core_id, uint64_t packets, uint64_t bytes);
    
    // Update error count for a core
    void update_errors(uint32_t core_id, uint64_t count);
    
    // Get per-core statistics
    CoreStats get_core_stats(uint32_t core_id) const;
    
    // Get global aggregated statistics
    CoreStats get_global_stats() const;
    
    // Get all per-core statistics
    std::vector<CoreStats> get_all_core_stats() const;
    
    // Reset all statistics
    void reset_all();
    
    // Reset specific core statistics
    void reset_core(uint32_t core_id);
    
    // Calculate current PPS (packets per second)
    double calculate_current_pps(uint32_t core_id) const;
    
    // Calculate current BPS (bytes per second)
    double calculate_current_bps(uint32_t core_id) const;
    
    // Calculate global PPS
    double calculate_global_pps() const;
    
    // Calculate global BPS
    double calculate_global_bps() const;
    
    // Record one latency sample (in nanoseconds) for a core.
    void record_latency(uint32_t core_id, uint64_t latency_ns);

    // Get average latency in microseconds.
    double get_average_latency_us() const;

    // Latency histogram bucket description.
    struct LatencyBucketInfo {
        uint32_t min_us;
        uint32_t max_us; // UINT32_MAX means open-ended
        uint64_t count;
    };

    // Get global latency histogram.
    std::vector<LatencyBucketInfo> get_latency_histogram() const;

    // Get uptime in seconds
    uint64_t get_uptime_seconds() const;
    
    // Set running state
    void set_running(bool running);
    
    // Check if running
    bool is_running() const;

private:
    std::vector<CoreStats> per_core_stats_;
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> running_{false};
    uint32_t num_cores_;

    // Latency metrics (global across cores)
    std::vector<uint64_t> latency_buckets_; // counts per bucket
    double total_latency_us_{0.0};
    uint64_t latency_samples_{0};
    
    // Last snapshot for rate calculations
    mutable std::map<uint32_t, std::pair<uint64_t, std::chrono::steady_clock::time_point>> last_snapshot_;
    mutable std::mutex snapshot_mutex_;
    
    // Update snapshot for rate calculation
    void update_snapshot(uint32_t core_id) const;
};

} // namespace trafficgen

