#include "control/grpc/server.hpp"
#include "core/packet/packet_builder.hpp"
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <iostream>

namespace trafficgen {

TrafficGeneratorServiceImpl::TrafficGeneratorServiceImpl(
    std::shared_ptr<FlowScheduler> flow_scheduler,
    std::shared_ptr<IMIXEngine> imix_engine,
    std::shared_ptr<MetricsCollector> metrics_collector)
    : flow_scheduler_(flow_scheduler),
      imix_engine_(imix_engine),
      metrics_collector_(metrics_collector) {
}

TrafficGeneratorServiceImpl::~TrafficGeneratorServiceImpl() {
}

grpc::Status TrafficGeneratorServiceImpl::ConfigureIMIX(
    grpc::ServerContext* context,
    const IMIXConfig* request,
    StatusResponse* response) {
    
    (void)context;
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!imix_engine_) {
        response->set_success(false);
        response->set_message("IMIX engine not initialized");
        response->set_error_code(-1);
        return grpc::Status::OK;
    }
    
    std::vector<IMIXEntryInternal> entries;
    for (const auto& entry : request->entries()) {
        entries.push_back(convert_proto_imix_entry(entry));
    }
    
    if (imix_engine_->configure(entries, request->target_pps())) {
        response->set_success(true);
        response->set_message("IMIX configured successfully");
        response->set_error_code(0);
    } else {
        response->set_success(false);
        response->set_message("Failed to configure IMIX");
        response->set_error_code(-1);
    }
    
    return grpc::Status::OK;
}

grpc::Status TrafficGeneratorServiceImpl::ConfigureFlows(
    grpc::ServerContext* context,
    const FlowConfig* request,
    StatusResponse* response) {
    
    (void)context;
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!flow_scheduler_) {
        response->set_success(false);
        response->set_message("Flow scheduler not initialized");
        response->set_error_code(-1);
        return grpc::Status::OK;
    }
    
    bool all_success = true;
    std::string error_message;
    
    for (const auto& proto_flow : request->flows()) {
        FlowConfigInternal internal_config;
        internal_config.flow_id = proto_flow.flow_id();
        internal_config.flow_key = convert_proto_flow_key(proto_flow);
        internal_config.packet_size = proto_flow.packet_size();
        internal_config.protocol = proto_flow.protocol();
        internal_config.pps = proto_flow.pps();
        internal_config.duration_seconds = proto_flow.duration_seconds();
        internal_config.stateless = request->stateless();
        // Pass the destination MAC from the proto message
        internal_config.dst_mac = proto_flow.dst_mac();

        if (!flow_scheduler_->add_flow(internal_config)) {
            all_success = false;
            error_message += "Failed to add flow " + std::to_string(proto_flow.flow_id()) + "; ";
        }
    }
    
    if (all_success) {
        response->set_success(true);
        response->set_message("All flows configured successfully");
        response->set_error_code(0);
    } else {
        response->set_success(false);
        response->set_message("Some flows failed to configure: " + error_message);
        response->set_error_code(-1);
    }
    
    return grpc::Status::OK;
}

grpc::Status TrafficGeneratorServiceImpl::StartTraffic(
    grpc::ServerContext* context,
    const StartRequest* request,
    StatusResponse* response) {
    
    (void)context;
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (request->reset_stats() && metrics_collector_) {
        metrics_collector_->reset_all();
    }
    
    if (flow_scheduler_) {
        auto all_flows = flow_scheduler_->get_all_flows_snapshot();
        if (all_flows.empty()) {
            response->set_success(false);
            response->set_message("No flows configured to start");
            response->set_error_code(-2);
            return grpc::Status::OK;
        }

        for (const auto& pair : all_flows) {
            flow_scheduler_->start_flow(pair.first);
        }
    }
    
    if (metrics_collector_) {
        metrics_collector_->set_running(true);
    }
    
    response->set_success(true);
    response->set_message("Traffic generation started");
    response->set_error_code(0);
    
    return grpc::Status::OK;
}

grpc::Status TrafficGeneratorServiceImpl::StopTraffic(
    grpc::ServerContext* context,
    const StopRequest* request,
    StatusResponse* response) {
    
    (void)context;
    (void)request;
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (flow_scheduler_) {
        auto active_flows = flow_scheduler_->get_active_flows();
        for (uint32_t flow_id : active_flows) {
            flow_scheduler_->stop_flow(flow_id);
        }
    }
    
    if (metrics_collector_) {
        metrics_collector_->set_running(false);
    }
    
    response->set_success(true);
    response->set_message("Traffic generation stopped");
    response->set_error_code(0);
    
    return grpc::Status::OK;
}

