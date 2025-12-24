#include "core/scheduler/token_bucket.hpp"
#include <algorithm>
#include <chrono>

namespace trafficgen {

TokenBucket::TokenBucket(uint64_t rate_pps, uint64_t burst_size)
    : rate_pps_(rate_pps), burst_size_(burst_size), tokens_(burst_size) {
    last_update_ns_.store(get_timestamp_ns(), std::memory_order_relaxed);
}

TokenBucket::~TokenBucket() {
}

uint32_t TokenBucket::try_consume(uint32_t requested) {
    update_tokens();

    uint64_t current_tokens = tokens_.load(std::memory_order_relaxed);

    if (current_tokens == 0) {
        return 0;
    }

    uint64_t consumed = std::min(static_cast<uint64_t>(requested), current_tokens);

    uint64_t expected = current_tokens;
    uint64_t desired = current_tokens - consumed;
    while (!tokens_.compare_exchange_weak(expected, desired, std::memory_order_release, std::memory_order_relaxed)) {
        current_tokens = expected;
        if (current_tokens == 0) {
            return 0;
        }
        consumed = std::min(static_cast<uint64_t>(requested), current_tokens);
        desired = current_tokens - consumed;
    }

    return static_cast<uint32_t>(consumed);
}

void TokenBucket::set_rate(uint64_t rate_pps) {
    rate_pps_.store(rate_pps, std::memory_order_relaxed);
}

void TokenBucket::set_burst_size(uint64_t burst_size) {
    std::lock_guard<std::mutex> lock(mutex_);
    burst_size_.store(burst_size, std::memory_order_relaxed);
    
    uint64_t current_tokens = tokens_.load(std::memory_order_relaxed);
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
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_.store(burst_size_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    last_update_ns_.store(get_timestamp_ns(), std::memory_order_relaxed);
}

void TokenBucket::update_tokens() {
    std::lock_guard<std::mutex> lock(mutex_);

    const uint64_t now_ns = get_timestamp_ns();
    const uint64_t last_update = last_update_ns_.load(std::memory_order_relaxed);
    
    if (now_ns <= last_update) {
        return;
    }
    
    const uint64_t elapsed_ns = now_ns - last_update;
    const uint64_t rate = rate_pps_.load(std::memory_order_relaxed);
    const uint64_t burst = burst_size_.load(std::memory_order_relaxed);
    
    if (rate == 0) {
        last_update_ns_.store(now_ns, std::memory_order_relaxed);
        return;
    }

    __uint128_t tokens_to_add_128 = static_cast<__uint128_t>(elapsed_ns) * rate;
    tokens_to_add_128 /= 1000000000ULL;
    uint64_t tokens_to_add = static_cast<uint64_t>(tokens_to_add_128);

    if (tokens_to_add > 0) {
        uint64_t current_tokens = tokens_.load(std::memory_order_relaxed);
        uint64_t new_tokens = std::min(current_tokens + tokens_to_add, burst);
        tokens_.store(new_tokens, std::memory_order_relaxed);
        last_update_ns_.store(now_ns, std::memory_order_relaxed);
    }
}

uint64_t TokenBucket::get_timestamp_ns() const {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace trafficgen
