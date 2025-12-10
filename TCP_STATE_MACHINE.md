# TCP State Machine Explained

## Overview

A **TCP state machine** is a finite state machine that describes all the possible states a TCP connection can be in, and how it transitions between these states based on events (like sending/receiving packets). Unlike UDP (which is stateless), TCP is a **stateful** protocol that maintains connection state throughout its lifetime.

## Why TCP Needs a State Machine

TCP is connection-oriented:
- It requires a **3-way handshake** to establish a connection
- It maintains **sequence numbers** for reliable delivery
- It requires a **4-way handshake** to properly close a connection
- It tracks **connection state** (open, closing, closed, etc.)

Each TCP connection must track:
- Current state (LISTEN, ESTABLISHED, CLOSE_WAIT, etc.)
- Sequence numbers (for ordering and reliability)
- Acknowledgment numbers
- Window sizes (for flow control)
- Timers (for retransmission, keep-alive, etc.)

## TCP States

### Standard TCP States (RFC 793)

```
┌─────────────────────────────────────────────────────────────┐
│                    TCP STATE DIAGRAM                         │
└─────────────────────────────────────────────────────────────┘

CLOSED
  │
  │ Application: LISTEN
  ▼
LISTEN          ← Server waiting for connection
  │
  │ Receive SYN, Send SYN+ACK
  ▼
SYN_RCVD        ← Server received SYN
  │
  │ Receive ACK
  ▼
ESTABLISHED     ← Connection open, data can flow
  │
  │ (data transfer happens here)
  │
  │ Application: CLOSE / Receive FIN
  ▼
FIN_WAIT_1      ← Sent FIN, waiting for ACK
  │
  │ Receive ACK
  ▼
FIN_WAIT_2      ← Received ACK for FIN, waiting for FIN
  │
  │ Receive FIN, Send ACK
  ▼
TIME_WAIT       ← Wait 2MSL before closing
  │
  │ Timeout (2MSL)
  ▼
CLOSED

Alternative paths:
CLOSED
  │
  │ Application: CONNECT
  ▼
SYN_SENT        ← Client sent SYN, waiting for SYN+ACK
  │
  │ Receive SYN+ACK, Send ACK
  ▼
ESTABLISHED

ESTABLISHED
  │
  │ Receive FIN, Send ACK
  ▼
CLOSE_WAIT      ← Received FIN, waiting for application
  │
  │ Application: CLOSE, Send FIN
  ▼
LAST_ACK        ← Sent FIN, waiting for ACK
  │
  │ Receive ACK
  ▼
CLOSED

ESTABLISHED
  │
  │ Send FIN
  ▼
FIN_WAIT_1
  │
  │ Receive FIN+ACK, Send ACK
  ▼
CLOSING         ← Both sides closing simultaneously
  │
  │ Timeout
  ▼
TIME_WAIT
  │
  │ Timeout
  ▼
CLOSED
```

### State Descriptions

| State | Description | When It Occurs |
|-------|-------------|----------------|
| **CLOSED** | No connection | Initial/final state |
| **LISTEN** | Server listening | Server waiting for incoming connection |
| **SYN_SENT** | Client sent SYN | Client initiated connection, waiting for SYN+ACK |
| **SYN_RCVD** | Server received SYN | Server received SYN, sent SYN+ACK, waiting for ACK |
| **ESTABLISHED** | Connection open | Connection established, data can be transferred |
| **FIN_WAIT_1** | Sent FIN | First side initiated close, waiting for ACK |
| **FIN_WAIT_2** | Received ACK for FIN | Waiting for remote FIN |
| **CLOSE_WAIT** | Received FIN | Received close request, waiting for app to close |
| **CLOSING** | Both closing | Both sides closing simultaneously |
| **LAST_ACK** | Sent FIN, waiting | Waiting for final ACK |
| **TIME_WAIT** | Waiting 2MSL | Grace period before final close |

## TCP 3-Way Handshake (Connection Establishment)

