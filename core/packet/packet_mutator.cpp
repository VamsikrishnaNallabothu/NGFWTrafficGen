#include "core/packet/packet_mutator.hpp"
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

    uint32_t final_size = template_in.total_size;

    if (rte_pktmbuf_pkt_len(mbuf) < final_size) {
        if (rte_pktmbuf_append(mbuf, final_size - rte_pktmbuf_pkt_len(mbuf)) == nullptr) {
            return false;
        }
    } else if (rte_pktmbuf_pkt_len(mbuf) > final_size) {
        rte_pktmbuf_trim(mbuf, rte_pktmbuf_pkt_len(mbuf) - final_size);
    }

    uint8_t* data = rte_pktmbuf_mtod(mbuf, uint8_t*);

    size_t header_size = template_in.payload_offset;
    if (template_in.data.size() < header_size) {
        return false;
    }
    std::memcpy(data, template_in.data.data(), header_size);

    size_t payload_size = final_size - header_size;
    if (payload_size > 0) {
        std::memset(data + header_size, 0x00, payload_size);
    }

    mbuf->pkt_len = final_size;
    mbuf->data_len = final_size;

    rte_ipv4_hdr* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(data + sizeof(rte_ether_hdr));

    ip_hdr->total_length = rte_cpu_to_be_16(final_size - sizeof(rte_ether_hdr));

    mutate_ip_id(ip_hdr, generate_ip_id());

    if (ip_hdr->next_proto_id == IPPROTO_TCP) {
        rte_tcp_hdr* tcp_hdr = reinterpret_cast<rte_tcp_hdr*>(
            reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr));
        
        if (sequence_number > 0) {
            mutate_tcp_seq(tcp_hdr, sequence_number);
        } else {
            mutate_tcp_seq(tcp_hdr, generate_sequence_number());
        }
    } else if (ip_hdr->next_proto_id == IPPROTO_UDP) {
        rte_udp_hdr* udp_hdr = reinterpret_cast<rte_udp_hdr*>(
            reinterpret_cast<uint8_t*>(ip_hdr) + sizeof(rte_ipv4_hdr));
        udp_hdr->dgram_len = rte_cpu_to_be_16(final_size - sizeof(rte_ether_hdr) - sizeof(rte_ipv4_hdr));
    }

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
    
    ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
}

void PacketMutator::recalculate_tcp_checksum(rte_ipv4_hdr* ip_hdr, rte_tcp_hdr* tcp_hdr) {
    if (ip_hdr == nullptr || tcp_hdr == nullptr) {
        return;
    }
    
    tcp_hdr->cksum = 0;
    tcp_hdr->cksum = rte_ipv4_udptcp_cksum(ip_hdr, tcp_hdr);
}

void PacketMutator::recalculate_udp_checksum(rte_ipv4_hdr* ip_hdr, rte_udp_hdr* udp_hdr) {
    if (ip_hdr == nullptr || udp_hdr == nullptr) {
        return;
    }
    
    udp_hdr->dgram_cksum = 0;
    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);
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

} // namespace trafficgen
