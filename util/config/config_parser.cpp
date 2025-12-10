#include "util/config/config_parser.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdlib>

namespace trafficgen {

namespace {

// Helper: find the value substring after a given JSON key.
// Returns empty string on failure.
std::string find_raw_value(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) {
        return {};
    }
    pos = json.find(':', pos);
    if (pos == std::string::npos) {
        return {};
    }
    ++pos; // move past ':'
    // Skip whitespace
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= json.size()) {
        return {};
    }

    // Determine if it's a string, number, bool, or object/array
    if (json[pos] == '"') {
        size_t end = json.find('"', pos + 1);
        while (end != std::string::npos && json[end - 1] == '\\') {
            end = json.find('"', end + 1);
        }
        if (end == std::string::npos) {
            return {};
        }
        return json.substr(pos, end - pos + 1);
    } else if (json[pos] == '{' || json[pos] == '[') {
        char open = json[pos];
        char close = (open == '{') ? '}' : ']';
        int depth = 1;
        size_t i = pos + 1;
        for (; i < json.size(); ++i) {
            if (json[i] == open) depth++;
            else if (json[i] == close) {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (depth != 0) {
            return {};
        }
        return json.substr(pos, i - pos + 1);
    } else {
        size_t end = pos;
        while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']') {
            ++end;
        }
        return json.substr(pos, end - pos);
    }
}

bool parse_string(const std::string& json, const std::string& key, std::string& out) {
    std::string raw = find_raw_value(json, key);
    if (raw.empty() || raw[0] != '"') {
        return false;
    }
    // Strip quotes
    if (raw.size() < 2) return false;
    out = raw.substr(1, raw.size() - 2);
    return true;
}

template <typename T>
bool parse_uint(const std::string& json, const std::string& key, T& out) {
    std::string raw = find_raw_value(json, key);
    if (raw.empty()) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(raw.c_str(), &end, 10);
    if (end == raw.c_str()) return false;
    out = static_cast<T>(v);
    return true;
}

bool parse_bool(const std::string& json, const std::string& key, bool& out) {
    std::string raw = find_raw_value(json, key);
    if (raw.empty()) return false;
    if (raw.find("true") != std::string::npos) {
        out = true;
        return true;
    }
    if (raw.find("false") != std::string::npos) {
        out = false;
        return true;
    }
    return false;
}

// Extract the substring containing the JSON array for a given key.
std::string extract_array(const std::string& json, const std::string& key) {
    std::string raw = find_raw_value(json, key);
    if (raw.empty() || raw[0] != '[') {
        return {};
    }
    return raw;
}

// Iterate objects within a JSON array string.
std::vector<std::string> split_objects_in_array(const std::string& array_str) {
    std::vector<std::string> result;
    size_t pos = 0;
    while (true) {
        pos = array_str.find('{', pos);
        if (pos == std::string::npos) break;
        int depth = 1;
        size_t i = pos + 1;
        for (; i < array_str.size(); ++i) {
            if (array_str[i] == '{') depth++;
            else if (array_str[i] == '}') {
                depth--;
                if (depth == 0) break;
            }
        }
        if (depth != 0) break;
        result.emplace_back(array_str.substr(pos, i - pos + 1));
        pos = i + 1;
    }
    return result;
}

} // namespace

ConfigParser::ConfigParser() {
}

ConfigParser::~ConfigParser() {
}

bool ConfigParser::parse_from_file(const std::string& config_path, AppConfig& config_out) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    return parse_from_string(buffer.str(), config_out);
}

