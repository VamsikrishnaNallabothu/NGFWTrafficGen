#include "core/scheduler/flow_scheduler.hpp"
#include <algorithm>
#include <chrono>

namespace trafficgen {

FlowScheduler::FlowScheduler() {
}

FlowScheduler::~FlowScheduler() {
}

bool FlowScheduler::initialize() {
    return true;
}

bool FlowScheduler::add_flow(const FlowConfigInternal& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    FlowConfigInternal new_config = config;
    
    // Build packet template if not already built
    if (new_config.template_packet.data.empty()) {
        if (!packet_builder_.build_template(
                new_config.template_packet,
                new_config.flow_key,
                new_config.packet_size,
                new_config.protocol)) {
            return false;
        }
    }
    
    // Create token bucket for rate limiting
    if (!new_config.token_bucket) {
        new_config.token_bucket = std::make_unique<TokenBucket>(
            new_config.pps, new_config.pps / 10);  // Burst = 10% of rate
    }

    // Initialize TCP state for stateful TCP flows
    if (!new_config.stateless && new_config.protocol == "tcp") {
        if (!new_config.tcp_state) {
            new_config.tcp_state = std::make_unique<TCPConnectionState>();
            new_config.tcp_state->src_ip = new_config.flow_key.src_ip;
            new_config.tcp_state->dst_ip = new_config.flow_key.dst_ip;
            new_config.tcp_state->src_port = new_config.flow_key.src_port;
            new_config.tcp_state->dst_port = new_config.flow_key.dst_port;
            new_config.tcp_state->state = TCPConnectionState::State::CLOSED;
            new_config.tcp_state->active = false;
        }
        if (!new_config.tcp_state_machine) {
            new_config.tcp_state_machine = std::make_unique<TCPStateMachine>();
        }
    }
    
    // Insert / update flow
    flows_[config.flow_id] = std::move(new_config);

    // Update lookup table for RX path (both directions)
    const FlowConfigInternal& stored = flows_[config.flow_id];
    flow_lookup_[stored.flow_key] = config.flow_id;
    // Reverse key for inbound packets
    FlowKey reverse_key = stored.flow_key;
    std::swap(reverse_key.src_ip, reverse_key.dst_ip);
    std::swap(reverse_key.src_port, reverse_key.dst_port);
    flow_lookup_[reverse_key] = config.flow_id;

    return true;
}

bool FlowScheduler::remove_flow(uint32_t flow_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = flows_.find(flow_id);
    if (it == flows_.end()) {
        return false;
    }

    // Remove from lookup map (both directions)
    FlowKey key = it->second.flow_key;
    flow_lookup_.erase(key);
    FlowKey reverse_key = key;
    std::swap(reverse_key.src_ip, reverse_key.dst_ip);
    std::swap(reverse_key.src_port, reverse_key.dst_port);
    flow_lookup_.erase(reverse_key);

    flows_.erase(it);
    return true;
}

bool FlowScheduler::start_flow(uint32_t flow_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = flows_.find(flow_id);
    if (it == flows_.end()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    
    it->second.start_time_ns.store(now_ns, std::memory_order_relaxed);
    it->second.active.store(true, std::memory_order_relaxed);

    // Initialize TCP connection state for stateful TCP flows
    if (!it->second.stateless && it->second.protocol == "tcp" &&
        it->second.tcp_state) {
        TCPConnectionState& conn = *it->second.tcp_state;
        conn.state = TCPConnectionState::State::ESTABLISHED;
        conn.active = true;
        // Simple initial sequence number
        conn.send_seq = 1;
        conn.recv_seq = 0;
    }
    
    if (it->second.token_bucket) {
        it->second.token_bucket->reset();
    }
    
    return true;
}

bool FlowScheduler::stop_flow(uint32_t flow_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = flows_.find(flow_id);
    if (it == flows_.end()) {
        return false;
    }
    
    it->second.active.store(false, std::memory_order_relaxed);
    return true;
}

std::vector<uint32_t> FlowScheduler::get_ready_flows(uint32_t max_flows) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint32_t> ready_flows;
    ready_flows.reserve(max_flows);
    
    for (auto& [flow_id, config] : flows_) {
        if (!config.active.load(std::memory_order_relaxed)) {
            continue;
        }
        
        // Check if flow expired
        if (is_flow_expired(flow_id)) {
            config.active.store(false, std::memory_order_relaxed);
            continue;
        }
        
        // Check if flow has tokens available
        if (config.token_bucket && config.token_bucket->try_consume(1) > 0) {
            ready_flows.push_back(flow_id);
            
            if (ready_flows.size() >= max_flows) {
                break;
            }
        }
    }
    
    return ready_flows;
}

void FlowScheduler::update_flow_stats(uint32_t flow_id, uint64_t packets, uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = flows_.find(flow_id);
    if (it != flows_.end()) {
        it->second.packets_sent.fetch_add(packets, std::memory_order_relaxed);
        it->second.bytes_sent.fetch_add(bytes, std::memory_order_relaxed);
    }
}

FlowConfigInternal* FlowScheduler::get_flow(uint32_t flow_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = flows_.find(flow_id);
    if (it == flows_.end()) {
        return nullptr;
    }
    
    return &it->second;
}

std::vector<uint32_t> FlowScheduler::get_active_flows() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<uint32_t> active_flows;
    
