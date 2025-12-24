#pragma once

#include "core/common/types.hpp"
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <string>
#include <vector>
#include <memory>

namespace trafficgen {

/**
 * Packet builder for creating packet templates
 * Supports Ethernet, IP, TCP/UDP/ICMP protocols
 */
class PacketBuilder {
public:
    PacketBuilder();
    ~PacketBuilder();
    
    // Build packet template from flow configuration
    bool build_template(PacketTemplate& template_out,
                       const FlowKey& flow_key,
                       uint32_t packet_size,
                       const std::string& protocol,
                       const std::string& src_mac = "",
                       const std::string& dst_mac = "");
    
    // Build Ethernet header
    void build_ethernet_header(rte_ether_hdr* eth_hdr,
                               const std::string& src_mac,
                               const std::string& dst_mac,
                               uint16_t ether_type = RTE_ETHER_TYPE_IPV4);
    
    // Build IP header
    void build_ip_header(rte_ipv4_hdr* ip_hdr,
                        uint32_t src_ip,
                        uint32_t dst_ip,
                        uint8_t protocol,
                        uint16_t total_len);
    
    // Build TCP header
    void build_tcp_header(rte_tcp_hdr* tcp_hdr,
                         uint16_t src_port,
                         uint16_t dst_port,
                         uint32_t seq_num = 0,
                         uint32_t ack_num = 0);
    
    // Build UDP header
    void build_udp_header(rte_udp_hdr* udp_hdr,
                         uint16_t src_port,
                         uint16_t dst_port,
                         uint16_t length);

    // Convert IP string to network byte order
    uint32_t ip_to_uint32(const std::string& ip_str);
    
    // Convert MAC string to bytes
    bool mac_to_bytes(const std::string& mac_str, uint8_t* mac_bytes);
    
    // Get default MAC address (can be used if not provided)
    std::string get_default_mac();

private:
    // Fill payload with pattern
    void fill_payload(uint8_t* payload, size_t size, uint8_t pattern = 0x00);
    
    // Helper to set default MACs if not provided
    void set_default_macs(std::string& src_mac, std::string& dst_mac);
};

} // namespace trafficgen