grpc::Status TrafficGeneratorServiceImpl::GetMetrics(
    grpc::ServerContext* context,
    const MetricsRequest* request,
    MetricsResponse* response) {
    
    (void)context;

    if (!metrics_collector_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Metrics collector not initialized");
    }
    
    CoreStatsSnapshot global_stats = metrics_collector_->get_global_stats();

    GlobalMetrics* global = response->mutable_global();
    
    global->set_total_tx_packets(global_stats.tx_packets);
    global->set_total_rx_packets(global_stats.rx_packets);
    global->set_total_tx_bytes(global_stats.tx_bytes);
    global->set_total_rx_bytes(global_stats.rx_bytes);
    global->set_current_pps(metrics_collector_->calculate_global_pps());
    global->set_current_bps(metrics_collector_->calculate_global_bps());
    global->set_avg_latency_us(metrics_collector_->get_average_latency_us());
    global->set_errors(global_stats.errors);
    
    if (request->include_per_core()) {
        auto per_core = metrics_collector_->get_all_core_stats();
        for (size_t i = 0; i < per_core.size(); ++i) {
            CoreMetrics* core_metrics = response->add_per_core();
            core_metrics->set_core_id(static_cast<uint32_t>(i));
            core_metrics->set_tx_packets(per_core[i].tx_packets);
            core_metrics->set_rx_packets(per_core[i].rx_packets);
            core_metrics->set_tx_bytes(per_core[i].tx_bytes);
            core_metrics->set_rx_bytes(per_core[i].rx_bytes);
            core_metrics->set_pps(metrics_collector_->calculate_current_pps(i));
        }
    }

    if (request->include_latency_histogram()) {
        auto buckets = metrics_collector_->get_latency_histogram();
        LatencyHistogram* hist = response->mutable_latency();
        for (const auto& b : buckets) {
            LatencyBucket* bucket = hist->add_buckets();
            bucket->set_min_us(b.min_us);
            bucket->set_max_us(b.max_us);
            bucket->set_count(b.count);
        }
    }
    
    return grpc::Status::OK;
}

grpc::Status TrafficGeneratorServiceImpl::GetStats(
    grpc::ServerContext* context,
    const StatsRequest* request,
    StatsResponse* response) {
    
    (void)context;

    if (!metrics_collector_) {
        return grpc::Status(grpc::StatusCode::INTERNAL, "Metrics collector not initialized");
    }
    
    CoreStatsSnapshot global_stats = metrics_collector_->get_global_stats();
    GlobalMetrics* global = response->mutable_metrics();
    
    global->set_total_tx_packets(global_stats.tx_packets);
    global->set_total_rx_packets(global_stats.rx_packets);
    global->set_total_tx_bytes(global_stats.tx_bytes);
    global->set_total_rx_bytes(global_stats.rx_bytes);
    global->set_current_pps(metrics_collector_->calculate_global_pps());
    global->set_current_bps(metrics_collector_->calculate_global_bps());
    global->set_errors(global_stats.errors);
    
    response->set_uptime_seconds(metrics_collector_->get_uptime_seconds());
    
    if (request->detailed() && flow_scheduler_) {
        auto flows = flow_scheduler_->get_all_flows_snapshot();
        for (const auto& kv : flows) {
            const FlowSnapshot& cfg = kv.second;
            FlowStats& fs = (*response->mutable_flow_stats())[cfg.flow_id];
            fs.set_flow_id(cfg.flow_id);
            fs.set_tx_packets(cfg.packets_sent);
            fs.set_tx_bytes(cfg.bytes_sent);

            double uptime = static_cast<double>(metrics_collector_->get_uptime_seconds());
            double avg_pps = 0.0;
            if (uptime > 0.0) {
                avg_pps = static_cast<double>(cfg.packets_sent) / uptime;
            }
            fs.set_avg_pps(avg_pps);
        }
    }
    
    return grpc::Status::OK;
}

FlowKey TrafficGeneratorServiceImpl::convert_proto_flow_key(const Flow& flow) {
    FlowKey key;
    
    PacketBuilder builder;
    key.src_ip = builder.ip_to_uint32(flow.src_ip());
    key.dst_ip = builder.ip_to_uint32(flow.dst_ip());
    key.src_port = static_cast<uint16_t>(flow.src_port());
    key.dst_port = static_cast<uint16_t>(flow.dst_port());
    
    if (flow.protocol() == "tcp") {
        key.protocol = IPPROTO_TCP;
    } else if (flow.protocol() == "udp") {
        key.protocol = IPPROTO_UDP;
    } else if (flow.protocol() == "icmp") {
        key.protocol = IPPROTO_ICMP;
    } else {
        key.protocol = IPPROTO_UDP;
    }
    
    return key;
}

IMIXEntryInternal TrafficGeneratorServiceImpl::convert_proto_imix_entry(const IMIXEntry& entry) {
    IMIXEntryInternal imix_entry;
    imix_entry.packet_size = entry.packet_size();
    imix_entry.percentage = entry.percentage();
    imix_entry.protocol = entry.protocol();
    imix_entry.enable_checksum = entry.enable_checksum();
    return imix_entry;
}

// GRPCServer implementation
GRPCServer::GRPCServer() : running_(false) {
}

GRPCServer::~GRPCServer() {
    stop();
}

bool GRPCServer::initialize(
    const std::string& listen_address,
    uint16_t port,
    std::shared_ptr<FlowScheduler> flow_scheduler,
    std::shared_ptr<IMIXEngine> imix_engine,
    std::shared_ptr<MetricsCollector> metrics_collector) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::stringstream ss;
    ss << listen_address << ":" << port;
    server_address_ = ss.str();
    
    service_ = std::make_unique<TrafficGeneratorServiceImpl>(
        flow_scheduler, imix_engine, metrics_collector);
    
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address_, grpc::InsecureServerCredentials());
    builder.RegisterService(service_.get());
    
    server_ = builder.BuildAndStart();
    
    return server_ != nullptr;
}

bool GRPCServer::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (server_) {
        running_ = true;
        return true;
    }
    
    return false;
}

void GRPCServer::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (server_) {
        server_->Shutdown();
        running_ = false;
    }
}

void GRPCServer::wait() {
    if (server_) {
        server_->Wait();
    }
}

bool GRPCServer::is_running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

} // namespace trafficgen
