#pragma once

#include "core/common/types.hpp"
#include <random>
#include <cstdint>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_ether.h>

namespace trafficgen {

/**
 * Packet mutator for modifying packet templates on the fly
 * Used for changing sequence numbers, IP IDs, checksums, etc.
 */
class PacketMutator {
public:
    PacketMutator();
    ~PacketMutator();

    // Initialize RNG with a seed
    void initialize(uint64_t seed = 0);

    // Clone a template into an mbuf and apply mutations
    bool clone_and_mutate(rte_mbuf* mbuf,
                          const PacketTemplate& template_in,
                          uint32_t sequence_number,
                          bool update_checksums);

    // Specific mutation functions
    void mutate_ip_id(rte_ipv4_hdr* ip_hdr, uint16_t id);
    void mutate_tcp_seq(rte_tcp_hdr* tcp_hdr, uint32_t seq_num);
    void mutate_tcp_ack(rte_tcp_hdr* tcp_hdr, uint32_t ack_num);
    void mutate_tcp_flags(rte_tcp_hdr* tcp_hdr, uint8_t flags);

    // Checksum recalculation
    void recalculate_ip_checksum(rte_ipv4_hdr* ip_hdr);
    void recalculate_tcp_checksum(rte_ipv4_hdr* ip_hdr, rte_tcp_hdr* tcp_hdr);
    void recalculate_udp_checksum(rte_ipv4_hdr* ip_hdr, rte_udp_hdr* udp_hdr);

    // Random number generation
    uint16_t generate_ip_id();
    uint32_t generate_sequence_number();

private:
    std::mt19937 rng_;
    std::uniform_int_distribution<uint16_t> ip_id_dist_;
    std::uniform_int_distribution<uint32_t> seq_dist_;
    bool initialized_;
};

} // namespace trafficgen
