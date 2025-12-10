#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

namespace trafficgen {

// Flow identifiers
struct FlowKey {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t protocol;
    
    bool operator==(const FlowKey& other) const {
        return src_ip == other.src_ip &&
               dst_ip == other.dst_ip &&
               src_port == other.src_port &&
               dst_port == other.dst_port &&
               protocol == other.protocol;
    }
    
    struct Hash {
        size_t operator()(const FlowKey& key) const {
            return std::hash<uint32_t>{}(key.src_ip) ^
                   (std::hash<uint32_t>{}(key.dst_ip) << 1) ^
                   (std::hash<uint16_t>{}(key.src_port) << 2) ^
                   (std::hash<uint16_t>{}(key.dst_port) << 3) ^
                   (std::hash<uint8_t>{}(key.protocol) << 4);
        }
    };
};

// Packet template
struct PacketTemplate {
    std::vector<uint8_t> data;
    size_t payload_offset;
    size_t total_size;
    FlowKey flow_key;
};

// IMIX entry (internal representation - proto generates IMIXEntry class)
// Use IMIXEntryInternal to avoid conflicts with proto-generated IMIXEntry
struct IMIXEntryInternal {
    uint32_t packet_size;
    double percentage;  // 0.0 - 100.0
    std::string protocol;  // "tcp", "udp", "icmp"
    bool enable_checksum;
};

// Core statistics
struct CoreStats {
    std::atomic<uint64_t> tx_packets{0};
    std::atomic<uint64_t> rx_packets{0};
    std::atomic<uint64_t> tx_bytes{0};
    std::atomic<uint64_t> rx_bytes{0};
    std::atomic<uint64_t> errors{0};

    CoreStats() = default;

    // Custom copy to allow returning by value despite atomic members.
    CoreStats(const CoreStats& other) {
        tx_packets.store(other.tx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
        rx_packets.store(other.rx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
        tx_bytes.store(other.tx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        rx_bytes.store(other.rx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
        errors.store(other.errors.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }

    CoreStats& operator=(const CoreStats& other) {
        if (this != &other) {
            tx_packets.store(other.tx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
            rx_packets.store(other.rx_packets.load(std::memory_order_relaxed), std::memory_order_relaxed);
            tx_bytes.store(other.tx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
            rx_bytes.store(other.rx_bytes.load(std::memory_order_relaxed), std::memory_order_relaxed);
            errors.store(other.errors.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        return *this;
    }
    
    void reset() {
        tx_packets = 0;
        rx_packets = 0;
        tx_bytes = 0;
        rx_bytes = 0;
        errors = 0;
    }
};

// Global statistics
struct GlobalStats {
    CoreStats aggregate;
    std::vector<CoreStats> per_core;
    std::atomic<bool> running{false};
    
    void reset() {
        aggregate.reset();
        for (auto& core : per_core) {
            core.reset();
        }
    }
};

} // namespace trafficgen

