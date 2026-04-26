// admin_service.cpp

#include "rpc/admin_service.hpp"

#include <vector>

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

Status AdminServiceImpl::UpsertAllowlist(ServerContext* /*ctx*/, const UpsertAllowlistRequest* req,
                                         UpsertAllowlistResponse* resp) {
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

Status AdminServiceImpl::ListAllowlist(ServerContext* /*ctx*/, const ListAllowlistRequest* req,
                                       ListAllowlistResponse* resp) {
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

Status AdminServiceImpl::DeleteAllowlist(ServerContext* /*ctx*/, const DeleteAllowlistRequest* req,
                                         UpsertAllowlistResponse* resp) {
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

}  // namespace gate::rpc
