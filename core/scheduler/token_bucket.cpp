#include "core/scheduler/token_bucket.hpp"
#include <algorithm>
#include <chrono>

namespace trafficgen {

TokenBucket::TokenBucket(uint64_t rate_pps, uint64_t burst_size)
    : rate_pps_(rate_pps), burst_size_(burst_size), tokens_(burst_size),
      last_update_ns_(get_timestamp_ns()) {
}

TokenBucket::~TokenBucket() {
}

uint32_t TokenBucket::try_consume(uint32_t requested) {
    update_tokens();
    
    uint64_t current_tokens = tokens_.load(std::memory_order_relaxed);
    uint32_t consumed = static_cast<uint32_t>(std::min(static_cast<uint64_t>(requested), current_tokens));
    
    if (consumed > 0) {
        tokens_.fetch_sub(consumed, std::memory_order_relaxed);
    }
    
    return consumed;
}

void TokenBucket::set_rate(uint64_t rate_pps) {
    rate_pps_.store(rate_pps, std::memory_order_relaxed);
}

void TokenBucket::set_burst_size(uint64_t burst_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    uint64_t current_tokens = tokens_.load(std::memory_order_relaxed);
    burst_size_.store(burst_size, std::memory_order_relaxed);
    
    // Adjust current tokens if burst size decreased
    if (current_tokens > burst_size) {
        tokens_.store(burst_size, std::memory_order_relaxed);
    }
}

uint64_t TokenBucket::get_rate() const {
    return rate_pps_.load(std::memory_order_relaxed);
}

uint64_t TokenBucket::get_burst_size() const {
    return burst_size_.load(std::memory_order_relaxed);
}

void TokenBucket::reset() {
    tokens_.store(burst_size_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    last_update_ns_.store(get_timestamp_ns(), std::memory_order_relaxed);
}

void TokenBucket::update_tokens() {
    uint64_t now_ns = get_timestamp_ns();
    uint64_t last_update = last_update_ns_.load(std::memory_order_relaxed);
    
    if (now_ns <= last_update) {
        return;  // Clock hasn't advanced
    }
    
    uint64_t elapsed_ns = now_ns - last_update;
    uint64_t rate = rate_pps_.load(std::memory_order_relaxed);
    uint64_t burst = burst_size_.load(std::memory_order_relaxed);
    
    // Calculate tokens to add: (elapsed_seconds * rate)
    // elapsed_ns is in nanoseconds, so: (elapsed_ns / 1e9) * rate
    uint64_t tokens_to_add = (elapsed_ns * rate) / 1000000000ULL;
    
    if (tokens_to_add > 0) {
        uint64_t current_tokens = tokens_.load(std::memory_order_relaxed);
        uint64_t new_tokens = std::min(current_tokens + tokens_to_add, burst);
        tokens_.store(new_tokens, std::memory_order_relaxed);
        last_update_ns_.store(now_ns, std::memory_order_relaxed);
    }
}

uint64_t TokenBucket::get_timestamp_ns() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

} // namespace trafficgen

