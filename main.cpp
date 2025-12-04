#include <iostream>
#include <vector>
#include <memory>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_lcore.h>

#include "core/mempools/mempool_manager.hpp"
#include "core/scheduler/flow_scheduler.hpp"
#include "core/scheduler/imix_engine.hpp"
#include "core/stats/metrics_collector.hpp"
#include "core/worker/worker.hpp"
#include "control/grpc/server.hpp"
#include "util/config/config_parser.hpp"

// 'trafficgen' is a namespace defined throughout the traffic generator project.
// Its main definition and usage are found in headers and implementation files
// such as 'control/grpc/server.hpp', 'core/scheduler/flow_scheduler.hpp', and related files.
// It contains the main classes, types, and logic for the DPDK-based traffic generator and its gRPC server/API.
// By writing 'using namespace trafficgen;', all symbols inside this namespace can be used directly in this file.

// Global variables for cleanup
std::atomic<bool> g_running{true};
std::vector<std::unique_ptr<Worker>> g_workers;
std::unique_ptr<GRPCServer> g_grpc_server;
std::shared_ptr<MempoolManager> g_mempool_manager;
std::shared_ptr<FlowScheduler> g_flow_scheduler;
std::shared_ptr<IMIXEngine> g_imix_engine;
std::shared_ptr<MetricsCollector> g_metrics_collector;

// Signal handler for graceful shutdown
void signal_handler(int signum) {
    std::cout << "\nReceived signal " << signum << ", shutting down...\n";
    g_running.store(false);
    
    if (g_grpc_server) {
        g_grpc_server->stop();
    }
    
    for (auto& worker : g_workers) {
        if (worker) {
            worker->stop();
        }
    }
}

// Print usage
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -c, --config PATH       Configuration file path\n"
              << "  -l, --log-level LEVEL   Log level (0-7)\n"
              << "  -h, --help              Show this help message\n"
              << "\n"
              << "DPDK EAL options can be passed after --\n"
              << "Example: " << program_name << " -- -l 0-3 -n 4 --huge-dir=/mnt/huge\n";
}

// Initialize DPDK EAL
bool initialize_dpdk(int argc, char* argv[]) {
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        std::cerr << "Error: Failed to initialize DPDK EAL\n";
        return false;
    }
    
    int lcore_count = rte_lcore_count();
    std::cout << "DPDK initialized successfully. Available cores: " << lcore_count << "\n";
    
    return true;
}

// Probe and configure network ports
bool configure_ports(const AppConfig& config) {
    uint16_t nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0) {
        std::cerr << "Error: No Ethernet ports found\n";
        return false;
    }
    
    std::cout << "Found " << nb_ports << " Ethernet port(s)\n";
    
    // Configure each port
    for (const auto& port_config : config.ports) {
        if (port_config.port_id >= nb_ports) {
            std::cerr << "Warning: Port " << port_config.port_id 
                      << " exceeds available ports (" << nb_ports << ")\n";
            continue;
        }
        
        struct rte_eth_conf port_conf;
        memset(&port_conf, 0, sizeof(port_conf));
        
        // Configure device
        int ret = rte_eth_dev_configure(
            port_config.port_id,
            port_config.nb_rx_queues,
            port_config.nb_tx_queues,
            &port_conf);
        
        if (ret < 0) {
            std::cerr << "Error: Failed to configure port " << port_config.port_id << "\n";
            return false;
        }
        
        // Configure RX queues
        for (uint16_t q = 0; q < port_config.nb_rx_queues; ++q) {
            ret = rte_eth_rx_queue_setup(
                port_config.port_id,
                q,
                port_config.nb_rx_desc,
                rte_eth_dev_socket_id(port_config.port_id),
                NULL,
                g_mempool_manager->get_mempool(rte_eth_dev_socket_id(port_config.port_id)));
            
            if (ret < 0) {
                std::cerr << "Error: Failed to setup RX queue " << q 
                          << " on port " << port_config.port_id << "\n";
                return false;
            }
        }
        
        // Configure TX queues
        for (uint16_t q = 0; q < port_config.nb_tx_queues; ++q) {
            ret = rte_eth_tx_queue_setup(
                port_config.port_id,
                q,
                port_config.nb_tx_desc,
                rte_eth_dev_socket_id(port_config.port_id),
                NULL);
            
            if (ret < 0) {
                std::cerr << "Error: Failed to setup TX queue " << q 
                          << " on port " << port_config.port_id << "\n";
                return false;
            }
        }
        
        // Start device
        ret = rte_eth_dev_start(port_config.port_id);
        if (ret < 0) {
            std::cerr << "Error: Failed to start port " << port_config.port_id << "\n";
            return false;
        }
        
        // Display MAC address
        struct rte_ether_addr addr;
        ret = rte_eth_macaddr_get(port_config.port_id, &addr);
        if (ret == 0) {
            std::printf("Port %u MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                       port_config.port_id,
                       addr.addr_bytes[0], addr.addr_bytes[1],
                       addr.addr_bytes[2], addr.addr_bytes[3],
                       addr.addr_bytes[4], addr.addr_bytes[5]);
        }
        
        std::cout << "Port " << port_config.port_id << " configured successfully\n";
    }
    
    return true;
}

