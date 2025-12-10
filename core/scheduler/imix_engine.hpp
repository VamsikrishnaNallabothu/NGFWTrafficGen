#pragma once

#include "core/common/types.hpp"
#include "core/scheduler/token_bucket.hpp"
#include <vector>
#include <memory>
#include <random>
#include <mutex>
#include <cstdint>

namespace trafficgen {

/**
 * IMIX (Internet Mix) engine for generating traffic patterns
 * Supports weighted packet size distribution based on IMIX profiles
 */
class IMIXEngine {
public:
    IMIXEngine();
    ~IMIXEngine();
    
    // Configure IMIX profile
    bool configure(const std::vector<IMIXEntryInternal>& entries, uint64_t target_pps);
    
    // Select next packet size based on IMIX distribution
    uint32_t select_next_packet_size();
    
    // Select next protocol based on IMIX distribution
    std::string select_next_protocol();
    
    // Get current target PPS
    uint64_t get_target_pps() const;
    
    // Check if configured
    bool is_configured() const;
    
    // Get all IMIX entries
    std::vector<IMIXEntryInternal> get_entries() const;
    
    // Reset engine
    void reset();

private:
    std::vector<IMIXEntryInternal> entries_;
    uint64_t target_pps_;
    bool configured_;
    mutable std::mutex mutex_;
    
    std::mt19937_64 rng_;
    std::discrete_distribution<size_t> size_distribution_;
    
    // Build discrete distribution from percentages
    void build_distribution();
    
    // Validate IMIX entries
    bool validate_entries(const std::vector<IMIXEntryInternal>& entries);
};

} // namespace trafficgen

