# x64-linux-asan — x64-linux with every dependency built under
# AddressSanitizer (used by the asan preset).
#
# abseil (and protobuf's arenas) poison and unpoison memory by hand
# when compiled with ASan. Mixing an instrumented project with
# uninstrumented abseil lets the linker pick an instrumented inline
# that poisons and an uninstrumented one that never unpoisons, which
# showed up as intermittent use-after-poison deep inside gRPC teardown.
# abseil requires its users and itself to share sanitizer flags.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
set(VCPKG_LINKER_FLAGS "-fsanitize=address")

# Host tools (protoc, grpc_cpp_plugin) come from the uninstrumented
# x64-linux host triplet (CMakePresets.json). vcpkg then cross-compiles
# grpc, which drops grpc++_reflection unless the "codegen" feature is
# requested explicitly (vcpkg.json does).
