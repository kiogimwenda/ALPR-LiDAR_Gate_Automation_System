// engine_compat_test.cpp — GPU / TensorRT compatibility rules for .plan files.
//
// The supported range is every NVIDIA RTX generation from the 20 series up:
// Turing sm_75, Ampere sm_86, Ada sm_89, Blackwell sm_120.

#include "inference/detail/engine_compat.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using gate::inference::detail::check_device_supported;
using gate::inference::detail::check_engine_compat;
using gate::inference::detail::EngineMeta;
using gate::inference::detail::parse_engine_meta;

namespace {

EngineMeta meta_for(int major, int minor, const char* compat = "none",
                    const char* trt = "10.16.1") {
    EngineMeta m;
    m.cc_major = major;
    m.cc_minor = minor;
    m.gpu = "build-gpu";
    m.tensorrt = trt;
    m.hardware_compat = compat;
    return m;
}

}  // namespace

TEST_CASE("every RTX generation from the 20 series is supported", "[engine_compat]") {
    CHECK_FALSE(check_device_supported("GeForce RTX 2060", 7, 5));
    CHECK_FALSE(check_device_supported("GeForce RTX 3060", 8, 6));
    CHECK_FALSE(check_device_supported("GeForce RTX 4060", 8, 9));
    CHECK_FALSE(check_device_supported("GeForce RTX 5060", 12, 0));
}

TEST_CASE("pre-Turing GPUs are rejected with a clear reason", "[engine_compat]") {
    const auto pascal = check_device_supported("GeForce GTX 1080", 6, 1);
    REQUIRE(pascal);
    CHECK(pascal->find("RTX 20-series") != std::string::npos);
    CHECK(pascal->find("sm_61") != std::string::npos);
    CHECK(check_device_supported("Tesla V100", 7, 0));  // Volta, below 7.5
}

TEST_CASE("meta sidecar parses key=value lines", "[engine_compat]") {
    std::istringstream in{
        "# written by build_engines.sh\r\n"
        "gpu=NVIDIA GeForce RTX 2070\n"
        "compute_capability=7.5\n"
        "tensorrt=10.16.1\n"
        "hardware_compat=none\n"
        "unknown_key=ignored\n"
        "no equals sign\n"};
    const auto m = parse_engine_meta(in);
    CHECK(m.gpu == "NVIDIA GeForce RTX 2070");
    CHECK(m.cc_major == 7);
    CHECK(m.cc_minor == 5);
    CHECK(m.tensorrt == "10.16.1");
    CHECK(m.hardware_compat == "none");
}

TEST_CASE("malformed compute capability leaves the field unset", "[engine_compat]") {
    std::istringstream in{"compute_capability=abc.def\n"};
    const auto m = parse_engine_meta(in);
    CHECK(m.cc_major == -1);
    CHECK(m.cc_minor == -1);
}

TEST_CASE("native engines run only on the GPU generation they were built on", "[engine_compat]") {
    CHECK_FALSE(check_engine_compat(meta_for(7, 5), "RTX 2080", 7, 5, "10.16.1"));
    CHECK_FALSE(check_engine_compat(meta_for(12, 0), "RTX 5090", 12, 0, "10.16.1"));

    const auto moved = check_engine_compat(meta_for(8, 9), "RTX 2060", 7, 5, "10.16.1");
    REQUIRE(moved);
    CHECK(moved->find("sm_89") != std::string::npos);
    CHECK(moved->find("sm_75") != std::string::npos);
    CHECK(moved->find("build_engines.sh") != std::string::npos);

    // same_cc behaves like native for portability purposes
    CHECK(check_engine_compat(meta_for(8, 6, "same_cc"), "RTX 4070", 8, 9, "10.16.1"));
}

TEST_CASE("Ampere-plus engines run on sm_80+ but never on Turing", "[engine_compat]") {
    const auto m = meta_for(8, 9, "ampere_plus");
    CHECK_FALSE(check_engine_compat(m, "RTX 3060", 8, 6, "10.16.1"));
    CHECK_FALSE(check_engine_compat(m, "RTX 5070", 12, 0, "10.16.1"));
    const auto turing = check_engine_compat(m, "RTX 2070", 7, 5, "10.16.1");
    REQUIRE(turing);
    CHECK(turing->find("Ampere-plus") != std::string::npos);
}

TEST_CASE("a TensorRT version change always requires a rebuild", "[engine_compat]") {
    const auto why = check_engine_compat(meta_for(8, 9), "RTX 4060", 8, 9, "10.17.0");
    REQUIRE(why);
    CHECK(why->find("10.16.1") != std::string::npos);
    CHECK(why->find("10.17.0") != std::string::npos);
}

TEST_CASE("a sidecar without GPU info defers to TensorRT", "[engine_compat]") {
    EngineMeta m;
    m.tensorrt = "10.16.1";
    CHECK_FALSE(check_engine_compat(m, "RTX 3080", 8, 6, "10.16.1"));
}
