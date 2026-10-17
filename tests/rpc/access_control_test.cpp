// access_control_test.cpp — CN → role mapping and the enforcement switch.

#include "rpc/access_control.hpp"

#include <catch2/catch_test_macros.hpp>
#include <grpcpp/server_context.h>

using gate::rpc::AccessPolicy;
using gate::rpc::identity_for_common_name;
using gate::rpc::Role;

TEST_CASE("certificate CNs map to roles", "[rpc][access]") {
    CHECK(identity_for_common_name("gate-dashboard").role == Role::kDashboard);
    CHECK(identity_for_common_name("gate-vision").role == Role::kVision);
    CHECK(identity_for_common_name("gate-vision-lane2").role == Role::kVision);

    const auto shared = identity_for_common_name("gate-field-client");
    CHECK(shared.role == Role::kField);
    CHECK(shared.gate_id.empty());  // legacy shared leaf: any gate

    const auto bound = identity_for_common_name("gate-field-gate-01");
    CHECK(bound.role == Role::kField);
    CHECK(bound.gate_id == "gate-01");
}

TEST_CASE("unknown or look-alike CNs get no role", "[rpc][access]") {
    for (const char* cn : {"", "gate-server", "gate-site-ca", "gate-field-", "gate-visionary",
                           "gate-vision-", "gate-dashboard2", "Gate-Dashboard", "admin"}) {
        INFO(cn);
        CHECK(identity_for_common_name(cn).role == Role::kNone);
    }
}

TEST_CASE("an unenforced policy admits everyone (dev postures)", "[rpc][access]") {
    AccessPolicy p;
    grpc::ServerContext ctx;
    CHECK(p.check(&ctx, Role::kDashboard, "AdminService.UpsertAllowlist").ok());
    CHECK(p.check_gate(identity_for_common_name("gate-field-gate-01"), "gate-02").ok());
}

TEST_CASE("an enforced policy denies callers without a client certificate", "[rpc][access]") {
    AccessPolicy p;
    p.set_enforced(true);
    grpc::ServerContext ctx;  // no TLS peer
    const auto s = p.check(&ctx, Role::kField | Role::kDashboard, "DashboardService.IssueCommand");
    CHECK(s.error_code() == grpc::StatusCode::PERMISSION_DENIED);
}

TEST_CASE("gate-bound identities may act only for their own gate", "[rpc][access]") {
    AccessPolicy p;
    p.set_enforced(true);
    const auto bound = identity_for_common_name("gate-field-gate-01");
    CHECK(p.check_gate(bound, "gate-01").ok());
    CHECK(p.check_gate(bound, "gate-02").error_code() == grpc::StatusCode::PERMISSION_DENIED);
    CHECK(p.check_gate(identity_for_common_name("gate-field-client"), "gate-02").ok());
}
