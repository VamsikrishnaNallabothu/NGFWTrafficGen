#include "core/packet/packet_builder.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace trafficgen {

PacketBuilder::PacketBuilder() {
}

PacketBuilder::~PacketBuilder() {
}

bool PacketBuilder::build_template(PacketTemplate& template_out,
                                   const FlowKey& flow_key,
                                   uint32_t packet_size,
                                   const std::string& protocol,
                                   const std::string& src_mac,
                                   const std::string& dst_mac) {
    template_out.flow_key = flow_key;
    template_out.total_size = packet_size;
    
    // Calculate header sizes
    size_t eth_header_size = sizeof(rte_ether_hdr);
    size_t ip_header_size = sizeof(rte_ipv4_hdr);
    size_t transport_header_size = 0;
    
    if (protocol == "tcp") {
        transport_header_size = sizeof(rte_tcp_hdr);
    } else if (protocol == "udp") {
        transport_header_size = sizeof(rte_udp_hdr);
    } else if (protocol == "icmp") {
        transport_header_size = 8;  // ICMP header minimum
    } else {
        return false;  // Unsupported protocol
    }
    
    size_t total_header_size = eth_header_size + ip_header_size + transport_header_size;
    if (packet_size < total_header_size) {
        return false;  // Packet size too small
    }
    size_t payload_size = packet_size - total_header_size;
    
    template_out.data.resize(packet_size);
    uint8_t* data = template_out.data.data();
    
    // Prepare MAC addresses
    std::string src_mac_final = src_mac;
    std::string dst_mac_final = dst_mac;
    set_default_macs(src_mac_final, dst_mac_final);
    
    // Build Ethernet header
    rte_ether_hdr* eth_hdr = reinterpret_cast<rte_ether_hdr*>(data);
    build_ethernet_header(eth_hdr, src_mac_final, dst_mac_final);
    
    // Build IP header
    rte_ipv4_hdr* ip_hdr = reinterpret_cast<rte_ipv4_hdr*>(data + eth_header_size);
    uint8_t ip_protocol = 0;
    if (protocol == "tcp") {
        ip_protocol = IPPROTO_TCP;
    } else if (protocol == "udp") {
        ip_protocol = IPPROTO_UDP;
    } else if (protocol == "icmp") {
        ip_protocol = IPPROTO_ICMP;
    }
    
    build_ip_header(ip_hdr, flow_key.src_ip, flow_key.dst_ip, ip_protocol, packet_size - eth_header_size);
    
    // Build transport header
    uint8_t* transport_hdr = data + eth_header_size + ip_header_size;
    template_out.payload_offset = eth_header_size + ip_header_size + transport_header_size;
    
    if (protocol == "tcp") {
        rte_tcp_hdr* tcp_hdr = reinterpret_cast<rte_tcp_hdr*>(transport_hdr);
        build_tcp_header(tcp_hdr, flow_key.src_port, flow_key.dst_port);
    } else if (protocol == "udp") {
        rte_udp_hdr* udp_hdr = reinterpret_cast<rte_udp_hdr*>(transport_hdr);
        build_udp_header(udp_hdr, flow_key.src_port, flow_key.dst_port,
                        transport_header_size + payload_size);
    } else if (protocol == "icmp") {
        // ICMP header - simple echo request
        transport_hdr[0] = 8;  // Echo Request
        transport_hdr[1] = 0;  // Code
        *reinterpret_cast<uint16_t*>(&transport_hdr[2]) = 0;  // Checksum (calculated later)
        *reinterpret_cast<uint16_t*>(&transport_hdr[4]) = 0;  // Identifier
        *reinterpret_cast<uint16_t*>(&transport_hdr[6]) = 0;  // Sequence number
    }
    
    // Fill payload
    if (payload_size > 0) {
        fill_payload(data + template_out.payload_offset, payload_size);
    }
    
    // Calculate and set IP checksum
    ip_hdr->hdr_checksum = 0;
    ip_hdr->hdr_checksum = calculate_ip_checksum(ip_hdr);
    
    return true;
}

void PacketBuilder::build_ethernet_header(rte_ether_hdr* eth_hdr,
                                         const std::string& src_mac,
                                         const std::string& dst_mac,
                                         uint16_t ether_type) {
    if (eth_hdr == nullptr) {
        return;
    }
    
    mac_to_bytes(dst_mac, eth_hdr->d_addr.addr_bytes);
    mac_to_bytes(src_mac, eth_hdr->s_addr.addr_bytes);
    eth_hdr->ether_type = rte_cpu_to_be_16(ether_type);
}

void PacketBuilder::build_ip_header(rte_ipv4_hdr* ip_hdr,
                                    uint32_t src_ip,
                                    uint32_t dst_ip,
                                    uint8_t protocol,
                                    uint16_t total_len) {
    if (ip_hdr == nullptr) {
        return;
    }
    
    ip_hdr->version_ihl = (4 << 4) | (sizeof(rte_ipv4_hdr) / 4);
    ip_hdr->type_of_service = 0;
    ip_hdr->total_length = rte_cpu_to_be_16(total_len);
    ip_hdr->packet_id = 0;
    ip_hdr->fragment_offset = 0;
    ip_hdr->time_to_live = 64;
    ip_hdr->next_proto_id = protocol;
    ip_hdr->src_addr = src_ip;
    ip_hdr->dst_addr = dst_ip;
    ip_hdr->hdr_checksum = 0;  // Will be calculated later
}

