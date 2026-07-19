// rpc_framing_test.cpp — gRPC length-prefixed framing codec.
//
// The FrameAssembler sits between nghttp2 DATA chunks (arbitrary
// sizes, no message alignment) and nanopb decode (whole messages), so
// the tests hammer exactly that boundary: split headers, split
// payloads, multiple messages per chunk, byte-at-a-time delivery, and
// the two protocol-violation paths.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>
#include <cstdint>
#include <vector>

#include "gate_rpc/grpc_framing.hpp"

namespace rpc = gate::rpc;
using Status = rpc::FrameAssembler::Status;

namespace {

// Frame a payload the way the wire carries it: 5-byte prefix + bytes.
std::vector<std::uint8_t> frame(const std::vector<std::uint8_t>& payload) {
    const auto hdr = rpc::make_frame_header(static_cast<std::uint32_t>(payload.size()));
    std::vector<std::uint8_t> out(hdr.begin(), hdr.end());
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

// Feed `wire` to the assembler in chunks of `chunk` bytes; collect
// every completed message.
std::vector<std::vector<std::uint8_t>> pump(rpc::FrameAssembler& fa,
                                            const std::vector<std::uint8_t>& wire,
                                            std::size_t chunk) {
    std::vector<std::vector<std::uint8_t>> messages;
    std::size_t off = 0;
    while (off < wire.size()) {
        const std::size_t n = std::min(chunk, wire.size() - off);
        std::size_t inner = 0;
        while (inner < n) {
            std::size_t consumed = 0;
            const auto st = fa.push(wire.data() + off + inner, n - inner, consumed);
            inner += consumed;
            REQUIRE((st == Status::NeedMore || st == Status::MessageReady));
            if (st == Status::MessageReady) {
                messages.emplace_back(fa.message(), fa.message() + fa.message_size());
            }
        }
        off += n;
    }
    return messages;
}

}  // namespace

TEST_CASE("frame header encodes length big-endian with clear flag") {
    const auto hdr = rpc::make_frame_header(0x01020304U);
    REQUIRE(hdr[0] == 0);  // uncompressed
    REQUIRE(hdr[1] == 0x01);
    REQUIRE(hdr[2] == 0x02);
    REQUIRE(hdr[3] == 0x03);
    REQUIRE(hdr[4] == 0x04);
}

TEST_CASE("single message in a single chunk") {
    std::array<std::uint8_t, 64> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const std::vector<std::uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    const auto msgs = pump(fa, frame(payload), 1024);
    REQUIRE(msgs.size() == 1);
    CHECK_THAT(msgs[0], Catch::Matchers::Equals(payload));
}

TEST_CASE("empty message is legal and completes on the header alone") {
    std::array<std::uint8_t, 8> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const auto msgs = pump(fa, frame({}), 1024);
    REQUIRE(msgs.size() == 1);
    CHECK(msgs[0].empty());
}

TEST_CASE("byte-at-a-time delivery reassembles correctly") {
    std::array<std::uint8_t, 64> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const std::vector<std::uint8_t> payload = {1, 2, 3, 4, 5, 6, 7};
    const auto msgs = pump(fa, frame(payload), 1);
    REQUIRE(msgs.size() == 1);
    CHECK_THAT(msgs[0], Catch::Matchers::Equals(payload));
}

TEST_CASE("multiple messages packed into one chunk all surface") {
    std::array<std::uint8_t, 64> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const std::vector<std::uint8_t> a = {0x11};
    const std::vector<std::uint8_t> b = {0x22, 0x22};
    const std::vector<std::uint8_t> c = {0x33, 0x33, 0x33};
    auto wire = frame(a);
    const auto wb = frame(b);
    const auto wc = frame(c);
    wire.insert(wire.end(), wb.begin(), wb.end());
    wire.insert(wire.end(), wc.begin(), wc.end());

    const auto msgs = pump(fa, wire, 1024);
    REQUIRE(msgs.size() == 3);
    CHECK_THAT(msgs[0], Catch::Matchers::Equals(a));
    CHECK_THAT(msgs[1], Catch::Matchers::Equals(b));
    CHECK_THAT(msgs[2], Catch::Matchers::Equals(c));
}

TEST_CASE("chunk boundary inside the 5-byte header") {
    std::array<std::uint8_t, 64> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const std::vector<std::uint8_t> payload = {9, 8, 7, 6};
    // 3-byte chunks put a boundary after header bytes [0,1,2].
    const auto msgs = pump(fa, frame(payload), 3);
    REQUIRE(msgs.size() == 1);
    CHECK_THAT(msgs[0], Catch::Matchers::Equals(payload));
}

TEST_CASE("compressed flag is a protocol error and poisons the assembler") {
    std::array<std::uint8_t, 64> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const std::vector<std::uint8_t> wire = {1, 0, 0, 0, 1, 0xAA};  // flag=1
    std::size_t consumed = 0;
    REQUIRE(fa.push(wire.data(), wire.size(), consumed) == Status::ErrorCompressed);

    // Poisoned until reset(): even clean bytes are rejected.
    const auto good = frame({0x42});
    REQUIRE(fa.push(good.data(), good.size(), consumed) == Status::ErrorOversize);

    fa.reset();
    std::size_t c2 = 0;
    REQUIRE(fa.push(good.data(), good.size(), c2) == Status::MessageReady);
    REQUIRE(fa.message_size() == 1);
    CHECK(fa.message()[0] == 0x42);
}

TEST_CASE("declared length beyond capacity is rejected before buffering") {
    std::array<std::uint8_t, 16> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    const auto hdr = rpc::make_frame_header(17);  // capacity is 16
    std::size_t consumed = 0;
    REQUIRE(fa.push(hdr.data(), hdr.size(), consumed) == Status::ErrorOversize);
}

TEST_CASE("assembler is reusable across many messages") {
    std::array<std::uint8_t, 32> buf{};
    rpc::FrameAssembler fa(buf.data(), buf.size());
    for (std::uint8_t round = 0; round < 20; ++round) {
        const std::vector<std::uint8_t> payload(round, round);  // varying sizes incl. 0
        const auto msgs = pump(fa, frame(payload), 4);
        REQUIRE(msgs.size() == 1);
        CHECK_THAT(msgs[0], Catch::Matchers::Equals(payload));
    }
}
