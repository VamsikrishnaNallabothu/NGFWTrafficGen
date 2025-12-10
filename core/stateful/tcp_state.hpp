#pragma once

#include <cstdint>
#include <chrono>

namespace trafficgen {

/**
 * Per-connection TCP state used for stateful TCP flows.
 *
 * This is intentionally lightweight and focuses on what a traffic
 * generator needs: basic handshake, simple data transfer, and
 * clean connection teardown. It does not implement full TCP
 * congestion control or retransmission logic.
 */
struct TCPConnectionState {
    // 5‑tuple identifiers (network byte order as used in FlowKey)
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;

    // TCP finite state machine states (subset of RFC 793)
    enum class State : uint8_t {
        CLOSED = 0,
        SYN_SENT,
        SYN_RCVD,
        ESTABLISHED,
        FIN_WAIT_1,
        FIN_WAIT_2,
        CLOSE_WAIT,
        CLOSING,
        LAST_ACK,
        TIME_WAIT
    } state{State::CLOSED};

    // Sequence / ACK tracking
    uint32_t send_seq{0};       // Next sequence number to send
    uint32_t send_ack{0};       // Next ACK number to send
    uint32_t recv_seq{0};       // Last received sequence number
    uint32_t recv_ack{0};       // Last received ACK number

    // Basic flow control (simple window, not full RFC implementation)
    uint16_t send_window{65535};
    uint16_t recv_window{65535};

    // Timing
    uint64_t last_activity_ns{0};
    uint64_t time_wait_end_ns{0};

    bool active{false};
};

/**
 * Minimal TCP state machine used by the traffic generator.
 *
 * It converts higher‑level events (SYN sent, FIN received, etc.)
 * into state transitions on TCPConnectionState.
 */
class TCPStateMachine {
public:
    enum class Event : uint8_t {
        SEND_SYN,
        RECV_SYN,
        RECV_SYN_ACK,
        SEND_FIN,
        RECV_FIN,
        RECV_ACK,
        APP_CLOSE,
        TIMEOUT
    };

    TCPStateMachine() = default;

    // Process a state transition. Returns true if a transition occurred.
    bool process_event(TCPConnectionState& conn, Event event);

    // Helper to get current timestamp in nanoseconds.
    static uint64_t now_ns();
};

} // namespace trafficgen


