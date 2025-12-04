#include "core/stateful/tcp_state.hpp"

namespace trafficgen {

bool TCPStateMachine::process_event(TCPConnectionState& conn, Event event) {
    using State = TCPConnectionState::State;

    conn.last_activity_ns = now_ns();

    switch (conn.state) {
    case State::CLOSED:
        if (event == Event::SEND_SYN) {
            conn.state = State::SYN_SENT;
            conn.active = true;
            return true;
        }
        break;

    case State::SYN_SENT:
        if (event == Event::RECV_SYN_ACK) {
            conn.state = State::ESTABLISHED;
            return true;
        }
        break;

    case State::SYN_RCVD:
        if (event == Event::RECV_ACK) {
            conn.state = State::ESTABLISHED;
            return true;
        }
        break;

    case State::ESTABLISHED:
        if (event == Event::APP_CLOSE || event == Event::SEND_FIN) {
            conn.state = State::FIN_WAIT_1;
            return true;
        }
        if (event == Event::RECV_FIN) {
            conn.state = State::CLOSE_WAIT;
            return true;
        }
        break;

    case State::FIN_WAIT_1:
        if (event == Event::RECV_ACK) {
            conn.state = State::FIN_WAIT_2;
            return true;
        }
        if (event == Event::RECV_FIN) {
            conn.state = State::CLOSING;
            return true;
        }
        break;

    case State::FIN_WAIT_2:
        if (event == Event::RECV_FIN) {
            conn.state = State::TIME_WAIT;
            // TIME_WAIT timeout will move to CLOSED
            // Caller should set time_wait_end_ns based on policy.
            return true;
        }
        break;

    case State::CLOSE_WAIT:
        if (event == Event::APP_CLOSE || event == Event::SEND_FIN) {
            conn.state = State::LAST_ACK;
            return true;
        }
        break;

    case State::CLOSING:
        if (event == Event::RECV_ACK) {
            conn.state = State::TIME_WAIT;
            return true;
        }
        break;

    case State::LAST_ACK:
        if (event == Event::RECV_ACK) {
            conn.state = State::CLOSED;
            conn.active = false;
            return true;
        }
        break;

    case State::TIME_WAIT:
        if (event == Event::TIMEOUT) {
            conn.state = State::CLOSED;
            conn.active = false;
            return true;
        }
        break;
    }

    return false;
}

uint64_t TCPStateMachine::now_ns() {
    auto now = std::chrono::steady_clock::now();
    auto d = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
}

} // namespace trafficgen


