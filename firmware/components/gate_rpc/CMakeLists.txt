# components/gate_rpc — gRPC client over nanopb + nghttp2 h2c (ADR-011).
#
# Three layers: nanopb-generated messages (src/pb/, committed output of
# scripts/gen-nanopb.sh), the pure gRPC framing codec (host-mirrored
# and unit-tested like gate_state_machine), and the ControlClient's
# nghttp2-over-socket pump.

idf_component_register(
    SRCS
        "src/grpc_framing.cpp"
        "src/command_tracker.cpp"
        "src/control_client.cpp"
        "src/pb/gate_service.pb.c"
        "src/pb/google/protobuf/timestamp.pb.c"
        "src/pb/google/protobuf/duration.pb.c"
        "src/pb/google/protobuf/empty.pb.c"
    INCLUDE_DIRS
        "include"
        # Public on purpose: the ControlClient API traffics in nanopb
        # message structs (TelemetryFiller, send_envelope), so callers
        # need the generated headers too.
        "src/pb"
    REQUIRES
        freertos
        gate_state_machine  # to_wire_state maps sm::State → wire enum
        nanopb              # livekit/nanopb runtime (see idf_component.yml)
    PRIV_REQUIRES
        esp-tls             # TLS transport (Phase 4.10.3)
        esp_timer
        log
        lwip
        nghttp              # espressif/nghttp — nghttp2 port
)
