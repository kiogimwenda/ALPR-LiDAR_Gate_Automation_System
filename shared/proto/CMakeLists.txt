# shared/proto/CMakeLists.txt
#
# Builds the gate_service proto into a single static library (gate_proto)
# that exposes both message classes and gRPC service stubs. Consumed by:
#
#   - server/  (full gRPC++ runtime via vcpkg)
#   - dashboard/backend/  (gRPC client to fusion engine)
#   - simulation/  (drives the same RPC surface programmatically)
#
# The firmware/ tree generates its own lite stubs separately via the
# ESP-IDF protobuf-c component (see firmware/components/gate_proto).

find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC    CONFIG REQUIRED)

set(_proto_files "${CMAKE_CURRENT_SOURCE_DIR}/gate_service.proto")
set(_proto_out   "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_proto_out}")

# protoc invocation for messages
set(_pb_h   "${_proto_out}/gate_service.pb.h")
set(_pb_cc  "${_proto_out}/gate_service.pb.cc")
set(_grpc_h "${_proto_out}/gate_service.grpc.pb.h")
set(_grpc_cc "${_proto_out}/gate_service.grpc.pb.cc")

# Resolve the gRPC C++ plugin path (vcpkg installs it as a target file).
get_target_property(_grpc_cpp_plugin gRPC::grpc_cpp_plugin LOCATION)

add_custom_command(
    OUTPUT  ${_pb_h} ${_pb_cc} ${_grpc_h} ${_grpc_cc}
    COMMAND $<TARGET_FILE:protobuf::protoc>
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --cpp_out=${_proto_out}
            --grpc_out=${_proto_out}
            --plugin=protoc-gen-grpc=${_grpc_cpp_plugin}
            ${_proto_files}
    DEPENDS ${_proto_files} protobuf::protoc gRPC::grpc_cpp_plugin
    COMMENT "Generating C++ stubs from gate_service.proto"
    VERBATIM
)

add_library(gate_proto STATIC
    ${_pb_cc}
    ${_grpc_cc}
)

target_include_directories(gate_proto
    PUBLIC
        $<BUILD_INTERFACE:${_proto_out}>
)

target_link_libraries(gate_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
        gRPC::grpc++_reflection
)

target_compile_features(gate_proto PUBLIC cxx_std_20)

# Generated code is allowed to ignore the strict warnings the project applies
# to first-party code; protoc output is not under our control.
target_compile_options(gate_proto PRIVATE
    -Wno-unused-parameter
    -Wno-deprecated-declarations
)

# Re-export the descriptor for tools that consume it (gRPC reflection,
# dashboard codegen).
add_custom_target(gate_proto_descriptor
    COMMAND $<TARGET_FILE:protobuf::protoc>
            --proto_path=${CMAKE_CURRENT_SOURCE_DIR}
            --descriptor_set_out=${_proto_out}/gate_service.desc
            --include_imports
            --include_source_info
            ${_proto_files}
    BYPRODUCTS ${_proto_out}/gate_service.desc
    DEPENDS    ${_proto_files}
)

add_dependencies(gate_proto gate_proto_descriptor)
