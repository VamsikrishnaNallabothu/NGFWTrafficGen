cmake_minimum_required(VERSION 3.16)
project(NGFWTrafficGenerator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find required packages
find_package(DPDK REQUIRED)
find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

# DPDK configuration
set(DPDK_LIB_DIR "${DPDK_ROOT}/lib")
set(DPDK_INCLUDE_DIR "${DPDK_ROOT}/include")

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DPDK_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/proto
)

# gRPC and Protobuf
set(PROTO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILES "${PROTO_PATH}/traffic_generator.proto")

# Set output directory for generated proto files
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto")

# Generate protobuf and gRPC files
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES} OUTPUT_DIR ${PROTO_OUT_DIR})
grpc_generate_cpp(GRPC_SRCS GRPC_HDRS ${PROTO_FILES} OUTPUT_DIR ${PROTO_OUT_DIR})

# Source files
set(CORE_SOURCES
    core/mempools/mempool_manager.cpp
    core/packet/packet_builder.cpp
    core/packet/packet_mutator.cpp
    core/stateful/tcp_state.cpp
    core/scheduler/imix_engine.cpp
    core/scheduler/token_bucket.cpp
    core/scheduler/flow_scheduler.cpp
    core/stats/metrics_collector.cpp
    core/worker/worker.cpp
)

set(CONTROL_SOURCES
    control/grpc/server.cpp
    util/config/config_parser.cpp
)

set(SOURCES
    main.cpp
    ${CORE_SOURCES}
    ${CONTROL_SOURCES}
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${DPDK_LIBRARIES}
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
    numa
    pthread
    dl
)

# Compiler flags
target_compile_options(${PROJECT_NAME} PRIVATE
    -march=native
    -O3
    -Wall
    -Wextra
    -Wno-deprecated-declarations
)

# DPDK specific flags
target_compile_definitions(${PROJECT_NAME} PRIVATE
    RTE_MACHINE_CPUFLAG_SSE
    RTE_MACHINE_CPUFLAG_SSE2
    RTE_MACHINE_CPUFLAG_SSE3
    RTE_MACHINE_CPUFLAG_SSSE3
    RTE_MACHINE_CPUFLAG_SSE4_1
    RTE_MACHINE_CPUFLAG_SSE4_2
)

# Install targets
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

