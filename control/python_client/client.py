#!/usr/bin/env python3
"""
Python client for DPDK Traffic Generator Control Plane

This module provides a Python interface to control and configure
the DPDK-based traffic generator via gRPC.

Example usage:
    from client import TrafficGeneratorClient
    
    client = TrafficGeneratorClient(server_address="localhost:50051")
    
    # Configure IMIX profile
    client.configure_imix([
        {"packet_size": 64, "percentage": 50.0, "protocol": "udp"},
        {"packet_size": 1500, "percentage": 30.0, "protocol": "tcp"},
        {"packet_size": 9000, "percentage": 20.0, "protocol": "tcp"}
    ], target_pps=1000000)
    
    # Configure flows
    client.configure_flows([
        {
            "flow_id": 1,
            "src_ip": "10.0.0.1",
            "dst_ip": "10.0.0.2",
            "src_port": 1000,
            "dst_port": 2000,
            "protocol": "udp",
            "packet_size": 64,
            "pps": 100000,
            "duration_seconds": 60
        }
    ])
    
    # Start traffic
    client.start_traffic(reset_stats=True)
    
    # Get metrics
    metrics = client.get_metrics()
    print(f"Current PPS: {metrics['global']['current_pps']}")
    
    # Stop traffic
    client.stop_traffic()
"""

import grpc
from typing import List, Dict, Optional, Any
import sys
import os

# Add proto generated directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../proto'))

try:
    import traffic_generator_pb2
    import traffic_generator_pb2_grpc
except ImportError:
    print("Error: Protobuf generated files not found. Please generate them first:")
    print("  python -m grpc_tools.protoc -I../../proto --python_out=. --grpc_python_out=. ../../proto/traffic_generator.proto")
    sys.exit(1)