    for (const auto& [flow_id, config] : flows_) {
        if (config.active.load(std::memory_order_relaxed)) {
            active_flows.push_back(flow_id);
        }
    }
    
    return active_flows;
}

std::unordered_map<uint32_t, FlowConfigInternal> FlowScheduler::get_all_flows_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flows_;
}

void FlowScheduler::handle_tcp_rx(const FlowKey& key,
                                  const rte_tcp_hdr* tcp_hdr,
                                  uint32_t payload_len) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it_id = flow_lookup_.find(key);
    if (it_id == flow_lookup_.end()) {
        return;
    }

    auto it_flow = flows_.find(it_id->second);
    if (it_flow == flows_.end()) {
        return;
    }

    FlowConfigInternal& flow = it_flow->second;
    if (flow.stateless || flow.protocol != "tcp" ||
        !flow.tcp_state || !flow.tcp_state_machine) {
        return;
    }

    TCPConnectionState& conn = *flow.tcp_state;

    // Extract flags
    uint8_t flags = tcp_hdr->tcp_flags;

    // SYN+ACK handshake completion
    if ((flags & (RTE_TCP_SYN_FLAG | RTE_TCP_ACK_FLAG)) ==
        (RTE_TCP_SYN_FLAG | RTE_TCP_ACK_FLAG)) {
        conn.recv_seq = rte_be_to_cpu_32(tcp_hdr->sent_seq);
        conn.recv_ack = rte_be_to_cpu_32(tcp_hdr->recv_ack);
        flow.tcp_state_machine->process_event(conn, TCPStateMachine::Event::RECV_SYN_ACK);
    } else {
        if (flags & RTE_TCP_ACK_FLAG) {
            conn.recv_ack = rte_be_to_cpu_32(tcp_hdr->recv_ack);
            flow.tcp_state_machine->process_event(conn, TCPStateMachine::Event::RECV_ACK);
        }
        if (flags & RTE_TCP_FIN_FLAG) {
            conn.recv_seq = rte_be_to_cpu_32(tcp_hdr->sent_seq);
            flow.tcp_state_machine->process_event(conn, TCPStateMachine::Event::RECV_FIN);
        }
    }

    // Very simple receive‑side sequence tracking
    if (payload_len > 0) {
        conn.recv_seq = rte_be_to_cpu_32(tcp_hdr->sent_seq) + payload_len;
    }
}

bool FlowScheduler::is_flow_expired(uint32_t flow_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = flows_.find(flow_id);
    if (it == flows_.end()) {
        return true;
    }
    
    const auto& config = it->second;
    
    // If duration is 0, flow never expires
    if (config.duration_seconds == 0) {
        return false;
    }
    
    uint64_t start_time = config.start_time_ns.load(std::memory_order_relaxed);
    if (start_time == 0) {
        return false;  // Not started yet
    }
    
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
    
    uint64_t elapsed_ns = now_ns - start_time;
    uint64_t duration_ns = static_cast<uint64_t>(config.duration_seconds) * 1000000000ULL;
    
    return elapsed_ns >= duration_ns;
}

void FlowScheduler::clear_all_flows() {
    std::lock_guard<std::mutex> lock(mutex_);
    flows_.clear();
}

size_t FlowScheduler::get_flow_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return flows_.size();
}

bool FlowScheduler::should_flow_be_active(const FlowConfigInternal& config) const {
    return config.active.load(std::memory_order_relaxed) && !is_flow_expired(config.flow_id);
}

} // namespace trafficgen