```
Client                          Server
  │                               │
  │        SYN (seq=x)            │
  ├──────────────────────────────>│
  │                               │ LISTEN → SYN_RCVD
  │                               │
  │    SYN+ACK (seq=y, ack=x+1)   │
  │<──────────────────────────────┤
  │                               │
  │ SYN_SENT → ESTABLISHED        │
  │                               │
  │        ACK (ack=y+1)          │
  ├──────────────────────────────>│
  │                               │ SYN_RCVD → ESTABLISHED
  │                               │
  │     [Data Transfer]           │
  │<══════════════════════════════>│
```

## TCP 4-Way Handshake (Connection Termination)

```
Client                          Server
  │                               │
  │        FIN (seq=x)            │
  ├──────────────────────────────>│
  │                               │ ESTABLISHED → CLOSE_WAIT
  │                               │
  │        ACK (ack=x+1)          │
  │<──────────────────────────────┤
  │                               │
  │ ESTABLISHED → FIN_WAIT_1      │
  │                               │
  │                               │ [Application closes]
  │                               │
  │        FIN (seq=y)            │
  │<──────────────────────────────┤
  │                               │ CLOSE_WAIT → LAST_ACK
  │                               │
  │ FIN_WAIT_1 → FIN_WAIT_2       │
  │                               │
  │        ACK (ack=y+1)          │
  ├──────────────────────────────>│
  │                               │ LAST_ACK → CLOSED
  │                               │
  │ FIN_WAIT_2 → TIME_WAIT        │
  │                               │
  │ [Wait 2MSL timeout]           │
  │                               │
  │ TIME_WAIT → CLOSED            │
```

## State Machine Implementation for Traffic Generator

### Current Status

Your traffic generator currently supports:
- ✅ **Stateless flows**: UDP/ICMP where each packet is independent
- ⚠️ **Stateful framework**: TCP flow structure exists but state machine not implemented

### What's Needed for Stateful TCP

For each TCP flow, you need to track:

```cpp
struct TCPConnectionState {
    // Connection identifiers
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    
    // State machine
    enum State {
        CLOSED = 0,
        LISTEN,
        SYN_SENT,
        SYN_RCVD,
        ESTABLISHED,
        FIN_WAIT_1,
        FIN_WAIT_2,
        CLOSE_WAIT,
        CLOSING,
        LAST_ACK,
        TIME_WAIT
    } state;
    
    // Sequence numbers
    uint32_t send_seq;      // Next sequence number to send
    uint32_t send_ack;      // Next ACK number to send
    uint32_t recv_seq;      // Last received sequence number
    uint32_t recv_ack;      // Last received ACK number
    
    // Flow control
    uint16_t send_window;   // Send window size
    uint16_t recv_window;   // Receive window size
    
    // Timers
    uint64_t last_activity_ns;  // For timeouts
    uint64_t time_wait_end_ns;  // For TIME_WAIT state
    
    // Flags
    bool active;            // Is connection active
};
```

### Implementation Example

Here's how you might implement the state machine:

```cpp
class TCPStateMachine {
public:
    enum Event {
        SEND_SYN,
        RECV_SYN,
        SEND_ACK,
        RECV_ACK,
        SEND_FIN,
        RECV_FIN,
        SEND_DATA,
        RECV_DATA,
        TIMEOUT,
        APP_CLOSE
    };
    
    bool process_event(TCPConnectionState& conn, Event event, 
                       const rte_tcp_hdr* tcp_hdr = nullptr) {
        switch (conn.state) {
            case CLOSED:
                if (event == SEND_SYN) {
                    conn.state = SYN_SENT;
                    return true;
                }
                if (event == RECV_SYN) {
                    conn.state = SYN_RCVD;
                    return true;
                }
                break;
                
            case SYN_SENT:
                if (event == RECV_SYN && event == RECV_ACK) {
                    conn.state = ESTABLISHED;
                    return true;
                }
                break;
                
            case SYN_RCVD:
                if (event == RECV_ACK) {
                    conn.state = ESTABLISHED;
                    return true;
                }
                break;
                
            case ESTABLISHED:
                if (event == APP_CLOSE || event == SEND_FIN) {
                    conn.state = FIN_WAIT_1;
                    return true;
                }
                if (event == RECV_FIN) {
                    conn.state = CLOSE_WAIT;
                    return true;
                }
                // Data transfer happens here
                break;
                
            case FIN_WAIT_1:
                if (event == RECV_ACK) {
                    conn.state = FIN_WAIT_2;
                    return true;
                }
                if (event == RECV_FIN) {
                    conn.state = CLOSING;
                    return true;
                }
                break;
                
            case FIN_WAIT_2:
                if (event == RECV_FIN) {
                    conn.state = TIME_WAIT;
                    return true;
                }
                break;
                
            case CLOSE_WAIT:
                if (event == APP_CLOSE || event == SEND_FIN) {
                    conn.state = LAST_ACK;
                    return true;
                }
                break;
                
            case CLOSING:
                if (event == RECV_ACK) {
                    conn.state = TIME_WAIT;
                    return true;
                }
                break;
                
            case LAST_ACK:
                if (event == RECV_ACK) {
                    conn.state = CLOSED;
                    return true;
                }
                break;
                
            case TIME_WAIT:
                if (event == TIMEOUT) {
                    conn.state = CLOSED;
                    return true;
                }
                break;
        }
        return false;
    }
};
```