bool ConfigParser::parse_from_string(const std::string& json_str, AppConfig& config_out) {
    // Start from defaults and override with JSON values if present
    AppConfig cfg = get_default_config();

    // Top-level simple fields
    parse_string(json_str, "hugepage_path", cfg.hugepage_path);
    parse_uint(json_str, "num_cores", cfg.num_cores);
    parse_uint(json_str, "mbufs_per_socket", cfg.mbufs_per_socket);
    parse_uint(json_str, "mbuf_cache_size", cfg.mbuf_cache_size);
    parse_uint(json_str, "tx_burst_size", cfg.tx_burst_size);
    parse_uint(json_str, "rx_burst_size", cfg.rx_burst_size);

    // Ports array
    std::string ports_array = extract_array(json_str, "ports");
    if (!ports_array.empty()) {
        cfg.ports.clear();
        auto objects = split_objects_in_array(ports_array);
        for (const auto& obj : objects) {
            AppConfig::PortConfig p{};
            parse_uint(obj, "port_id", p.port_id);
            parse_uint(obj, "nb_rx_queues", p.nb_rx_queues);
            parse_uint(obj, "nb_tx_queues", p.nb_tx_queues);
            parse_uint(obj, "nb_rx_desc", p.nb_rx_desc);
            parse_uint(obj, "nb_tx_desc", p.nb_tx_desc);
            cfg.ports.push_back(p);
        }
    }

    // Core mappings array
    std::string cores_array = extract_array(json_str, "core_mappings");
    if (!cores_array.empty()) {
        cfg.core_mappings.clear();
        auto objects = split_objects_in_array(cores_array);
        for (const auto& obj : objects) {
            AppConfig::CoreMapping m{};
            parse_uint(obj, "core_id", m.core_id);
            parse_uint(obj, "port_id", m.port_id);
            parse_uint(obj, "queue_id", m.queue_id);
            parse_bool(obj, "enable_rx", m.enable_rx);
            cfg.core_mappings.push_back(m);
        }
    }

    // gRPC config object
    std::string grpc_obj = find_raw_value(json_str, "grpc_config");
    if (!grpc_obj.empty() && grpc_obj[0] == '{') {
        parse_string(grpc_obj, "listen_address", cfg.grpc_config.listen_address);
        parse_uint(grpc_obj, "port", cfg.grpc_config.port);
    }

    config_out = cfg;
    return true;
}

bool ConfigParser::validate_config(const AppConfig& config) {
    if (config.num_cores == 0) {
        return false;
    }
    
    if (config.ports.empty()) {
        return false;
    }
    
    if (config.core_mappings.empty()) {
        return false;
    }
    
    // Validate each port config
    for (const auto& port : config.ports) {
        if (port.nb_rx_queues == 0 && port.nb_tx_queues == 0) {
            return false;
        }
        if (port.nb_rx_desc == 0 || port.nb_tx_desc == 0) {
            return false;
        }
    }
    
    // Validate core mappings
    for (const auto& mapping : config.core_mappings) {
        // Check if port exists
        bool port_exists = false;
        for (const auto& port : config.ports) {
            if (port.port_id == mapping.port_id) {
                port_exists = true;
                break;
            }
        }
        if (!port_exists) {
            return false;
        }
    }
    
    return true;
}

AppConfig ConfigParser::get_default_config() {
    AppConfig config;
    
    config.hugepage_path = "/mnt/huge";
    config.num_cores = 2;
    config.mbufs_per_socket = 8192;
    config.mbuf_cache_size = 512;
    config.tx_burst_size = 32;
    config.rx_burst_size = 32;
    
    // Default port configuration
    AppConfig::PortConfig port;
    port.port_id = 0;
    port.nb_rx_queues = 1;
    port.nb_tx_queues = 1;
    port.nb_rx_desc = 1024;
    port.nb_tx_desc = 1024;
    config.ports.push_back(port);
    
    // Default core mapping
    AppConfig::CoreMapping mapping;
    mapping.core_id = 1;
    mapping.port_id = 0;
    mapping.queue_id = 0;
    mapping.enable_rx = false;
    config.core_mappings.push_back(mapping);
    
    // Default gRPC config
    config.grpc_config.listen_address = "0.0.0.0";
    config.grpc_config.port = 50051;
    
    return config;
}

} // namespace trafficgen

