#include "core/mempools/mempool_manager.hpp"
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_common.h>
#include <rte_memory.h>
#include <cstring>

namespace trafficgen {

MempoolManager::MempoolManager() : initialized_(false) {
}

MempoolManager::~MempoolManager() {
    // Mempools are managed by DPDK, no explicit cleanup needed
    // but we clear our references
    pools_.clear();
}

bool MempoolManager::initialize(uint32_t nb_mbufs_per_socket,
                                uint32_t mbuf_cache_size,
                                const std::string& name_prefix) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    int max_socket = rte_socket_count();
    if (max_socket <= 0) {
        max_socket = 1;  // Fallback to single socket
    }
    
    pools_.reserve(max_socket);
    
    for (int socket_id = 0; socket_id < max_socket; ++socket_id) {
        std::string pool_name = name_prefix + "_socket" + std::to_string(socket_id);
        
        rte_mempool* pool = create_mempool(socket_id, nb_mbufs_per_socket,
                                          mbuf_cache_size, pool_name);
        
        if (pool == nullptr) {
            // Clean up already created pools
            pools_.clear();
            return false;
        }
        
        pools_.push_back({socket_id, pool, pool_name});
    }
    
    initialized_ = true;
    return true;
}

rte_mempool* MempoolManager::get_mempool(int socket_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& entry : pools_) {
        if (entry.socket_id == socket_id) {
            return entry.pool;
        }
    }
    
    // Fallback to first available pool
    if (!pools_.empty()) {
        return pools_[0].pool;
    }
    
    return nullptr;
}

rte_mempool* MempoolManager::get_local_mempool() {
    int socket_id = rte_socket_id();
    return get_mempool(socket_id);
}

uint16_t MempoolManager::allocate_burst(rte_mempool* pool, rte_mbuf** mbufs, uint16_t count) {
    if (pool == nullptr || mbufs == nullptr || count == 0) {
        return 0;
    }
    
    uint16_t allocated = rte_pktmbuf_alloc_bulk(pool, mbufs, count);
    return allocated;
}

void MempoolManager::free_burst(rte_mbuf** mbufs, uint16_t count) {
    if (mbufs == nullptr || count == 0) {
        return;
    }
    
    for (uint16_t i = 0; i < count; ++i) {
        if (mbufs[i] != nullptr) {
            rte_pktmbuf_free(mbufs[i]);
        }
    }
}

std::vector<MempoolManager::PoolStats> MempoolManager::get_stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PoolStats> stats;
    stats.reserve(pools_.size());
    
    for (const auto& entry : pools_) {
        if (entry.pool != nullptr) {
            PoolStats stat;
            stat.socket_id = entry.socket_id;
            stat.available = rte_mempool_avail_count(entry.pool);
            stat.in_use = rte_mempool_in_use_count(entry.pool);
            stat.total_allocated = rte_mempool_count(entry.pool);
            stats.push_back(stat);
        }
    }
    
    return stats;
}

rte_mempool* MempoolManager::create_mempool(int socket_id,
                                             uint32_t nb_mbufs,
                                             uint32_t cache_size,
                                             const std::string& name) {
    // Create mbuf pool with optimal settings
    rte_mempool* pool = rte_pktmbuf_pool_create_by_ops(
        name.c_str(),
        nb_mbufs,
        cache_size,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        socket_id,
        "ring_mp_mc"
    );
    
    if (pool == nullptr) {
        // Fallback to standard pool creation
        pool = rte_pktmbuf_pool_create(
            name.c_str(),
            nb_mbufs,
            cache_size,
            0,
            RTE_MBUF_DEFAULT_BUF_SIZE,
            socket_id
        );
    }
    
    if (pool == nullptr) {
        // Error creating pool
        return nullptr;
    }
    
    return pool;
}

} // namespace trafficgen

