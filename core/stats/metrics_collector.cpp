#include "core/stats/metrics_collector.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace trafficgen {

// Latency bucket boundaries in microseconds.
// Buckets: [0,10), [10,50), [50,100), [100,250), [250,500),
//           [500,1000), [1000,5000), [5000,10000), [10000,50000), [50000,+inf)
static const uint32_t kLatencyBucketBoundsUs[] = {
    10, 50, 100, 250, 500, 1000, 5000, 10000, 50000
};
static constexpr size_t kNumLatencyBuckets =
    sizeof(kLatencyBucketBoundsUs) / sizeof(kLatencyBucketBoundsUs[0]) + 1;

MetricsCollector::MetricsCollector() : num_cores_(0) {
    start_time_ = std::chrono::steady_clock::now();
    latency_buckets_.assign(kNumLatencyBuckets, 0);
}

MetricsCollector::~MetricsCollector() {
}

bool MetricsCollector::initialize(uint32_t num_cores) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (num_cores == 0) {
        return false;
    }
    
    num_cores_ = num_cores;
    per_core_stats_.resize(num_cores);
    
    // Initialize all cores
    for (uint32_t i = 0; i < num_cores; ++i) {
        per_core_stats_[i].reset();
    }
    
    start_time_ = std::chrono::steady_clock::now();
    // Reset latency metrics
    latency_buckets_.assign(kNumLatencyBuckets, 0);
    total_latency_us_ = 0.0;
    latency_samples_ = 0;
    return true;
}

void MetricsCollector::update_tx_stats(uint32_t core_id, uint64_t packets, uint64_t bytes) {
    if (core_id >= per_core_stats_.size()) {
        return;
    }
    
    per_core_stats_[core_id].tx_packets.fetch_add(packets, std::memory_order_relaxed);
    per_core_stats_[core_id].tx_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsCollector::update_rx_stats(uint32_t core_id, uint64_t packets, uint64_t bytes) {
    if (core_id >= per_core_stats_.size()) {
        return;
    }
    
    per_core_stats_[core_id].rx_packets.fetch_add(packets, std::memory_order_relaxed);
    per_core_stats_[core_id].rx_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void MetricsCollector::update_errors(uint32_t core_id, uint64_t count) {
    if (core_id >= per_core_stats_.size()) {
        return;
    }
    
    per_core_stats_[core_id].errors.fetch_add(count, std::memory_order_relaxed);
}

CoreStats MetricsCollector::get_core_stats(uint32_t core_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (core_id >= per_core_stats_.size()) {
        return CoreStats();
    }
    
    return per_core_stats_[core_id];
}

CoreStats MetricsCollector::get_global_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    CoreStats aggregated;
    aggregated.reset();
    
    for (const auto& core_stats : per_core_stats_) {
        aggregated.tx_packets.fetch_add(
            core_stats.tx_packets.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        aggregated.rx_packets.fetch_add(
            core_stats.rx_packets.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        aggregated.tx_bytes.fetch_add(
            core_stats.tx_bytes.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        aggregated.rx_bytes.fetch_add(
            core_stats.rx_bytes.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        aggregated.errors.fetch_add(
            core_stats.errors.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
    }
    
    return aggregated;
}

std::vector<CoreStats> MetricsCollector::get_all_core_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return per_core_stats_;
}

void MetricsCollector::reset_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& core_stats : per_core_stats_) {
        core_stats.reset();
    }
    
    {
        std::lock_guard<std::mutex> snap_lock(snapshot_mutex_);
        last_snapshot_.clear();
    }
    
    start_time_ = std::chrono::steady_clock::now();
    latency_buckets_.assign(kNumLatencyBuckets, 0);
    total_latency_us_ = 0.0;
    latency_samples_ = 0;
}

void MetricsCollector::reset_core(uint32_t core_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (core_id < per_core_stats_.size()) {
        per_core_stats_[core_id].reset();
        
        std::lock_guard<std::mutex> snap_lock(snapshot_mutex_);
        last_snapshot_.erase(core_id);
    }
}

double MetricsCollector::calculate_current_pps(uint32_t core_id) const {
    if (core_id >= per_core_stats_.size()) {
        return 0.0;
    }
    
    update_snapshot(core_id);
    
    std::lock_guard<std::mutex> snap_lock(snapshot_mutex_);
    auto it = last_snapshot_.find(core_id);
    if (it == last_snapshot_.end()) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - it->second.timestamp).count();
    
    if (elapsed <= 0) {
        return 0.0;
    }
    
    uint64_t current_packets = per_core_stats_[core_id].tx_packets.load(std::memory_order_relaxed);
    uint64_t delta_packets = current_packets - it->second.packets;
    
    double elapsed_seconds = elapsed / 1000.0;
    return delta_packets / elapsed_seconds;
}

double MetricsCollector::calculate_current_bps(uint32_t core_id) const {
    if (core_id >= per_core_stats_.size()) {
        return 0.0;
    }
    
    update_snapshot(core_id);
    
    std::lock_guard<std::mutex> snap_lock(snapshot_mutex_);
    auto it = last_snapshot_.find(core_id);
    if (it == last_snapshot_.end()) {
        return 0.0;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - it->second.timestamp).count();
    
    if (elapsed <= 0) {
        return 0.0;
    }
    
    uint64_t current_bytes = per_core_stats_[core_id].tx_bytes.load(std::memory_order_relaxed);
    uint64_t delta_bytes = current_bytes - it->second.bytes;
    
    double elapsed_seconds = elapsed / 1000.0;
    return (delta_bytes * 8.0) / elapsed_seconds;  // Bits per second
}

double MetricsCollector::calculate_global_pps() const {
    double total_pps = 0.0;
    for (uint32_t i = 0; i < per_core_stats_.size(); ++i) {
        total_pps += calculate_current_pps(i);
    }
    return total_pps;
}

double MetricsCollector::calculate_global_bps() const {
    double total_bps = 0.0;
    for (uint32_t i = 0; i < per_core_stats_.size(); ++i) {
        total_bps += calculate_current_bps(i);
    }
    return total_bps;
}

uint64_t MetricsCollector::get_uptime_seconds() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_).count();
    return static_cast<uint64_t>(elapsed);
}