### Integration with Your Traffic Generator

For stateful TCP flows, you would:

1. **Per-Flow State Storage**: Extend `FlowConfig` to include TCP state
   ```cpp
   struct FlowConfig {
       // ... existing fields ...
       bool stateless;
       
       // For stateful TCP
       std::unique_ptr<TCPConnectionState> tcp_state;
       std::unique_ptr<TCPStateMachine> state_machine;
   };
   ```

2. **RX Processing**: When receiving TCP packets:
   - Parse TCP header
   - Extract flags (SYN, ACK, FIN, etc.)
   - Update connection state
   - Update sequence numbers
   - Generate appropriate response packets

3. **TX Packet Generation**: When sending TCP packets:
   - Check current state
   - Generate appropriate flags
   - Set correct sequence/ACK numbers
   - Update state after sending

4. **State Transitions**: Use state machine to transition based on:
   - Received packets
   - Application actions
   - Timeouts

## Why This Matters for Your Traffic Generator

### Stateless vs Stateful

**Stateless (Current Implementation)**:
- Each packet is independent
- No memory of previous packets
- Simple to implement
- Works for UDP, ICMP
- ❌ Doesn't work properly for TCP (needs handshake)

**Stateful (TCP State Machine Required)**:
- Maintains connection state
- Tracks sequence numbers
- Handles handshakes
- Proper connection lifecycle
- ✅ Required for realistic TCP traffic generation

### Use Cases

**Stateless TCP (Current)**:
- Firewall testing (packet filtering)
- DDoS simulation
- Basic performance testing

**Stateful TCP (With State Machine)**:
- Application testing
- Load balancer testing
- Full protocol stack testing
- Realistic traffic patterns
- Connection tracking tests

## Implementation Complexity

| Feature | Complexity | Required Components |
|---------|-----------|-------------------|
| Basic state tracking | Medium | State enum, per-flow storage |
| Sequence numbers | Medium | Sequence/ACK tracking, arithmetic |
| Handshake handling | High | Multiple packet exchanges, timers |
| Data transfer | Medium | Sequence number updates |
| Connection teardown | High | FIN handling, TIME_WAIT |
| Timeouts & retransmission | High | Timer management, state recovery |
| Window management | High | Flow control implementation |

## Next Steps for Implementation

1. **Add TCP State Structure** to `core/common/types.hpp`
2. **Create TCP State Machine Class** in `core/stateful/` directory
3. **Extend FlowScheduler** to handle TCP state transitions
4. **Modify Packet Builder** to generate state-appropriate TCP packets
5. **Add RX Processing** to handle incoming TCP packets
6. **Implement Timers** for timeouts and retransmissions

## References

- RFC 793: Transmission Control Protocol
- RFC 1122: Requirements for Internet Hosts
- TCP/IP Illustrated, Volume 1 (W. Richard Stevens)

---

**Note**: Your current implementation has the framework for stateful flows (the `stateless` flag in FlowConfig), but the actual TCP state machine needs to be implemented to make stateful TCP flows functional.

