#pragma once

#include <cstdint>
#include <atomic>
#include <chrono>
#include <mutex>

namespace trafficgen {

/**
 * Token bucket rate limiter for traffic shaping
 * Thread-safe implementation for per-core rate control
 */
class TokenBucket {
public:
    TokenBucket(uint64_t rate_pps, uint64_t burst_size);
    ~TokenBucket();
    
    // Try to consume tokens (non-blocking)
    // Returns number of tokens consumed (0 to requested)
    uint32_t try_consume(uint32_t requested);
    
    // Update rate (thread-safe)
    void set_rate(uint64_t rate_pps);
    
    // Update burst size (thread-safe)
    void set_burst_size(uint64_t burst_size);
    
    // Get current rate
    uint64_t get_rate() const;
    
    // Get current burst size
    uint64_t get_burst_size() const;
    
    // Reset bucket to full
    void reset();
    
private:
    std::atomic<uint64_t> rate_pps_;
    std::atomic<uint64_t> burst_size_;
    std::atomic<uint64_t> tokens_;
    std::atomic<uint64_t> last_update_ns_;
    
    mutable std::mutex mutex_;
    
    // Update tokens based on elapsed time
    void update_tokens();
    
    // Get current timestamp in nanoseconds
    uint64_t get_timestamp_ns() const;
};

} // namespace trafficgen

