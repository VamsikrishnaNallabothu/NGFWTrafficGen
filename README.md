# DPDK-based C++ Traffic Generator with gRPC/Python Control Plane

A high-performance, DPDK-based traffic generator implemented in C++ with a gRPC control plane and Python client interface. Supports stateless and stateful traffic generation with IMIX (Internet Mix) profiles, flow-based scheduling, and real-time metrics collection.

## Features

- **High Performance**: DPDK-based zero-copy packet processing
- **NUMA-Aware**: Per-socket memory pool management for optimal performance
- **IMIX Support**: Configurable Internet Mix traffic profiles
- **Flow-Based**: Multiple concurrent flows with rate limiting
- **Rate Control**: Token bucket algorithm for precise rate shaping
- **Statistics**: Per-core and global metrics collection
- **Control Plane**: gRPC API for remote configuration and control
- **Python Client**: Easy-to-use Python interface for experimentation
- **Stateless & Stateful**: Support for UDP, TCP, and ICMP protocols

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    gRPC Control Plane                        │
│              (Python/C++/Other Clients)                      │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│                  Main Process                                │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │ Flow         │  │ IMIX         │  │ Metrics      │      │
│  │ Scheduler    │  │ Engine       │  │ Collector    │      │
│  └──────────────┘  └──────────────┘  └──────────────┘      │
└──────────────────────┬──────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
┌───────▼──────┐ ┌─────▼─────┐ ┌─────▼─────┐
│ Worker Core 0│ │ Worker    │ │ Worker    │
│              │ │ Core 1    │ │ Core N    │
│ - TX Bursts  │ │ ...       │ │ ...       │
│ - RX Process │ │           │ │           │
└───────┬──────┘ └─────┬─────┘ └─────┬─────┘
        │              │              │
        └──────────────┼──────────────┘
                       │
              ┌────────▼────────┐
              │  DPDK NIC       │
              │  Port/Queues    │
              └─────────────────┘
```

## Prerequisites

### System Requirements

- Linux (tested on Ubuntu 20.04+)
- DPDK 20.11 or later
- CMake 3.16+
- C++17 compatible compiler (GCC 7+ or Clang 5+)
- Python 3.7+ (for Python client)
- Hugepages configured

### Dependencies

- **DPDK**: Data Plane Development Kit
- **gRPC**: Google RPC framework
- **Protobuf**: Protocol Buffers
- **libnuma**: NUMA library
- **pthread**: POSIX threads

### Installing Dependencies

#### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libnuma-dev \
    python3-dev \
    python3-pip

# Install DPDK (follow DPDK installation guide)
# Install gRPC and Protobuf
pip3 install grpcio grpcio-tools
```

#### DPDK Setup

1. Download and build DPDK (see DPDK documentation)
2. Configure hugepages:
   ```bash
   echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
   ```
3. Set environment variables:
   ```bash
   export DPDK_ROOT=/path/to/dpdk
   export LD_LIBRARY_PATH=$DPDK_ROOT/lib:$LD_LIBRARY_PATH
   ```

## Building

### Build Steps

1. Clone the repository:
   ```bash
   git clone <repository-url>
   cd NGFW
   ```

2. Create build directory:
   ```bash
   mkdir build && cd build
   ```

3. Configure CMake:
   ```bash
   cmake .. -DDPDK_ROOT=/path/to/dpdk
   ```

4. Build:
   ```bash
   make -j$(nproc)
   ```

5. Generate Python protobuf files (for Python client):
   ```bash
   cd ../control/python_client
   python3 -m grpc_tools.protoc \
       -I../../proto \
       --python_out=. \
       --grpc_python_out=. \
       ../../proto/traffic_generator.proto
   ```

## Configuration

### Configuration File Format

Create a JSON configuration file (example: `config.json`). The built-in parser fully supports this schema (objects, arrays, numbers, booleans, and strings) and applies sensible defaults when fields are omitted:

```json
{
  "hugepage_path": "/mnt/huge",
  "num_cores": 2,
  "mbufs_per_socket": 8192,
  "mbuf_cache_size": 512,
  "tx_burst_size": 32,
  "rx_burst_size": 32,
  "ports": [
    {
      "port_id": 0,
      "nb_rx_queues": 1,
      "nb_tx_queues": 1,
      "nb_rx_desc": 1024,
      "nb_tx_desc": 1024
    }
  ],
  "core_mappings": [
    {
      "core_id": 1,
      "port_id": 0,
      "queue_id": 0,
      "enable_rx": false
    }
  ],
  "grpc_config": {
    "listen_address": "0.0.0.0",
    "port": 50051
  }
}
```

To enable **stateful TCP** receive processing on a worker, set `enable_rx` to `true` for its `core_mappings` entry and configure at least one TCP flow with `stateless=False` in the control plane (see Python example below).

## Running

### Basic Usage

