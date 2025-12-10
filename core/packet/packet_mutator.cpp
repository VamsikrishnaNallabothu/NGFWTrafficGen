#include "core/packet/packet_mutator.hpp"
#include "core/packet/packet_builder.hpp"
#include <rte_udp.h>
#include <cstring>
#include <ctime>

namespace trafficgen {

PacketMutator::PacketMutator() : rng_(0), ip_id_dist_(1, 65535), seq_dist_(1, UINT32_MAX), initialized_(false) {
}

PacketMutator::~PacketMutator() {
}

void PacketMutator::initialize(uint64_t seed) {
    if (seed == 0) {
        seed = std::time(nullptr);
    }
    rng_.seed(seed);
    initialized_ = true;
}

bool PacketMutator::clone_and_mutate(rte_mbuf* mbuf,
                                     const PacketTemplate& template_in,
                                     uint32_t sequence_number,
                                     bool update_checksums) {
    if (mbuf == nullptr || template_in.data.empty()) {
        return false;
    }
    
    // Ensure mbuf has enough space
    if (rte_pktmbuf_pkt_len(mbuf) < template_in.total_size) {
        if (rte_pktmbuf_append(mbuf, template_in.total_size - rte_pktmbuf_pkt_len(mbuf)) == nullptr) {
            return false;
        }
    }
    
    // Copy template data to mbuf
    uint8_t* data = rte_pktmbuf_mtod(mbuf, uint8_t*);
    std::memcpy(data, template_in.data.data(), template_in.total_size);
    
    // Update packet length
    mbuf->pkt_len = template_in.total_size;
    mbuf->data_len = template_in.total_size;
    
    // Get headers
    rte_ether_hdr* eth_hdr = rte_pktmbuf_mtod(mbuf, rte_ether_hdr*);
    rte_ipv4_hdr* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(
        reinterpret_cast<uint8_t*>(eth_hdr) + sizeof(rte_ether_hdr));
    
    // Mutate IP ID
    mutate_ip_id(ip_hdr, generate_ip_id());
    
    // Mutate transport layer based on protocol
    if (ip_hdr->next_proto_id == IPPROTO_TCP) {
        rte_tcp_hdr* tcp_hdr = reinterpret_cast<rte_tcp_hdr*>(
            reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr));
        
        if (sequence_number > 0) {
            mutate_tcp_seq(tcp_hdr, sequence_number);
        } else {
            mutate_tcp_seq(tcp_hdr, generate_sequence_number());
        }
    }
    
    // Update checksums if requested
    if (update_checksums) {
        recalculate_ip_checksum(ip_hdr);
        
        if (ip_hdr->next_proto_id == IPPROTO_TCP) {
            recalculate_tcp_checksum(ip_hdr,
                reinterpret_cast<rte_tcp_hdr*>(
                    reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr)));
        } else if (ip_hdr->next_proto_id == IPPROTO_UDP) {
            recalculate_udp_checksum(ip_hdr,
                reinterpret_cast<rte_udp_hdr*>(
                    reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr)));
        }
    }
    
    return true;
}

void PacketMutator::mutate_ip_id(rte_ipv4_hdr* ip_hdr, uint16_t id) {
    if (ip_hdr == nullptr) {
        return;
    }
    ip_hdr->packet_id = rte_cpu_to_be_16(id);
}

void PacketMutator::mutate_tcp_seq(rte_tcp_hdr* tcp_hdr, uint32_t seq_num) {
    if (tcp_hdr == nullptr) {
        return;
    }
    tcp_hdr->sent_seq = rte_cpu_to_be_32(seq_num);
}

void PacketMutator::mutate_tcp_ack(rte_tcp_hdr* tcp_hdr, uint32_t ack_num) {
    if (tcp_hdr == nullptr) {
        return;
    }
    tcp_hdr->recv_ack = rte_cpu_to_be_32(ack_num);
}

void PacketMutator::mutate_tcp_flags(rte_tcp_hdr* tcp_hdr, uint8_t flags) {
    if (tcp_hdr == nullptr) {
        return;
    }
    tcp_hdr->tcp_flags = flags;
}

void PacketMutator::recalculate_ip_checksum(rte_ipv4_hdr* ip_hdr) {
    if (ip_hdr == nullptr) {
        return;
    }
    
    PacketBuilder builder;
    ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = builder.calculate_ip_checksum(ip_hdr);
}

void PacketMutator::recalculate_tcp_checksum(rte_ipv4_hdr* ip_hdr, rte_tcp_hdr* tcp_hdr) {
    if (ip_hdr == nullptr || tcp_hdr == nullptr) {
        return;
    }
    
    uint16_t tcp_len = rte_be_to_cpu_16(ip_hdr->total_length) - sizeof(rte_ipv4_hdr);
    uint16_t checksum = calculate_transport_checksum(tcp_hdr, tcp_len, ip_hdr, IPPROTO_TCP);
    tcp_hdr->cksum = checksum;
}

void PacketMutator::recalculate_udp_checksum(rte_ipv4_hdr* ip_hdr, rte_udp_hdr* udp_hdr) {
    if (ip_hdr == nullptr || udp_hdr == nullptr) {
        return;
    }
    
    uint16_t udp_len = rte_be_to_cpu_16(udp_hdr->dgram_len);
    uint16_t checksum = calculate_transport_checksum(udp_hdr, udp_len, ip_hdr, IPPROTO_UDP);
    udp_hdr->dgram_cksum = checksum;
}

uint16_t PacketMutator::generate_ip_id() {
    if (!initialized_) {
        initialize();
    }
    return ip_id_dist_(rng_);
}

uint32_t PacketMutator::generate_sequence_number() {
    if (!initialized_) {
        initialize();
    }
    return seq_dist_(rng_);
}

uint16_t PacketMutator::calculate_transport_checksum(const void* hdr,
                                                     size_t hdr_len,
                                                     const rte_ipv4_hdr* ip_hdr,
                                                     uint8_t protocol) {
    PacketBuilder builder;
    
    // Pseudo-header checksum
    uint32_t sum = builder.calculate_pseudo_header_checksum(
        ip_hdr->src_addr, ip_hdr->dst_addr, protocol, static_cast<uint16_t>(hdr_len));
    
    // Transport header checksum
    const uint16_t* words = reinterpret_cast<const uint16_t*>(hdr);
    size_t word_count = hdr_len / 2;
    
    for (size_t i = 0; i < word_count; ++i) {
        sum += rte_be_to_cpu_16(words[i]);
    }
    
    // Handle odd length
    if (hdr_len % 2 == 1) {
        auto last_byte_ptr = reinterpret_cast<const uint8_t*>(hdr);
        uint8_t last_byte = *(last_byte_ptr + hdr_len - 1);
        sum += last_byte << 8;
    }
    
    // Fold to 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

} // namespace trafficgen

