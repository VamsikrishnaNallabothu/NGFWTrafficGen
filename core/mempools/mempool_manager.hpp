#pragma once

#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <vector>
#include <memory>
#include <string>
#include <mutex>

namespace trafficgen {

/**
 * Manages DPDK mbuf pools per NUMA socket
 * Provides NUMA-aware memory allocation for packet buffers
 */
class MempoolManager {
public:
    MempoolManager();
    ~MempoolManager();
    
    // Initialize mempools for all NUMA sockets
    bool initialize(uint32_t nb_mbufs_per_socket = 8192,
                    uint32_t mbuf_cache_size = 512,
                    const std::string& name_prefix = "mbuf_pool");
    
    // Get mempool for a specific NUMA socket
    rte_mempool* get_mempool(int socket_id);
    
    // Get mempool for current core's NUMA socket
    rte_mempool* get_local_mempool();
    
    // Allocate a burst of mbufs
    uint16_t allocate_burst(rte_mempool* pool, rte_mbuf** mbufs, uint16_t count);
    
    // Free a burst of mbufs
    void free_burst(rte_mbuf** mbufs, uint16_t count);
    
    // Get pool statistics
    struct PoolStats {
        uint32_t socket_id;
        uint32_t available;
        uint32_t in_use;
        uint64_t total_allocated;
    };
    
    std::vector<PoolStats> get_stats() const;
    
private:
    struct PoolEntry {
        int socket_id;
        rte_mempool* pool;
        std::string name;
    };
    
    std::vector<PoolEntry> pools_;
    std::mutex mutex_;
    bool initialized_;
    
    // Create a mempool for a specific socket
    rte_mempool* create_mempool(int socket_id, 
                                 uint32_t nb_mbufs,
                                 uint32_t cache_size,
                                 const std::string& name);
};

} // namespace trafficgen

