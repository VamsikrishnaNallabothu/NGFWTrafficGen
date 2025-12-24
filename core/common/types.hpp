#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>

namespace trafficgen {

// 5-tuple flow key
struct FlowKey {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;

    bool operator==(const FlowKey& other) const {
        return src_ip == other.src_ip && dst_ip == other.dst_ip &&
               src_port == other.src_port && dst_port == other.dst_port &&
               protocol == other.protocol;
    }

    struct Hash {
        std::size_t operator()(const FlowKey& k) const {
            return std::hash<uint64_t>()((uint64_t)k.src_ip << 32 | k.dst_ip) ^
                   std::hash<uint32_t>()((uint32_t)k.src_port << 16 | k.dst_port) ^
                   std::hash<uint8_t>()(k.protocol);
        }
    };
};

// Packet template for building packets
struct PacketTemplate {
    FlowKey flow_key;
    uint32_t total_size;
    uint32_t payload_offset;
    std::vector<uint8_t> data;
};

// Per-core statistics using atomic types for thread-safe updates
struct CoreStats {
    std::atomic<uint64_t> tx_packets{0};
    std::atomic<uint64_t> rx_packets{0};
    std::atomic<uint64_t> tx_bytes{0};
    std::atomic<uint64_t> rx_bytes{0};
    std::atomic<uint64_t> errors{0};

    // Default constructor
    CoreStats() = default;

    // --- THE CRITICAL FIX ---
    // std::atomic makes a class non-movable and non-copyable by default.
    // We must provide a custom move constructor and move assignment operator
    // to allow this struct to be stored in a std::vector, which requires
    // its elements to be movable.
    CoreStats(CoreStats&& other) noexcept {
        tx_packets.store(other.tx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
        rx_packets.store(other.rx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_bytes.store(other.tx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        rx_bytes.store(other.rx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        errors.store(other.errors.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    CoreStats& operator=(CoreStats&& other) noexcept {
        if (this != &other) {
            tx_packets.store(other.tx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
            rx_packets.store(other.rx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
            tx_bytes.store(other.tx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
            rx_bytes.store(other.rx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
            errors.store(other.errors.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }

    // Explicitly delete the copy constructor and copy assignment operator
    CoreStats(const CoreStats&) = delete;
    CoreStats& operator=(const CoreStats&) = delete;


    void reset() {
        tx_packets.store(0, std::memory_order_relaxed);
        rx_packets.store(0, std::memory_order_relaxed);
        tx_bytes.store(0, std::memory_order_relaxed);
        rx_bytes.store(0, std::memory_order_relaxed);
        errors.store(0, std::memory_order_relaxed);
    }
};

// A non-atomic, copyable snapshot of CoreStats for reporting
struct CoreStatsSnapshot {
    uint64_t tx_packets{0};
    uint64_t rx_packets{0};
    uint64_t tx_bytes{0};
    uint64_t rx_bytes{0};
    uint64_t errors{0};
};

// IMIX profile entry
struct IMIXEntryInternal {
    uint32_t packet_size;
    double percentage;
    std::string protocol;
    bool enable_checksum;
};

} // namespace trafficgen
