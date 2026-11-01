// admin_service.cpp

#include "rpc/admin_service.hpp"

#include <ctime>
#include <vector>

#include "fusion/fusion_engine.hpp"

namespace gate::rpc {

using gate::v1::Allowlistentry;
using gate::v1::DeleteAllowlistRequest;
using gate::v1::ListAllowlistRequest;
using gate::v1::ListAllowlistResponse;
using gate::v1::UpsertAllowlistRequest;
using gate::v1::UpsertAllowlistResponse;
using grpc::ServerContext;
using grpc::Status;
using grpc::StatusCode;

AdminServiceImpl::AdminServiceImpl(gate::auth::AllowlistStore& store) : store_(store) {}

Status AdminServiceImpl::UpsertAllowlist(ServerContext* ctx, const UpsertAllowlistRequest* req,
                                         UpsertAllowlistResponse* resp) {
    if (auto denied = policy_.check(ctx, Role::kDashboard, "AdminService.UpsertAllowlist");
        !denied.ok())
        return denied;
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    std::vector<Allowlistentry> entries;
    entries.reserve(static_cast<std::size_t>(req->entries_size()));
    for (int i = 0; i < req->entries_size(); ++i) {
        entries.push_back(req->entries(i));
    }
    try {
        const auto stats = store_.upsert(req->site_id(), entries);
        resp->set_inserted(stats.inserted);
        resp->set_updated(stats.updated);
    } catch (const std::exception& e) {
        return {StatusCode::INTERNAL, std::string{"upsert failed: "} + e.what()};
    }
    return Status::OK;
}

Status AdminServiceImpl::ListAllowlist(ServerContext* ctx, const ListAllowlistRequest* req,
                                       ListAllowlistResponse* resp) {
    if (auto denied = policy_.check(ctx, Role::kDashboard, "AdminService.ListAllowlist");
        !denied.ok())
        return denied;
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    try {
        // page_size=0 → AllowlistStore clamps to 1; treat 0 as "default 100".
        const auto page_size = req->page_size() == 0 ? 100u : req->page_size();
        auto page = store_.list(req->site_id(), page_size, req->page_token());
        resp->mutable_entries()->Reserve(static_cast<int>(page.entries.size()));
        for (auto& e : page.entries) {
            *resp->add_entries() = std::move(e);
        }
        resp->set_next_page_token(std::move(page.next_token));
    } catch (const std::exception& e) {
        return {StatusCode::INTERNAL, std::string{"list failed: "} + e.what()};
    }
    return Status::OK;
}

Status AdminServiceImpl::DeleteAllowlist(ServerContext* ctx, const DeleteAllowlistRequest* req,
                                         UpsertAllowlistResponse* resp) {
    if (auto denied = policy_.check(ctx, Role::kDashboard, "AdminService.DeleteAllowlist");
        !denied.ok())
        return denied;
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    std::vector<std::string> plates;
    plates.reserve(static_cast<std::size_t>(req->plate_texts_size()));
    for (int i = 0; i < req->plate_texts_size(); ++i) {
        plates.push_back(req->plate_texts(i));
    }
    try {
        const auto removed = store_.remove(req->site_id(), plates);
        // Reuse UpsertAllowlistResponse: `updated` carries the removal count;
        // `inserted` stays 0. Callers see "deleted N" in `updated`.
        resp->set_inserted(0);
        resp->set_updated(static_cast<std::uint32_t>(removed));
    } catch (const std::exception& e) {
        return {StatusCode::INTERNAL, std::string{"delete failed: "} + e.what()};
    }
    return Status::OK;
}

#define GATE_ADMIN_GUARD(name)                                                                  \
    if (auto denied = policy_.check(ctx, Role::kDashboard, "AdminService." name); !denied.ok()) \
    return denied

Status AdminServiceImpl::UpsertBlocklist(ServerContext* ctx,
                                         const gate::v1::UpsertBlocklistRequest* req,
                                         UpsertAllowlistResponse* resp) {
    GATE_ADMIN_GUARD("UpsertBlocklist");
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    try {
        for (const auto& e : req->entries()) {
            store_.blocklist_upsert(req->site_id(), e.plate_text(), e.reason(), e.added_by());
        }
        resp->set_updated(static_cast<std::uint32_t>(req->entries_size()));
    } catch (const std::exception& e) {
        return {StatusCode::INVALID_ARGUMENT, e.what()};
    }
    return Status::OK;
}

Status AdminServiceImpl::ListBlocklist(ServerContext* ctx,
                                       const gate::v1::ListBlocklistRequest* req,
                                       gate::v1::ListBlocklistResponse* resp) {
    GATE_ADMIN_GUARD("ListBlocklist");
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    auto page = store_.blocklist_list(
        req->site_id(), req->page_size() == 0 ? 100u : req->page_size(), req->page_token());
    for (auto& e : page.entries) {
        *resp->add_entries() = std::move(e);
    }
    resp->set_next_page_token(std::move(page.next_token));
    return Status::OK;
}

Status AdminServiceImpl::DeleteBlocklist(ServerContext* ctx, const DeleteAllowlistRequest* req,
                                         UpsertAllowlistResponse* resp) {
    GATE_ADMIN_GUARD("DeleteBlocklist");
    if (req->site_id().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id is required"};
    }
    const std::vector<std::string> plates(req->plate_texts().begin(), req->plate_texts().end());
    resp->set_updated(static_cast<std::uint32_t>(store_.blocklist_remove(req->site_id(), plates)));
    return Status::OK;
}

Status AdminServiceImpl::SetVehicleAccess(ServerContext* ctx,
                                          const gate::v1::SetVehicleAccessRequest* req,
                                          gate::v1::VehicleAccess* resp) {
    GATE_ADMIN_GUARD("SetVehicleAccess");
    const auto& a = req->access();
    if (a.site_id().empty() || a.plate_text().empty()) {
        return {StatusCode::INVALID_ARGUMENT, "site_id and plate_text are required"};
    }
    if (a.mode() == gate::v1::VEHICLE_ACCESS_BLOCK) {
        store_.blocklist_upsert(a.site_id(), a.plate_text(), a.reason());
    } else if (a.mode() == gate::v1::VEHICLE_ACCESS_ALLOW) {
        const std::vector<std::string> plates{a.plate_text()};
        store_.blocklist_remove(a.site_id(), plates);
    } else {
        return {StatusCode::INVALID_ARGUMENT, "mode is required"};
    }
    *resp = a;
    return Status::OK;
}

Status AdminServiceImpl::CreateGuestPass(ServerContext* ctx, const gate::v1::GuestPass* req,
                                         gate::v1::GuestPass* resp) {
    GATE_ADMIN_GUARD("CreateGuestPass");
    gate::v1::GuestPass pass{*req};
    if (pass.pass_id().empty()) {
        pass.set_pass_id(gate::fusion::generate_uuidv4());
    }
    try {
        *resp = store_.guest_pass_create(std::move(pass), std::time(nullptr));
    } catch (const std::exception& e) {
        return {StatusCode::INVALID_ARGUMENT, e.what()};
    }
    return Status::OK;
}

Status AdminServiceImpl::ListGuestPasses(ServerContext* ctx,
                                         const gate::v1::ListGuestPassesRequest* req,
                                         gate::v1::ListGuestPassesResponse* resp) {
    GATE_ADMIN_GUARD("ListGuestPasses");
    for (auto& p : store_.guest_pass_list(req->site_id())) {
        *resp->add_passes() = std::move(p);
    }
    return Status::OK;
}

Status AdminServiceImpl::RevokeGuestPass(ServerContext* ctx,
                                         const gate::v1::RevokeGuestPassRequest* req,
                                         gate::v1::GuestPass* resp) {
    GATE_ADMIN_GUARD("RevokeGuestPass");
    auto p = store_.guest_pass_revoke(req->pass_id());
    if (!p) {
        return {StatusCode::NOT_FOUND, "no such pass"};
    }
    *resp = std::move(*p);
    return Status::OK;
}

#undef GATE_ADMIN_GUARD

}  // namespace gate::rpc
