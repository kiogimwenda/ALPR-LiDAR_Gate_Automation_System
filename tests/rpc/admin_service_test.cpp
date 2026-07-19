// admin_service_test.cpp — direct calls into AdminServiceImpl handlers.
//
// These exercise the proto ↔ store adapter without spinning up a gRPC
// server: each handler is a plain C++ method taking ServerContext +
// request + response, and we call it directly with locally-constructed
// objects. End-to-end gRPC roundtrip lives in tests/integration (4.3.6).

#include "rpc/admin_service.hpp"

#include <catch2/catch_test_macros.hpp>
#include <grpcpp/server_context.h>
#include <vector>

using gate::auth::AllowlistStore;
using gate::rpc::AdminServiceImpl;
using gate::v1::Allowlistentry;
using gate::v1::DeleteAllowlistRequest;
using gate::v1::ListAllowlistRequest;
using gate::v1::ListAllowlistResponse;
using gate::v1::UpsertAllowlistRequest;
using gate::v1::UpsertAllowlistResponse;
using gate::v1::VehicleClass;

TEST_CASE("AdminService: UpsertAllowlist round-trips entries through the store", "[rpc][admin]") {
    auto store = AllowlistStore::open(":memory:");
    AdminServiceImpl svc{store};
    grpc::ServerContext ctx;

    UpsertAllowlistRequest req;
    req.set_site_id("site-a");
    auto* e1 = req.add_entries();
    e1->set_plate_text("KBZ-001A");
    e1->set_owner_name("Alice");
    e1->add_allowed_classes(VehicleClass::VEHICLE_CLASS_SEDAN);
    auto* e2 = req.add_entries();
    e2->set_plate_text("KBZ-002B");
    e2->set_owner_name("Bob");

    UpsertAllowlistResponse resp;
    REQUIRE(svc.UpsertAllowlist(&ctx, &req, &resp).ok());
    REQUIRE(resp.inserted() == 2);
    REQUIRE(resp.updated() == 0);

    // Re-upsert with a modification — counts shift to updated.
    e1->set_owner_name("Alice Renamed");
    UpsertAllowlistResponse resp2;
    REQUIRE(svc.UpsertAllowlist(&ctx, &req, &resp2).ok());
    REQUIRE(resp2.inserted() == 0);
    REQUIRE(resp2.updated() == 2);

    // Verify the modification landed in the store.
    const auto looked_up = store.lookup("site-a", "KBZ001A");
    REQUIRE(looked_up.has_value());
    REQUIRE(looked_up->owner_name() == "Alice Renamed");
}

TEST_CASE("AdminService: UpsertAllowlist rejects empty site_id", "[rpc][admin][validation]") {
    auto store = AllowlistStore::open(":memory:");
    AdminServiceImpl svc{store};
    grpc::ServerContext ctx;
    UpsertAllowlistRequest req;
    UpsertAllowlistResponse resp;
    const auto status = svc.UpsertAllowlist(&ctx, &req, &resp);
    REQUIRE_FALSE(status.ok());
    REQUIRE(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT);
}

TEST_CASE("AdminService: ListAllowlist paginates and round-trips through gRPC types",
          "[rpc][admin]") {
    auto store = AllowlistStore::open(":memory:");
    AdminServiceImpl svc{store};
    grpc::ServerContext ctx;

    // Seed 5 entries.
    UpsertAllowlistRequest seed;
    seed.set_site_id("site-a");
    for (const char* p : {"AAA111", "BBB222", "CCC333", "DDD444", "EEE555"}) {
        seed.add_entries()->set_plate_text(p);
    }
    UpsertAllowlistResponse seed_resp;
    REQUIRE(svc.UpsertAllowlist(&ctx, &seed, &seed_resp).ok());

    ListAllowlistRequest req;
    req.set_site_id("site-a");
    req.set_page_size(2);

    ListAllowlistResponse resp;
    REQUIRE(svc.ListAllowlist(&ctx, &req, &resp).ok());
    REQUIRE(resp.entries_size() == 2);
    REQUIRE(resp.entries(0).plate_text() == "AAA111");
    REQUIRE_FALSE(resp.next_page_token().empty());

    req.set_page_token(resp.next_page_token());
    ListAllowlistResponse resp2;
    REQUIRE(svc.ListAllowlist(&ctx, &req, &resp2).ok());
    REQUIRE(resp2.entries_size() == 2);
    REQUIRE(resp2.entries(0).plate_text() == "CCC333");
}

TEST_CASE("AdminService: DeleteAllowlist removes entries and reports count via `updated`",
          "[rpc][admin]") {
    auto store = AllowlistStore::open(":memory:");
    AdminServiceImpl svc{store};
    grpc::ServerContext ctx;

    UpsertAllowlistRequest seed;
    seed.set_site_id("site-a");
    seed.add_entries()->set_plate_text("KBZ001A");
    seed.add_entries()->set_plate_text("KBZ002B");
    UpsertAllowlistResponse seed_resp;
    REQUIRE(svc.UpsertAllowlist(&ctx, &seed, &seed_resp).ok());

    DeleteAllowlistRequest del;
    del.set_site_id("site-a");
    del.add_plate_texts("KBZ-001A");     // normalization makes this match
    del.add_plate_texts("NOT-PRESENT");  // counted as 0
    UpsertAllowlistResponse del_resp;
    REQUIRE(svc.DeleteAllowlist(&ctx, &del, &del_resp).ok());
    REQUIRE(del_resp.inserted() == 0);
    REQUIRE(del_resp.updated() == 1);

    REQUIRE_FALSE(store.lookup("site-a", "KBZ001A").has_value());
    REQUIRE(store.lookup("site-a", "KBZ002B").has_value());
}
