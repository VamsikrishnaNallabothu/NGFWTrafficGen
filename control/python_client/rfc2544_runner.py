#!/usr/bin/env python3
"""
Python Runner for RFC 2544 IMIX Profile

This script uses the TrafficGeneratorClient to configure and run a
standard IMIX (Internet Mix) traffic profile, often used for performance
testing and based on the frame sizes specified in RFC 2544.

The IMIX profile used here consists of:
- 7 packets of 64 bytes (~58.33%)
- 4 packets of 594 bytes (~33.33%)
- 1 packet of 1518 bytes (~8.33%)
"""

import time
from client import TrafficGeneratorClient

# --- Configuration ---
SERVER_ADDRESS = "localhost:50051"
TARGET_PPS = 1_000_000  # Target 1 Million packets per second
TRAFFIC_DURATION_SECONDS = 10  # How long to run traffic for monitoring

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

    if "per_core" in stats and stats["per_core"]:
        print("\nPer-Core Statistics:")
        for core_stat in stats["per_core"]:
            print(f"  - Core {core_stat['core_id']}:")
            print(f"    - TX Packets: {core_stat.get('tx_packets', 0):,}")
            print(f"    - RX Packets: {core_stat.get('rx_packets', 0):,}")
            print(f"    - TX Bytes:   {core_stat.get('tx_bytes', 0):,}")
            print(f"    - RX Bytes:   {core_stat.get('rx_bytes', 0):,}")
            print(f"    - PPS:        {core_stat.get('pps', 0):,.2f}")

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

            # 1. Configure the IMIX profile
            print(f"\nConfiguring IMIX profile with target PPS: {TARGET_PPS:,}...")
            result = client.configure_imix(
                entries=RFC2544_IMIX_PROFILE,
                target_pps=TARGET_PPS
            )
            if not result.get("success"):
                print(f"Error configuring IMIX: {result.get('message')}")
                return
            print(f"IMIX configuration successful: {result.get('message')}")

            # 2. Start traffic generation
            print("\nStarting traffic...")
            result = client.start_traffic(reset_stats=True)
            if not result.get("success"):
                print(f"Error starting traffic: {result.get('message')}")
                return
            print("Traffic started successfully.")

            # 3. Monitor metrics for a specified duration
            print(f"\nMonitoring traffic for {TRAFFIC_DURATION_SECONDS} seconds...")
            for i in range(TRAFFIC_DURATION_SECONDS):
                time.sleep(1)
                try:
                    metrics = client.get_metrics(include_per_core=True)
                    global_metrics = metrics.get("global", {})
                    pps = global_metrics.get("current_pps", 0)
                    bps = global_metrics.get("current_bps", 0)
                    tx_packets = global_metrics.get("total_tx_packets", 0)
                    avg_latency = global_metrics.get("avg_latency_us", 0)
                    print(f"  [{i + 1:>{len(str(TRAFFIC_DURATION_SECONDS))}}] "
                          f"PPS: {pps:,.2f} | BPS: {bps:,.2f} | "
                          f"Avg Latency: {avg_latency:.2f} us | "
                          f"Total TX: {tx_packets:,}")
                except Exception as e:
                    print(f"  An error occurred while getting metrics: {e}")
                    break

            # 4. Stop traffic generation
            print("\nStopping traffic...")
            result = client.stop_traffic()
            if not result.get("success"):
                print(f"Error stopping traffic: {result.get('message')}")
            else:
                print("Traffic stopped successfully.")

            # 5. Get final detailed stats
            print("\nFetching final detailed statistics...")
            stats = client.get_stats(detailed=True)
            print_final_stats(stats)

    except Exception as e:
        print(f"\nAn error occurred: {e}")
        print("Please ensure the traffic generator server is running and accessible.")


if __name__ == "__main__":
    main()
