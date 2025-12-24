#!/usr/bin/env python3
"""
Python Runner for RFC 2544 IMIX Profile
"""

import time
from client import TrafficGeneratorClient

# --- Configuration ---
SERVER_ADDRESS = "localhost:50051"
TARGET_PPS = 1_000_000
TRAFFIC_DURATION_SECONDS = 10

# ======================================================================
# ==  CRITICAL: You must configure these values for your environment  ==
# ======================================================================
# The IP address of the machine that will RECEIVE the traffic.
TARGET_IP = "10.130.39.47" 
# The MAC address of the TARGET_IP machine's network interface.
TARGET_MAC = "02:e1:3e:35:fc:37" # Example: "0A:4A:27:63:31:A4"

# The source IP that will be stamped on the packets. This should be the
# private IP assigned to the DPDK-controlled network interface on your
# traffic generator VM.
SOURCE_IP = "10.130.3.168"
# ======================================================================

# RFC 2544-based IMIX Profile
RFC2544_IMIX_PROFILE = [
    {"packet_size": 64, "percentage": 58.33, "protocol": "udp"},
    {"packet_size": 594, "percentage": 33.33, "protocol": "udp"},
    {"packet_size": 1518, "percentage": 8.34, "protocol": "udp"},
]


def print_final_stats(stats: dict):
    """Prints the final statistics in a formatted way."""
    print("\n" + "="*20 + " Final Statistics " + "="*20)
    
    metrics = stats.get("metrics", {})
    print("\nGlobal Metrics:")
    print(f"  - Total TX Packets: {metrics.get('total_tx_packets', 0):,}")
    print(f"  - Total RX Packets: {metrics.get('total_rx_packets', 0):,}")
    print(f"  - Total TX Bytes:   {metrics.get('total_tx_bytes', 0):,}")
    print(f"  - Total RX Bytes:   {metrics.get('total_rx_bytes', 0):,}")
    print(f"  - Final PPS Rate:   {metrics.get('current_pps', 0):,.2f}")
    print(f"  - Final BPS Rate:   {metrics.get('current_bps', 0):,.2f}")
    print(f"  - Errors:           {metrics.get('errors', 0):,}")
    print(f"  - Uptime:           {stats.get('uptime_seconds', 0):.2f} seconds")

    if "flow_stats" in stats and stats["flow_stats"]:
        print("\nFlow Statistics:")
        for flow_id, flow_stat in stats["flow_stats"].items():
            print(f"  - Flow {flow_id}:")
            print(f"    - TX Packets: {flow_stat.get('tx_packets', 0):,}")
            print(f"    - TX Bytes:   {flow_stat.get('tx_bytes', 0):,}")
            print(f"    - Avg PPS:    {flow_stat.get('avg_pps', 0):,.2f}")
    
    print("\n" + "="*58)


def main():
    """Main function to run the IMIX traffic test."""
    print(f"Connecting to traffic generator at {SERVER_ADDRESS}...")
    try:
        with TrafficGeneratorClient(SERVER_ADDRESS) as client:
            print("Connection successful.")

            print(f"\nConfiguring IMIX profile with target PPS: {TARGET_PPS:,}...")
            result = client.configure_imix(
                entries=RFC2544_IMIX_PROFILE,
                target_pps=TARGET_PPS
            )
            if not result.get("success"):
                print(f"Error configuring IMIX: {result.get('message')}")
                return
            print(f"IMIX configuration successful: {result.get('message')}")

            print(f"\nConfiguring flow to send traffic to {TARGET_IP} (MAC: {TARGET_MAC})...")
            result = client.configure_flows([
                {
                    "flow_id": 1,
                    "src_ip": SOURCE_IP,
                    "dst_ip": TARGET_IP,
                    "dst_mac": TARGET_MAC, # Pass the destination MAC
                    "src_port": 10000,
                    "dst_port": 5201, # Default iperf3 port
                    "protocol": "udp",
                    "pps": TARGET_PPS,
                    "packet_size": 0
                }
            ])
            if not result.get("success"):
                print(f"Error configuring flow: {result.get('message')}")
                return
            print("Flow configured successfully.")

            print("\nStarting traffic...")
            result = client.start_traffic(reset_stats=True)
            if not result.get("success"):
                print(f"Error starting traffic: {result.get('message')}")
                return
            print("Traffic started successfully.")

            print(f"\nMonitoring traffic for {TRAFFIC_DURATION_SECONDS} seconds...")
            for i in range(TRAFFIC_DURATION_SECONDS):
                time.sleep(1)
                try:
                    metrics = client.get_metrics(include_per_core=False)
                    global_metrics = metrics.get("global", {})
                    pps = global_metrics.get("current_pps", 0)
                    tx_packets = global_metrics.get("total_tx_packets", 0)
                    print(f"  [{i + 1:>{len(str(TRAFFIC_DURATION_SECONDS))}}] "
                          f"PPS: {pps:,.2f} | Total TX: {tx_packets:,}")
                except Exception as e:
                    print(f"  An error occurred while getting metrics: {e}")
                    pass

            print("\nStopping traffic...")
            result = client.stop_traffic()
            if not result.get("success"):
                print(f"Error stopping traffic: {result.get('message')}")
            else:
                print("Traffic stopped successfully.")

            print("\nFetching final detailed statistics...")
            stats = client.get_stats(detailed=True)
            print_final_stats(stats)

    except Exception as e:
        print(f"\nAn error occurred: {e}")
        print("Please ensure the traffic generator server is running and accessible.")


if __name__ == "__main__":
    main()
