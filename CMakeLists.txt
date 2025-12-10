cmake_minimum_required(VERSION 3.16)
project(NGFWTrafficGenerator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Build gRPC from Source using FetchContent ---
include(FetchContent)

set(gRPC_INSTALL OFF)
set(gRPC_BUILD_TESTS OFF)
set(gRPC_ABSL_PROVIDER "module")
set(gRPC_CARES_PROVIDER "module")
set(gRPC_PROTOBUF_PROVIDER "module")
set(gRPC_RE2_PROVIDER "module")
set(gRPC_SSL_PROVIDER "package")
set(gRPC_ZLIB_PROVIDER "package")

FetchContent_Declare(
  gRPC
  GIT_REPOSITORY https://github.com/grpc/grpc.git
  GIT_TAG        v1.54.2
)

FetchContent_MakeAvailable(gRPC)
# --- End of gRPC Build ---


# --- Find System-Provided Dependencies ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# Find NUMA manually to bypass the problematic pkg-config environment.
find_path(NUMA_INCLUDE_DIR numa.h)
find_library(NUMA_LIBRARY NAMES numa)
if(NOT NUMA_INCLUDE_DIR OR NOT NUMA_LIBRARY)
    message(FATAL_ERROR "Could not find NUMA library or headers. Please ensure libnuma-dev is installed.")
endif()

find_package(Threads REQUIRED)
# --- End of System Dependencies ---


# --- Protobuf and gRPC Code Generation ---
# When building with FetchContent, the targets do not have the gRPC:: namespace.
# We use the plain target names provided by the sub-build.
set(PROTO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILES "${PROTO_PATH}/traffic_generator.proto")
set(PROTO_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/proto")

# Ensure the output directory for generated files exists before we try to use it.
file(MAKE_DIRECTORY ${PROTO_OUT_DIR})

add_custom_command(
    OUTPUT  "${PROTO_OUT_DIR}/traffic_generator.pb.cc"
            "${PROTO_OUT_DIR}/traffic_generator.pb.h"
            "${PROTO_OUT_DIR}/traffic_generator.grpc.pb.cc"
            "${PROTO_OUT_DIR}/traffic_generator.grpc.pb.h"
    COMMAND $<TARGET_FILE:protoc>
            --proto_path=${PROTO_PATH}
            --cpp_out=${PROTO_OUT_DIR}
            --grpc_out=${PROTO_OUT_DIR}
            --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
            ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating Protobuf and gRPC C++ sources"
)

set(PROTO_SRCS "${PROTO_OUT_DIR}/traffic_generator.pb.cc")
set(GRPC_SRCS "${PROTO_OUT_DIR}/traffic_generator.grpc.pb.cc")
# --- End of Code Generation ---

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

# Add all necessary include directories for the target.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DPDK_INCLUDE_DIRS}
    ${NUMA_INCLUDE_DIR} # Use the path from the manual find
    ${PROTO_OUT_DIR}    # Add directory for generated headers
)

# Link libraries using the variables and imported targets.
# When using FetchContent, the main library target is just 'grpc++'.
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${DPDK_LIBRARIES}
    ${NUMA_LIBRARY} # Use the library from the manual find
    grpc++
    Threads::Threads
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

# Install target
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
