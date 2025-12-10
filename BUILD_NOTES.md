# Build Notes and Known Issues

## Important Notes

### Proto File Generation

The protobuf and gRPC files need to be generated before compilation. The CMakeLists.txt is configured to generate them automatically during the build process.

**Generated files will be in:** `${CMAKE_BINARY_DIR}/proto/`

**Include path:** The generated headers should be included as:
```cpp
#include "traffic_generator.grpc.pb.h"
#include "traffic_generator.pb.h"
```

### Namespace Considerations

1. **Proto Namespace**: The proto package is `trafficgen`, so generated classes are in the `trafficgen` namespace.

2. **FlowConfig Name Conflict**: 
   - Proto message: `trafficgen::FlowConfig` (contains repeated Flow)
   - Internal struct: `trafficgen::FlowConfig` (in core/scheduler/flow_scheduler.hpp)
   
   **Resolution**: In `control/grpc/server.cpp`, we use fully qualified name `::trafficgen::FlowConfig` when referring to the internal struct to avoid ambiguity.

### Dependencies

#### Required System Libraries:
- DPDK (20.11+)
- gRPC
- Protobuf
- libnuma
- pthread

#### CMake Configuration:

Make sure DPDK_ROOT is set:
```bash
cmake .. -DDPDK_ROOT=/path/to/dpdk
```

### Compilation Flags

The project uses:
- C++17 standard
- Optimizations: `-O3 -march=native`
- DPDK-specific flags for SSE instructions

### Common Compilation Issues

1. **Proto files not found**:
   - Ensure CMake successfully generated proto files
   - Check that `${CMAKE_BINARY_DIR}/proto` contains `.pb.h` and `.grpc.pb.h` files

2. **DPDK not found**:
   - Set DPDK_ROOT environment variable
   - Or pass `-DDPDK_ROOT=/path/to/dpdk` to cmake

3. **Missing includes**:
   - All core modules should include their respective headers
   - Proto-generated headers must be in the include path

4. **Link errors**:
   - Ensure all DPDK libraries are linked
   - Check LD_LIBRARY_PATH includes DPDK lib directory

### Runtime Requirements

1. **Hugepages**: Must be configured before running
   ```bash
   echo 1024 | sudo tee /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
   ```

2. **NIC Binding**: Network interfaces must be bound to DPDK driver
   ```bash
   dpdk-devbind.py --bind=vfio-pci 0000:01:00.0
   ```

3. **Permissions**: May need root/sudo for DPDK operations

### Code Structure Notes

1. **Worker Threads**: Each worker is pinned to a specific CPU core
2. **NUMA Awareness**: Mempools are created per NUMA socket
3. **Thread Safety**: Statistics use atomic operations for lock-free updates

### Python Client

The Python client requires generated proto files. Generate them with:
```bash
cd control/python_client
python3 -m grpc_tools.protoc \
    -I../../proto \
    --python_out=. \
    --grpc_python_out=. \
    ../../proto/traffic_generator.proto
```

### Testing

Before full deployment:
1. Test DPDK initialization
2. Test port configuration
3. Test gRPC server startup
4. Test Python client connection
5. Test basic flow configuration
6. Verify packet transmission

### Known Limitations

1. Some error handling paths are still conservative and may abort configuration on minor issues
2. Retransmission and full TCP congestion control are not implemented (stateful TCP is simplified)

### Future Improvements

1. Add comprehensive unit tests
2. Enhance error reporting and logging
3. Add Prometheus metrics export
4. Implement advanced TCP features (retransmission, congestion control, window scaling)

