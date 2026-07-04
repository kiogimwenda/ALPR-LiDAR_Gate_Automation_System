// grpc_framing.hpp — gRPC length-prefixed message framing (pure C++20).
//
// gRPC wraps every protobuf message on an HTTP/2 stream in a 5-byte
// prefix: 1 byte compressed-flag + 4 bytes big-endian payload length.
// This module implements exactly that layer and nothing else — no
// sockets, no HTTP/2, no protobuf — so the byte-level protocol logic
// is host-unit-testable (tests/rpc_framing/) with the same mirror
// pattern as gate_state_machine.
//
// The assembler is allocation-free: the caller supplies the payload
// buffer (sized to the largest message it expects — for the Control
// stream that is a beam-search over the nanopb .options sizes, in
// practice OtaChunk's 4 KiB data field plus slack). DATA frames from
// nghttp2 arrive in arbitrary chunk sizes with no alignment to message
// boundaries, so push() is incremental and re-entrant.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gate::rpc {

constexpr std::size_t kFrameHeaderSize = 5;

// Build the 5-byte prefix for an uncompressed message of `payload_len`
// bytes. gRPC's compressed flag is always 0 for us — we never
// negotiate a compression codec (ADR-011).
std::array<std::uint8_t, kFrameHeaderSize> make_frame_header(std::uint32_t payload_len) noexcept;

class FrameAssembler {
public:
    enum class Status : std::uint8_t {
        NeedMore = 0,         // consumed everything offered; no full message yet
        MessageReady = 1,     // a payload is available via message()/message_size()
        ErrorCompressed = 2,  // peer set the compressed flag — we never negotiate one
        ErrorOversize = 3,    // declared length exceeds the caller's buffer
    };

    // `buffer` receives reassembled payloads and must outlive the
    // assembler. Capacity bounds the largest accepted message.
    FrameAssembler(std::uint8_t* buffer, std::size_t capacity) noexcept
        : buf_(buffer), cap_(capacity) {}

    // Consume up to `len` bytes; `consumed` reports how many were
    // eaten. Stops early when a message completes, so the caller loops
    //   while (…) { status = push(p + off, n - off, c); off += c; … }
    // and handles each MessageReady before feeding the remainder.
    // After an Error* status the assembler is poisoned until reset();
    // the connection should be torn down anyway — both errors are
    // protocol violations on a stream we configured.
    Status push(const std::uint8_t* data, std::size_t len, std::size_t& consumed) noexcept;

    // Valid after MessageReady, until the next push() or reset().
    [[nodiscard]] const std::uint8_t* message() const noexcept { return buf_; }
    [[nodiscard]] std::size_t message_size() const noexcept { return msg_len_; }

    void reset() noexcept;

private:
    std::uint8_t* buf_;
    std::size_t cap_;
    std::array<std::uint8_t, kFrameHeaderSize> header_{};
    std::size_t header_filled_ = 0;
    std::size_t msg_len_ = 0;     // declared payload length once header parsed
    std::size_t msg_filled_ = 0;  // payload bytes received so far
    bool in_payload_ = false;
    bool poisoned_ = false;
};

}  // namespace gate::rpc
