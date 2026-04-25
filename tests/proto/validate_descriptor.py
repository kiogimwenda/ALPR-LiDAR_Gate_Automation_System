#!/usr/bin/env python3
"""
validate_descriptor.py — protoc-only schema validation for gate_service.proto.

Runs as part of CI even when the full C++ toolchain (vcpkg / gRPC / Catch2)
is not bootstrapped. Exercises:

  1. Proto compiles to a clean descriptor.
  2. Every message we expect downstream to consume is present.
  3. Every enum has the documented stable field numbers.
  4. The two long-lived RPCs (Control, DeliverOta, Subscribe) are streaming.
  5. ControlEnvelope is a oneof with the documented payload variants.

Exits non-zero on any contract violation.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

from google.protobuf import descriptor_pb2

REPO_ROOT  = Path(__file__).resolve().parents[2]
PROTO_DIR  = REPO_ROOT / "shared" / "proto"
PROTO_FILE = PROTO_DIR / "gate_service.proto"

EXPECTED_MESSAGES = {
    "BoundingBox", "Point3D", "BoundingBox3D",
    "PlateDetection", "VehicleDetection", "DetectionFrame",
    "AuthorizeRequest", "AuthDecision",
    "Telemetry", "GateCommand", "CommandAck",
    "FaultEvent",
    "OtaManifest", "OtaChunk", "OtaProgress",
    "DashboardEvent", "DashboardSubscription",
    "TimeWindow", "Allowlistentry",
    "UpsertAllowlistRequest", "UpsertAllowlistResponse",
    "ListAllowlistRequest",   "ListAllowlistResponse",
    "DeleteAllowlistRequest",
    "ControlEnvelope",
}

EXPECTED_ENUMS = {
    "VehicleClass": {
        "VEHICLE_CLASS_UNKNOWN":     0,
        "VEHICLE_CLASS_PEDESTRIAN":  1,
        "VEHICLE_CLASS_BICYCLE":     2,
        "VEHICLE_CLASS_MOTORCYCLE":  3,
        "VEHICLE_CLASS_SEDAN":       4,
        "VEHICLE_CLASS_SUV":         5,
        "VEHICLE_CLASS_PICKUP":      6,
        "VEHICLE_CLASS_VAN":         7,
        "VEHICLE_CLASS_TRUCK":       8,
    },
    "GateState": {
        "GATE_STATE_UNKNOWN":  0,
        "GATE_STATE_CLOSED":   1,
        "GATE_STATE_OPENING":  2,
        "GATE_STATE_OPEN":     3,
        "GATE_STATE_CLOSING":  4,
        "GATE_STATE_FAULT":    5,
        "GATE_STATE_LOCKDOWN": 6,
    },
    "AuthVerdict": {
        "AUTH_VERDICT_UNSPECIFIED":    0,
        "AUTH_VERDICT_AUTHORIZED":     1,
        "AUTH_VERDICT_DENIED":         2,
        "AUTH_VERDICT_LOW_CONFIDENCE": 3,
        "AUTH_VERDICT_MANUAL_REVIEW":  4,
    },
}

EXPECTED_SERVICES = {
    "FieldControllerService": {
        "Control":           ("client streaming", "server streaming"),
        "DeliverOta":        ("unary",            "server streaming"),
        "ReportOtaProgress": ("client streaming", "unary"),
        "SubmitDetection":   ("unary",            "unary"),
    },
    "DashboardService": {
        "Subscribe":    ("unary", "server streaming"),
        "IssueCommand": ("unary", "unary"),
        "Authorize":    ("unary", "unary"),
    },
    "AdminService": {
        "UpsertAllowlist": ("unary", "unary"),
        "ListAllowlist":   ("unary", "unary"),
        "DeleteAllowlist": ("unary", "unary"),
    },
}


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def compile_descriptor() -> descriptor_pb2.FileDescriptorSet:
    with tempfile.NamedTemporaryFile(suffix=".desc") as desc:
        rc = subprocess.run(
            [
                "protoc",
                f"--proto_path={PROTO_DIR}",
                f"--descriptor_set_out={desc.name}",
                "--include_imports",
                str(PROTO_FILE),
            ],
            capture_output=True,
            text=True,
        )
        if rc.returncode != 0:
            fail(f"protoc failed: {rc.stderr}")
        fds = descriptor_pb2.FileDescriptorSet()
        fds.ParseFromString(Path(desc.name).read_bytes())
        return fds


def find_proto_file(fds: descriptor_pb2.FileDescriptorSet) -> descriptor_pb2.FileDescriptorProto:
    for f in fds.file:
        if f.name == "gate_service.proto":
            return f
    fail("gate_service.proto not present in descriptor set")
    return descriptor_pb2.FileDescriptorProto()  # unreachable


def check_messages(proto: descriptor_pb2.FileDescriptorProto) -> None:
    found = {m.name for m in proto.message_type}
    missing = EXPECTED_MESSAGES - found
    if missing:
        fail(f"missing expected messages: {sorted(missing)}")
    print(f"OK: all {len(EXPECTED_MESSAGES)} expected messages present")


def check_enums(proto: descriptor_pb2.FileDescriptorProto) -> None:
    enums_by_name = {e.name: e for e in proto.enum_type}
    for enum_name, expected_values in EXPECTED_ENUMS.items():
        if enum_name not in enums_by_name:
            fail(f"enum {enum_name} not found")
        e = enums_by_name[enum_name]
        actual = {v.name: v.number for v in e.value}
        for name, number in expected_values.items():
            if name not in actual:
                fail(f"{enum_name}.{name} missing")
            if actual[name] != number:
                fail(f"{enum_name}.{name} has number {actual[name]}, expected {number}")
        print(f"OK: enum {enum_name} has {len(expected_values)} stable values")


def streaming_kind(method: descriptor_pb2.MethodDescriptorProto) -> tuple[str, str]:
    client = "client streaming" if method.client_streaming else "unary"
    server = "server streaming" if method.server_streaming else "unary"
    return client, server


def check_services(proto: descriptor_pb2.FileDescriptorProto) -> None:
    services = {s.name: s for s in proto.service}
    for svc_name, expected_methods in EXPECTED_SERVICES.items():
        if svc_name not in services:
            fail(f"service {svc_name} not found")
        svc = services[svc_name]
        method_kinds = {m.name: streaming_kind(m) for m in svc.method}
        for method, (c_exp, s_exp) in expected_methods.items():
            if method not in method_kinds:
                fail(f"{svc_name}.{method} not found")
            c_act, s_act = method_kinds[method]
            if (c_act, s_act) != (c_exp, s_exp):
                fail(f"{svc_name}.{method} streaming mismatch: got ({c_act}, {s_act}), expected ({c_exp}, {s_exp})")
        print(f"OK: service {svc_name} has {len(expected_methods)} methods with correct streaming kinds")


def check_control_envelope(proto: descriptor_pb2.FileDescriptorProto) -> None:
    env = next((m for m in proto.message_type if m.name == "ControlEnvelope"), None)
    if env is None:
        fail("ControlEnvelope message missing")
    if len(env.oneof_decl) != 1:
        fail(f"ControlEnvelope must have exactly one oneof, found {len(env.oneof_decl)}")
    payload = env.oneof_decl[0]
    if payload.name != "payload":
        fail(f"ControlEnvelope oneof must be named 'payload', got '{payload.name}'")
    expected_fields = {"telemetry", "ack", "fault", "command"}
    actual_fields = {f.name for f in env.field if f.HasField("oneof_index")}
    if actual_fields != expected_fields:
        fail(f"ControlEnvelope payload fields mismatch: expected {expected_fields}, got {actual_fields}")
    print(f"OK: ControlEnvelope oneof has {len(expected_fields)} expected payloads")


def check_package(proto: descriptor_pb2.FileDescriptorProto) -> None:
    if proto.package != "gate.v1":
        fail(f"package must be gate.v1, got {proto.package}")
    if proto.syntax != "proto3":
        fail(f"syntax must be proto3, got {proto.syntax}")
    print("OK: package=gate.v1, syntax=proto3")


def main() -> None:
    print(f"Validating {PROTO_FILE.relative_to(REPO_ROOT)}")
    fds = compile_descriptor()
    proto = find_proto_file(fds)
    check_package(proto)
    check_messages(proto)
    check_enums(proto)
    check_services(proto)
    check_control_envelope(proto)
    print("\nAll proto contract checks passed.")


if __name__ == "__main__":
    main()
