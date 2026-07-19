// grpc_framing.cpp — incremental gRPC frame assembly.

#include "gate_rpc/grpc_framing.hpp"

namespace gate::rpc {

std::array<std::uint8_t, kFrameHeaderSize> make_frame_header(std::uint32_t payload_len) noexcept {
    return {
        0U,  // compressed flag — always uncompressed (ADR-011)
        static_cast<std::uint8_t>((payload_len >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((payload_len >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((payload_len >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(payload_len & 0xFFU),
    };
}

FrameAssembler::Status FrameAssembler::push(const std::uint8_t* data, std::size_t len,
                                            std::size_t& consumed) noexcept {
    consumed = 0;
    if (poisoned_) {
        return Status::ErrorOversize;  // sticky until reset()
    }

    // A previous MessageReady result is only valid until the next
    // push(), so a fresh call restarts header collection implicitly:
    // in_payload_/header_filled_ were cleared when the message
    // completed.

    while (consumed < len) {
        if (!in_payload_) {
            header_[header_filled_++] = data[consumed++];
            if (header_filled_ < kFrameHeaderSize) {
                continue;
            }
            header_filled_ = 0;
            if (header_[0] != 0U) {
                poisoned_ = true;
                return Status::ErrorCompressed;
            }
            msg_len_ = (static_cast<std::size_t>(header_[1]) << 24U) |
                       (static_cast<std::size_t>(header_[2]) << 16U) |
                       (static_cast<std::size_t>(header_[3]) << 8U) |
                       static_cast<std::size_t>(header_[4]);
            if (msg_len_ > cap_) {
                poisoned_ = true;
                return Status::ErrorOversize;
            }
            msg_filled_ = 0;
            in_payload_ = true;
            if (msg_len_ == 0) {  // legal: empty protobuf message
                in_payload_ = false;
                return Status::MessageReady;
            }
            continue;
        }

        const std::size_t want = msg_len_ - msg_filled_;
        const std::size_t avail = len - consumed;
        const std::size_t take = (avail < want) ? avail : want;
        for (std::size_t i = 0; i < take; ++i) {
            buf_[msg_filled_ + i] = data[consumed + i];
        }
        msg_filled_ += take;
        consumed += take;
        if (msg_filled_ == msg_len_) {
            in_payload_ = false;
            return Status::MessageReady;
        }
    }
    return Status::NeedMore;
}

void FrameAssembler::reset() noexcept {
    header_filled_ = 0;
    msg_len_ = 0;
    msg_filled_ = 0;
    in_payload_ = false;
    poisoned_ = false;
}

}  // namespace gate::rpc