1. Start the traffic generator:
   ```bash
   sudo ./build/NGFWTrafficGenerator \
       --config config.json \
       -- -l 0-3 -n 4 --huge-dir=/mnt/huge
   ```

   DPDK EAL arguments:
   - `-l`: List of cores to use
   - `-n`: Number of memory channels
   - `--huge-dir`: Hugepage directory

2. Use Python client to control:
   ```python
   from control.python_client.client import TrafficGeneratorClient
   
   client = TrafficGeneratorClient("localhost:50051")
   
   # Configure and start traffic
   client.configure_flows([{
       "flow_id": 1,
       "src_ip": "10.0.0.1",
       "dst_ip": "10.0.0.2",
       "src_port": 1000,
       "dst_port": 2000,
       "protocol": "udp",
       "packet_size": 64,
       "pps": 100000
   }])
   
   client.start_traffic()
   
   # Monitor metrics
   metrics = client.get_metrics()
   print(f"Current PPS: {metrics['global']['current_pps']}")
   ```

### Command Line Options

```
Usage: NGFWTrafficGenerator [OPTIONS]

Options:
  -c, --config PATH       Configuration file path
  -l, --log-level LEVEL   Log level (0-7)
  -h, --help              Show this help message

DPDK EAL options can be passed after --
```

## API Usage

### Python Client API

#### Configure IMIX Profile

```python
client.configure_imix([
    {"packet_size": 64, "percentage": 50.0, "protocol": "udp"},
    {"packet_size": 1500, "percentage": 30.0, "protocol": "tcp"},
    {"packet_size": 9000, "percentage": 20.0, "protocol": "tcp"}
], target_pps=1000000)
```

#### Configure Flows

```python
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
```

#### Control Traffic

```python
# Start
client.start_traffic(reset_stats=True)

# Get metrics
metrics = client.get_metrics(include_per_core=True,
                             include_latency_histogram=True)

# Get stats (includes per-flow TX stats when detailed=True)
stats = client.get_stats(detailed=True)

# Stop
client.stop_traffic(drain_queues=True)
```

### gRPC API

The gRPC service definition is in `proto/traffic_generator.proto`. You can generate client stubs for any language supported by gRPC.

## Performance Tuning

### Optimizing Throughput

1. **Core Affinity**: Pin worker threads to specific CPU cores
2. **NUMA Awareness**: Use mempools on the same NUMA node as the NIC
3. **Batch Size**: Adjust `tx_burst_size` based on workload
4. **Queue Depth**: Configure appropriate RX/TX descriptor ring sizes
5. **Hugepages**: Allocate sufficient hugepages for mbuf pools

### Memory Configuration

- Mbuf pool size: Adjust `mbufs_per_socket` based on traffic rate
- Cache size: Set `mbuf_cache_size` to reduce pool lock contention

## Troubleshooting

### Common Issues

1. **DPDK initialization fails**:
   - Check hugepage configuration
   - Verify DPDK_ROOT environment variable
   - Ensure running with appropriate permissions

2. **Port configuration fails**:
   - Verify NIC is bound to DPDK driver (use dpdk-devbind.py)
   - Check queue configuration matches NIC capabilities

3. **Low throughput**:
   - Check CPU affinity settings
   - Verify NUMA locality
   - Increase burst sizes
   - Monitor for packet drops

4. **gRPC connection fails**:
   - Verify server is running
   - Check firewall settings
   - Confirm correct port number

## Architecture Details

### Core Modules

1. **core/mempools**: NUMA-aware mbuf pool management
2. **core/packet**: Packet template building and mutation
3. **core/scheduler**: IMIX engine, token buckets, flow scheduling
4. **core/stats**: Metrics collection and aggregation
5. **core/worker**: Per-core TX/RX processing loops
6. **control/grpc**: gRPC service implementation
7. **util/config**: Configuration parsing

### Design Principles

- **Lock-Free**: Minimize locking in data path
- **Zero-Copy**: Use DPDK mbufs for efficient packet handling
- **Scalable**: Per-core worker threads for parallel processing
- **Modular**: Clean separation of concerns

## Testing

### Unit Tests

```bash
# Run unit tests (when implemented)
cd build
ctest
```

### Integration Tests

Use the Python client to perform integration testing:

```python
# Example integration test
def test_basic_flow():
    client = TrafficGeneratorClient()
    assert client.configure_flows([...]).success
    assert client.start_traffic().success
    time.sleep(5)
    metrics = client.get_metrics()
    assert metrics['global']['total_tx_packets'] > 0
    assert client.stop_traffic().success
```

## Contributing

1. Follow C++17 best practices
2. Maintain DPDK coding guidelines
3. Add unit tests for new features
4. Update documentation

## License

[Specify your license here]

## References

- [DPDK Documentation](https://doc.dpdk.org/)
- [gRPC Documentation](https://grpc.io/docs/)
- [DPDK Traffic Generator Guide](https://doc.dpdk.org/guides/testpmd_app_ug/)

## Support

For issues and questions, please open an issue on the repository.

