#pragma once

#include "core/common/types.hpp"
#include "core/packet/packet_builder.hpp"
#include "core/scheduler/token_bucket.hpp"
#include "core/stateful/tcp_state.hpp"
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <cstdint>
#include <atomic>
#include <rte_ether.h>

namespace trafficgen {

// Flow configuration (internal representation, proto has same name)
struct FlowConfigInternal {
    uint32_t flow_id;
    FlowKey flow_key;
    uint32_t packet_size;
    std::string protocol;
    uint64_t pps;  // Packets per second
    uint32_t duration_seconds;  // 0 means infinite
    bool stateless;
    std::string dst_mac; // Destination MAC address for this flow
    PacketTemplate template_packet;
    std::unique_ptr<TokenBucket> token_bucket;
    std::unique_ptr<TCPConnectionState> tcp_state;
    std::unique_ptr<TCPStateMachine> tcp_state_machine;
    
    std::atomic<uint64_t> packets_sent{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> start_time_ns{0};
    std::atomic<bool> active{false};
};

// Lightweight, copyable snapshot for reporting.
struct FlowSnapshot {
    uint32_t flow_id;
    FlowKey flow_key;
    uint32_t packet_size;
    std::string protocol;
    uint64_t pps;
    uint32_t duration_seconds;
    bool stateless;
    uint64_t packets_sent;
    uint64_t bytes_sent;
    uint64_t start_time_ns;
    bool active;
};

class FlowScheduler {
public:
    FlowScheduler();
    ~FlowScheduler();
    
    // Initialize scheduler with the source MAC of the TX port
    bool initialize(const rte_ether_addr& src_mac);
    
    // Add flow configuration
    bool add_flow(FlowConfigInternal& config);
    
    // Remove flow
    bool remove_flow(uint32_t flow_id);
    
    // Start flow
    bool start_flow(uint32_t flow_id);
    
    // Stop flow
    bool stop_flow(uint32_t flow_id);
    
    // Get flows ready for transmission
    std::vector<uint32_t> get_ready_flows(uint32_t max_flows);
    
    // Update flow statistics
    void update_flow_stats(uint32_t flow_id, uint64_t packets, uint64_t bytes);
    
    // Get flow configuration
    std::shared_ptr<FlowConfigInternal> get_flow(uint32_t flow_id);
    
    // Get all active flows
    std::vector<uint32_t> get_active_flows() const;
    
    // Get snapshot of per-flow statistics
    std::unordered_map<uint32_t, FlowSnapshot> get_all_flows_snapshot() const;

    // Handle an incoming TCP packet
    void handle_tcp_rx(const FlowKey& key,
                       const rte_tcp_hdr* tcp_hdr,
                       uint32_t payload_len);

    // Check if flow is expired
    bool is_flow_expired(uint32_t flow_id) const;
    
    // Clear all flows
    void clear_all_flows();
    
    // Get total number of flows
    size_t get_flow_count() const;

private:
    std::unordered_map<uint32_t, std::shared_ptr<FlowConfigInternal>> flows_;
    std::unordered_map<FlowKey, uint32_t, FlowKey::Hash> flow_lookup_;
    mutable std::mutex mutex_;
    PacketBuilder packet_builder_;
    rte_ether_addr src_mac_; // Real MAC of the egress port

    bool is_flow_expired_locked(const FlowConfigInternal& config,
                                uint64_t now_ns) const;
};

} // namespace trafficgen