class TrafficGeneratorClient:
    """Client for interacting with DPDK Traffic Generator via gRPC"""
    
    def __init__(self, server_address: str = "localhost:50051"):
        """
        Initialize the traffic generator client.
        
        Args:
            server_address: gRPC server address (host:port)
        """
        self.server_address = server_address
        self.channel = grpc.insecure_channel(server_address)
        self.stub = traffic_generator_pb2_grpc.TrafficGeneratorServiceStub(self.channel)
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()
    
    def close(self):
        """Close the gRPC channel"""
        if self.channel:
            self.channel.close()
    
    def configure_imix(self, entries: List[Dict[str, Any]], 
                      target_pps: int = 1000000,
                      duration_seconds: int = 0) -> Dict[str, Any]:
        """
        Configure IMIX (Internet Mix) traffic profile.
        
        Args:
            entries: List of IMIX entries, each with:
                - packet_size: Packet size in bytes
                - percentage: Percentage (0.0-100.0)
                - protocol: "tcp", "udp", or "icmp"
                - enable_checksum: Whether to enable checksums
            target_pps: Target packets per second
            duration_seconds: Duration in seconds (0 = infinite)
        
        Returns:
            Response dictionary with success status and message
        """
        request = traffic_generator_pb2.IMIXConfig()
        request.target_pps = target_pps
        request.duration_seconds = duration_seconds
        
        for entry in entries:
            imix_entry = request.entries.add()
            imix_entry.packet_size = entry.get("packet_size", 64)
            imix_entry.percentage = entry.get("percentage", 100.0)
            imix_entry.protocol = entry.get("protocol", "udp")
            imix_entry.enable_checksum = entry.get("enable_checksum", True)
        
        response = self.stub.ConfigureIMIX(request)
        
        return {
            "success": response.success,
            "message": response.message,
            "error_code": response.error_code
        }
    
    def configure_flows(self, flows: List[Dict[str, Any]], 
                       stateless: bool = True) -> Dict[str, Any]:
        """
        Configure traffic flows.
        
        Args:
            flows: List of flow configurations, each with:
                - flow_id: Unique flow identifier
                - src_ip: Source IP address
                - dst_ip: Destination IP address
                - src_port: Source port
                - dst_port: Destination port
                - protocol: "tcp", "udp", or "icmp"
                - packet_size: Packet size in bytes
                - pps: Packets per second
                - duration_seconds: Duration in seconds (0 = infinite)
            stateless: Whether flows are stateless (True) or stateful (False)
        
        Returns:
            Response dictionary with success status and message
        """
        request = traffic_generator_pb2.FlowConfig()
        request.stateless = stateless
        
        for flow in flows:
            flow_entry = request.flows.add()
            flow_entry.flow_id = flow.get("flow_id", 0)
            flow_entry.src_ip = flow.get("src_ip", "0.0.0.0")
            flow_entry.dst_ip = flow.get("dst_ip", "0.0.0.0")
            flow_entry.src_port = flow.get("src_port", 0)
            flow_entry.dst_port = flow.get("dst_port", 0)
            flow_entry.protocol = flow.get("protocol", "udp")
            flow_entry.packet_size = flow.get("packet_size", 64)
            flow_entry.pps = flow.get("pps", 1000)
            flow_entry.duration_seconds = flow.get("duration_seconds", 0)
        
        response = self.stub.ConfigureFlows(request)
        
        return {
            "success": response.success,
            "message": response.message,
            "error_code": response.error_code
        }
    
    def start_traffic(self, reset_stats: bool = True) -> Dict[str, Any]:
        """
        Start traffic generation.
        
        Args:
            reset_stats: Whether to reset statistics before starting
        
        Returns:
            Response dictionary with success status and message
        """
        request = traffic_generator_pb2.StartRequest()
        request.reset_stats = reset_stats
        
        response = self.stub.StartTraffic(request)
        
        return {
            "success": response.success,
            "message": response.message,
            "error_code": response.error_code
        }
    
    def stop_traffic(self, drain_queues: bool = True, 
                    timeout_seconds: int = 5) -> Dict[str, Any]:
        """
        Stop traffic generation.
        
        Args:
            drain_queues: Whether to wait for queues to drain
            timeout_seconds: Timeout for draining
        
        Returns:
            Response dictionary with success status and message
        """
        request = traffic_generator_pb2.StopRequest()
        request.drain_queues = drain_queues
        request.timeout_seconds = timeout_seconds
        
        response = self.stub.StopTraffic(request)
        
        return {
            "success": response.success,
            "message": response.message,
            "error_code": response.error_code
        }
    
    def get_metrics(self, include_per_core: bool = True,
                   include_latency_histogram: bool = False) -> Dict[str, Any]:
        """
        Get real-time metrics.
        
        Args:
            include_per_core: Include per-core metrics
            include_latency_histogram: Include latency histogram
        
        Returns:
            Dictionary containing metrics
        """
        request = traffic_generator_pb2.MetricsRequest()
        request.include_per_core = include_per_core
        request.include_latency_histogram = include_latency_histogram
        
        response = self.stub.GetMetrics(request)
        
        metrics = {
            "global": {
                "total_tx_packets": response.global.total_tx_packets,
                "total_rx_packets": response.global.total_rx_packets,
                "total_tx_bytes": response.global.total_tx_bytes,
                "total_rx_bytes": response.global.total_rx_bytes,
                "current_pps": response.global.current_pps,
                "current_bps": response.global.current_bps,
                "avg_latency_us": response.global.avg_latency_us,
                "errors": response.global.errors
            }
        }
        
        if include_per_core and response.per_core:
            metrics["per_core"] = []
            for core in response.per_core:
                metrics["per_core"].append({
                    "core_id": core.core_id,
                    "tx_packets": core.tx_packets,
                    "rx_packets": core.rx_packets,
                    "tx_bytes": core.tx_bytes,
                    "rx_bytes": core.rx_bytes,
                    "pps": core.pps
                })
        
        return metrics
    
    def get_stats(self, detailed: bool = False) -> Dict[str, Any]:
        """
        Get detailed statistics.
        
        Args:
            detailed: Include detailed flow statistics
        
        Returns:
            Dictionary containing statistics
        """
        request = traffic_generator_pb2.StatsRequest()
        request.detailed = detailed
        
        response = self.stub.GetStats(request)
        
        stats = {
            "metrics": {
                "total_tx_packets": response.metrics.total_tx_packets,
                "total_rx_packets": response.metrics.total_rx_packets,
                "total_tx_bytes": response.metrics.total_tx_bytes,
                "total_rx_bytes": response.metrics.total_rx_bytes,
                "current_pps": response.metrics.current_pps,
                "current_bps": response.metrics.current_bps,
                "errors": response.metrics.errors
            },
            "uptime_seconds": response.uptime_seconds
        }
        
        if detailed and response.flow_stats:
            stats["flow_stats"] = {}
            for flow_id, flow_stat in response.flow_stats.items():
                stats["flow_stats"][flow_id] = {
                    "flow_id": flow_stat.flow_id,
                    "tx_packets": flow_stat.tx_packets,
                    "tx_bytes": flow_stat.tx_bytes,
                    "avg_pps": flow_stat.avg_pps
                }
        
        return stats


def main():
    """Example usage of the client"""
    import time
    
    # Create client
    with TrafficGeneratorClient("localhost:50051") as client:
        print("Connected to traffic generator")
        
        # Configure IMIX
        print("\nConfiguring IMIX...")
        result = client.configure_imix([
            {"packet_size": 64, "percentage": 50.0, "protocol": "udp"},
            {"packet_size": 1500, "percentage": 30.0, "protocol": "tcp"},
            {"packet_size": 9000, "percentage": 20.0, "protocol": "tcp"}
        ], target_pps=1000000)
        print(f"Result: {result}")
        
        # Configure flows
        print("\nConfiguring flows...")
        result = client.configure_flows([
            {
                "flow_id": 1,
                "src_ip": "10.0.0.1",
                "dst_ip": "10.0.0.2",
                "src_port": 1000,
                "dst_port": 2000,
                "protocol": "udp",
                "packet_size": 64,
                "pps": 100000,
                "duration_seconds": 60
            }
        ])
        print(f"Result: {result}")
        
        # Start traffic
        print("\nStarting traffic...")
        result = client.start_traffic(reset_stats=True)
        print(f"Result: {result}")
        
        # Monitor metrics for a few seconds
        print("\nMonitoring metrics...")
        for i in range(5):
            time.sleep(1)
            metrics = client.get_metrics()
            print(f"  [{i+1}] PPS: {metrics['global']['current_pps']:.2f}, "
                  f"TX Packets: {metrics['global']['total_tx_packets']}")
        
        # Stop traffic
        print("\nStopping traffic...")
        result = client.stop_traffic()
        print(f"Result: {result}")


if __name__ == "__main__":
    main()