void MetricsCollector::set_running(bool running) {
    running_.store(running, std::memory_order_relaxed);
}

bool MetricsCollector::is_running() const {
    return running_.load(std::memory_order_relaxed);
}

void MetricsCollector::record_latency(uint32_t /*core_id*/, uint64_t latency_ns) {
    double latency_us = static_cast<double>(latency_ns) / 1000.0;

    std::lock_guard<std::mutex> lock(mutex_);

    // Update average
    total_latency_us_ += latency_us;
    latency_samples_ += 1;

    // Find bucket
    size_t idx = 0;
    while (idx + 1 < kNumLatencyBuckets &&
           latency_us >= static_cast<double>(kLatencyBucketBoundsUs[idx])) {
        ++idx;
    }
    if (idx >= latency_buckets_.size()) {
        latency_buckets_.resize(idx + 1, 0);
    }
    latency_buckets_[idx] += 1;
}

double MetricsCollector::get_average_latency_us() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latency_samples_ == 0) {
        return 0.0;
    }
    return total_latency_us_ / static_cast<double>(latency_samples_);
}

std::vector<MetricsCollector::LatencyBucketInfo>
MetricsCollector::get_latency_histogram() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<LatencyBucketInfo> result;
    result.reserve(kNumLatencyBuckets);

    uint32_t prev_bound = 0;
    for (size_t i = 0; i < kNumLatencyBuckets; ++i) {
        LatencyBucketInfo info{};
        info.min_us = prev_bound;
        if (i + 1 < kNumLatencyBuckets) {
            info.max_us = kLatencyBucketBoundsUs[i];
            prev_bound = kLatencyBucketBoundsUs[i];
        } else {
            info.max_us = std::numeric_limits<uint32_t>::max();
        }
        uint64_t count = (i < latency_buckets_.size()) ? latency_buckets_[i] : 0;
        info.count = count;
        result.push_back(info);
    }

    return result;
}

void MetricsCollector::update_snapshot(uint32_t core_id) const {
    if (core_id >= per_core_stats_.size()) {
        return;
    }
    
    std::lock_guard<std::mutex> snap_lock(snapshot_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto it = last_snapshot_.find(core_id);
    
    uint64_t current_packets = per_core_stats_[core_id].tx_packets.load(std::memory_order_relaxed);
    uint64_t current_bytes = per_core_stats_[core_id].tx_bytes.load(std::memory_order_relaxed);

    if (it == last_snapshot_.end()) {
        // First snapshot
        last_snapshot_[core_id] = {current_packets, current_bytes, now};
    } else {
        // Update snapshot every second
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.timestamp).count();
        
        if (elapsed >= 1) {
            last_snapshot_[core_id] = {current_packets, current_bytes, now};
        }
    }
}

} // namespace trafficgen

