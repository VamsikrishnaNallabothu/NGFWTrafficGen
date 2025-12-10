#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <memory>

namespace trafficgen {

// Configuration structure
struct AppConfig {
    struct PortConfig {
        uint16_t port_id;
        uint16_t nb_rx_queues;
        uint16_t nb_tx_queues;
        uint16_t nb_rx_desc;
        uint16_t nb_tx_desc;
    };
    
    struct CoreMapping {
        uint32_t core_id;
        uint16_t port_id;
        uint16_t queue_id;
        bool enable_rx;
    };
    
    struct GRPCConfig {
        std::string listen_address;
        uint16_t port;
    };
    
    std::string hugepage_path;
    uint32_t num_cores;
    std::vector<PortConfig> ports;
    std::vector<CoreMapping> core_mappings;
    GRPCConfig grpc_config;
    uint32_t mbufs_per_socket;
    uint32_t mbuf_cache_size;
    uint16_t tx_burst_size;
    uint16_t rx_burst_size;
};

/**
 * Configuration parser for JSON configuration files
 */
class ConfigParser {
public:
    ConfigParser();
    ~ConfigParser();
    
    // Parse configuration from JSON file
    bool parse_from_file(const std::string& config_path, AppConfig& config_out);
    
    // Parse configuration from JSON string
    bool parse_from_string(const std::string& json_str, AppConfig& config_out);
    
    // Validate configuration
    bool validate_config(const AppConfig& config);
    
    // Get default configuration
    static AppConfig get_default_config();
};

} // namespace trafficgen

