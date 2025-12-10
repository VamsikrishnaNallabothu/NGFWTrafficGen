#pragma once

#include "core/common/types.hpp"
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <cstdint>
#include <random>

namespace trafficgen {

/**
 * Packet mutator for modifying packet fields per transmission
 * Used for sequence numbers, IDs, checksums, etc.
 */
class PacketMutator {
public:
    PacketMutator();
    ~PacketMutator();
    
    // Initialize mutator with seed
    void initialize(uint64_t seed = 0);
    
    // Clone template into mbuf and mutate for transmission
    bool clone_and_mutate(rte_mbuf* mbuf,
                         const PacketTemplate& template_in,
                         uint32_t sequence_number = 0,
                         bool update_checksums = true);
    
    // Update IP ID field
    void mutate_ip_id(rte_ipv4_hdr* ip_hdr, uint16_t id);
    
    // Update TCP sequence number
    void mutate_tcp_seq(rte_tcp_hdr* tcp_hdr, uint32_t seq_num);
    
    // Update TCP acknowledgment number
    void mutate_tcp_ack(rte_tcp_hdr* tcp_hdr, uint32_t ack_num);
    
    // Update TCP flags
    void mutate_tcp_flags(rte_tcp_hdr* tcp_hdr, uint8_t flags);
    
    // Recalculate IP checksum
    void recalculate_ip_checksum(rte_ipv4_hdr* ip_hdr);
    
    // Recalculate TCP checksum
    void recalculate_tcp_checksum(rte_ipv4_hdr* ip_hdr, rte_tcp_hdr* tcp_hdr);
    
    // Recalculate UDP checksum
    void recalculate_udp_checksum(rte_ipv4_hdr* ip_hdr, rte_udp_hdr* udp_hdr);
    
    // Generate random IP ID
    uint16_t generate_ip_id();
    
    // Generate random sequence number
    uint32_t generate_sequence_number();

private:
    std::mt19937_64 rng_;
    std::uniform_int_distribution<uint16_t> ip_id_dist_;
    std::uniform_int_distribution<uint32_t> seq_dist_;
    bool initialized_;
    
    // Helper to calculate TCP/UDP checksum with pseudo-header
    uint16_t calculate_transport_checksum(const void* hdr,
                                         size_t hdr_len,
                                         const rte_ipv4_hdr* ip_hdr,
                                         uint8_t protocol);
};

} // namespace trafficgen