void PacketBuilder::build_tcp_header(rte_tcp_hdr* tcp_hdr,
                                     uint16_t src_port,
                                     uint16_t dst_port,
                                     uint32_t seq_num,
                                     uint32_t ack_num) {
    if (tcp_hdr == nullptr) {
        return;
    }
    
    tcp_hdr->src_port = rte_cpu_to_be_16(src_port);
    tcp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
    tcp_hdr->sent_seq = rte_cpu_to_be_32(seq_num);
    tcp_hdr->recv_ack = rte_cpu_to_be_32(ack_num);
    tcp_hdr->data_off = (sizeof(rte_tcp_hdr) / 4) << 4;
    tcp_hdr->tcp_flags = 0;
    tcp_hdr->rx_win = 0;
    tcp_hdr->tcp_urp = 0;
    tcp_hdr->cksum = 0;  // Will be calculated later
}

void PacketBuilder::build_udp_header(rte_udp_hdr* udp_hdr,
                                     uint16_t src_port,
                                     uint16_t dst_port,
                                     uint16_t length) {
    if (udp_hdr == nullptr) {
        return;
    }
    
    udp_hdr->src_port = rte_cpu_to_be_16(src_port);
    udp_hdr->dst_port = rte_cpu_to_be_16(dst_port);
    udp_hdr->dgram_len = rte_cpu_to_be_16(length);
    udp_hdr->dgram_cksum = 0;  // Optional for UDP over IPv4
}

uint16_t PacketBuilder::calculate_ip_checksum(const rte_ipv4_hdr* ip_hdr) {
    if (ip_hdr == nullptr) {
        return 0;
    }
    
    uint32_t sum = 0;
    const uint16_t* words = reinterpret_cast<const uint16_t*>(ip_hdr);
    
    for (int i = 0; i < (int)(ip_hdr->version_ihl & 0x0F) * 2; ++i) {
        sum += rte_be_to_cpu_16(words[i]);
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

uint16_t PacketBuilder::calculate_pseudo_header_checksum(uint32_t src_ip,
                                                         uint32_t dst_ip,
                                                         uint8_t protocol,
                                                         uint16_t len) {
    struct pseudo_header {
        uint32_t src_ip;
        uint32_t dst_ip;
        uint8_t zero;
        uint8_t protocol;
        uint16_t length;
    } __attribute__((packed));
    
    pseudo_header ph;
    ph.src_ip = src_ip;
    ph.dst_ip = dst_ip;
    ph.zero = 0;
    ph.protocol = protocol;
    ph.length = rte_cpu_to_be_16(len);
    
    uint32_t sum = 0;
    const uint16_t* words = reinterpret_cast<const uint16_t*>(&ph);
    
    for (size_t i = 0; i < sizeof(pseudo_header) / 2; ++i) {
        sum += words[i];
    }
    
    return sum;
}

uint32_t PacketBuilder::ip_to_uint32(const std::string& ip_str) {
    struct in_addr addr;
    if (inet_aton(ip_str.c_str(), &addr) == 0) {
        return 0;
    }
    return addr.s_addr;
}

bool PacketBuilder::mac_to_bytes(const std::string& mac_str, uint8_t* mac_bytes) {
    if (mac_bytes == nullptr) {
        return false;
    }
    
    if (mac_str.empty()) {
        // Default MAC: 00:00:00:00:00:00
        std::memset(mac_bytes, 0, RTE_ETHER_ADDR_LEN);
        return true;
    }
    
    unsigned int bytes[6];
    if (std::sscanf(mac_str.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
                    &bytes[0], &bytes[1], &bytes[2],
                    &bytes[3], &bytes[4], &bytes[5]) != 6) {
        // Try without colons
        if (std::sscanf(mac_str.c_str(), "%02x%02x%02x%02x%02x%02x",
                        &bytes[0], &bytes[1], &bytes[2],
                        &bytes[3], &bytes[4], &bytes[5]) != 6) {
            std::memset(mac_bytes, 0, RTE_ETHER_ADDR_LEN);
            return false;
        }
    }
    
    for (int i = 0; i < 6; ++i) {
        mac_bytes[i] = static_cast<uint8_t>(bytes[i]);
    }
    
    return true;
}

std::string PacketBuilder::get_default_mac() {
    return "00:00:00:00:00:00";
}

void PacketBuilder::fill_payload(uint8_t* payload, size_t size, uint8_t pattern) {
    if (payload == nullptr || size == 0) {
        return;
    }
    
    // Fill with pattern, can be enhanced for different patterns
    std::memset(payload, pattern, size);
}

void PacketBuilder::set_default_macs(std::string& src_mac, std::string& dst_mac) {
    if (src_mac.empty()) {
        src_mac = "00:01:02:03:04:05";
    }
    if (dst_mac.empty()) {
        dst_mac = "00:06:07:08:09:0A";
    }
}

} // namespace trafficgen

