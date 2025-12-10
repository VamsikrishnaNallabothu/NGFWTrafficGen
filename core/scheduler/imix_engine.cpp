#include "core/scheduler/imix_engine.hpp"
#include <algorithm>
#include <numeric>
#include <ctime>

namespace trafficgen {

IMIXEngine::IMIXEngine() : target_pps_(0), configured_(false), rng_(std::time(nullptr)) {
}

IMIXEngine::~IMIXEngine() {
}

bool IMIXEngine::configure(const std::vector<IMIXEntry>& entries, uint64_t target_pps) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!validate_entries(entries)) {
        return false;
    }
    
    entries_ = entries;
    target_pps_ = target_pps;
    
    build_distribution();
    configured_ = true;
    
    return true;
}

uint32_t IMIXEngine::select_next_packet_size() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!configured_ || entries_.empty()) {
        return 64;  // Default minimum packet size
    }
    
    size_t index = size_distribution_(rng_);
    return entries_[index].packet_size;
}

std::string IMIXEngine::select_next_protocol() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!configured_ || entries_.empty()) {
        return "udp";  // Default protocol
    }
    
    size_t index = size_distribution_(rng_);
    return entries_[index].protocol;
}

uint64_t IMIXEngine::get_target_pps() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return target_pps_;
}

bool IMIXEngine::is_configured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configured_;
}

std::vector<IMIXEntry> IMIXEngine::get_entries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_;
}

void IMIXEngine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    target_pps_ = 0;
    configured_ = false;
}

void IMIXEngine::build_distribution() {
    if (entries_.empty()) {
        return;
    }
    
    std::vector<double> weights;
    weights.reserve(entries_.size());
    
    for (const auto& entry : entries_) {
        weights.push_back(entry.percentage);
    }
    
    size_distribution_ = std::discrete_distribution<size_t>(weights.begin(), weights.end());
}

bool IMIXEngine::validate_entries(const std::vector<IMIXEntry>& entries) {
    if (entries.empty()) {
        return false;
    }
    
    double total_percentage = 0.0;
    for (const auto& entry : entries) {
        if (entry.packet_size < 64 || entry.packet_size > 9000) {
            return false;  // Invalid packet size
        }
        if (entry.percentage < 0.0 || entry.percentage > 100.0) {
            return false;  // Invalid percentage
        }
        if (entry.protocol != "tcp" && entry.protocol != "udp" && entry.protocol != "icmp") {
            return false;  // Unsupported protocol
        }
        total_percentage += entry.percentage;
    }
    
    // Allow some tolerance for floating point errors
    if (total_percentage < 99.0 || total_percentage > 101.0) {
        return false;  // Percentages don't sum to ~100%
    }
    
    return true;
}

} // namespace trafficgen