// Launch worker threads
bool launch_workers(const AppConfig& config) {
    g_workers.reserve(config.core_mappings.size());
    
    for (const auto& mapping : config.core_mappings) {
        // Verify core is available
        if (!rte_lcore_is_enabled(mapping.core_id)) {
            std::cerr << "Warning: Core " << mapping.core_id << " is not enabled\n";
            continue;
        }
        
        Worker::Config worker_config;
        worker_config.core_id = mapping.core_id;
        worker_config.port_id = mapping.port_id;
        worker_config.queue_id = mapping.queue_id;
        worker_config.tx_burst_size = config.tx_burst_size;
        worker_config.rx_burst_size = config.rx_burst_size;
        worker_config.enable_rx = mapping.enable_rx;
        worker_config.mempool_manager = g_mempool_manager;
        worker_config.flow_scheduler = g_flow_scheduler;
        worker_config.imix_engine = g_imix_engine;
        worker_config.metrics_collector = g_metrics_collector;
        
        auto worker = std::make_unique<Worker>(worker_config);
        if (!worker->start()) {
            std::cerr << "Error: Failed to start worker on core " << mapping.core_id << "\n";
            return false;
        }
        
        g_workers.push_back(std::move(worker));
        std::cout << "Worker started on core " << mapping.core_id 
                  << " (port " << mapping.port_id 
                  << ", queue " << mapping.queue_id << ")\n";
    }
    
    return true;
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    static struct option long_options[] = {
        {"config", required_argument, 0, 'c'},
        {"log-level", required_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    std::string config_path;
    int log_level = 7;
    
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "c:l:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'c':
                config_path = optarg;
                break;
            case 'l':
                log_level = atoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize DPDK EAL
    if (!initialize_dpdk(argc, argv)) {
        return 1;
    }
    
    // Parse configuration
    AppConfig config;
    if (!config_path.empty()) {
        ConfigParser parser;
        if (!parser.parse_from_file(config_path, config)) {
            std::cerr << "Warning: Failed to parse config file, using defaults\n";
            config = ConfigParser::get_default_config();
        }
    } else {
        config = ConfigParser::get_default_config();
    }
    
    // Validate configuration
    ConfigParser parser;
    if (!parser.validate_config(config)) {
        std::cerr << "Error: Invalid configuration\n";
        return 1;
    }
    
    // Initialize shared components
    g_mempool_manager = std::make_shared<MempoolManager>();
    if (!g_mempool_manager->initialize(config.mbufs_per_socket, 
                                       config.mbuf_cache_size)) {
        std::cerr << "Error: Failed to initialize mempool manager\n";
        return 1;
    }
    
    g_flow_scheduler = std::make_shared<FlowScheduler>();
    if (!g_flow_scheduler->initialize()) {
        std::cerr << "Error: Failed to initialize flow scheduler\n";
        return 1;
    }
    
    g_imix_engine = std::make_shared<IMIXEngine>();
    
    g_metrics_collector = std::make_shared<MetricsCollector>();
    uint32_t num_cores = static_cast<uint32_t>(config.core_mappings.size());
    if (!g_metrics_collector->initialize(num_cores)) {
        std::cerr << "Error: Failed to initialize metrics collector\n";
        return 1;
    }
    
    // Configure network ports
    if (!configure_ports(config)) {
        return 1;
    }
    
    // Initialize and start gRPC server (on main thread or separate thread)
    g_grpc_server = std::make_unique<GRPCServer>();
    if (!g_grpc_server->initialize(config.grpc_config.listen_address,
                                   config.grpc_config.port,
                                   g_flow_scheduler,
                                   g_imix_engine,
                                   g_metrics_collector)) {
        std::cerr << "Warning: Failed to initialize gRPC server\n";
    } else {
        if (!g_grpc_server->start()) {
            std::cerr << "Warning: Failed to start gRPC server\n";
        } else {
            std::cout << "gRPC server started on " 
                      << config.grpc_config.listen_address 
                      << ":" << config.grpc_config.port << "\n";
        }
    }
    
    // Launch worker threads
    if (!launch_workers(config)) {
        std::cerr << "Error: Failed to launch workers\n";
        return 1;
    }
    
    std::cout << "\nTraffic generator started successfully!\n";
    std::cout << "Use gRPC API to configure and control traffic generation.\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
    // Main loop - wait for signal
    while (g_running.load()) {
        sleep(1);
        
        // Optional: Print periodic statistics
        // This can be enabled for debugging/monitoring
    }
    
    // Cleanup
    std::cout << "Shutting down...\n";
    
    // Stop gRPC server
    if (g_grpc_server) {
        g_grpc_server->stop();
    }
    
    // Stop all workers
    for (auto& worker : g_workers) {
        if (worker) {
            worker->stop();
        }
    }
    
    // Wait for workers to finish
    for (auto& worker : g_workers) {
        if (worker) {
            worker->join();
        }
    }
    
    // Stop all ports
    for (const auto& port_config : config.ports) {
        rte_eth_dev_stop(port_config.port_id);
    }
    
    std::cout << "Shutdown complete.\n";
    
    return 0;
}

