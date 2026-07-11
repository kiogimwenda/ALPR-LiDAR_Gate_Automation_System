// allowlist_controller.cpp — /api/allowlist CRUD, proxied to AdminService.
//
//   GET    /api/allowlist?pageSize=&pageToken=   → list (paged)
//   POST   /api/allowlist                        → upsert; body is one
//          entry object or {"entries":[…]} for a batch
//   DELETE /api/allowlist/{plate}                → remove one plate
//
// The bridge stamps site_id and addedBy/addedTs defaults; validation
// errors from allowlist_entry_from_json surface as 400s with the
// specific reason, upstream refusals via the shared status mapping.

#include <drogon/HttpController.h>

#include "dash_api/grpc_bridge.hpp"
#include "dash_api/json_mapping.hpp"
#include "grpc_http.hpp"

namespace gate::dash_api {

class AllowlistController : public drogon::HttpController<AllowlistController> {
public:
    // All three verbs sit behind the AuthFilter (4.10.2) — the list
    // isn't just config, it's resident PII (names, units, plates).
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AllowlistController::list, "/api/allowlist", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(AllowlistController::upsert, "/api/allowlist", drogon::Post,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(AllowlistController::remove, "/api/allowlist/{plate}", drogon::Delete,
                  "gate::dash_api::AuthFilter");
    METHOD_LIST_END

    void list(const drogon::HttpRequestPtr& req,
              std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
        const auto b = bridge_or_error(callback);
        if (b == nullptr) {
            return;
        }
        std::uint32_t page_size = 100;
        if (const auto ps = req->getParameter("pageSize"); !ps.empty()) {
            page_size = static_cast<std::uint32_t>(std::stoul(ps));
        }
        gate::v1::ListAllowlistResponse resp;
        const auto result = b->list_allowlist(page_size, req->getParameter("pageToken"), resp);
        if (!result.ok()) {
            callback(json_error(http_status(result.code), result.message));
            return;
        }
        callback(drogon::HttpResponse::newHttpJsonResponse(to_json(resp)));
    }

    void upsert(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
        const auto b = bridge_or_error(callback);
        if (b == nullptr) {
            return;
        }
        const auto body = req->getJsonObject();
        if (body == nullptr) {
            callback(json_error(drogon::k400BadRequest, "body must be JSON"));
            return;
        }
        gate::v1::UpsertAllowlistRequest upstream;
        std::string error;
        if (body->isMember("entries")) {
            if (!(*body)["entries"].isArray() || (*body)["entries"].empty()) {
                callback(json_error(drogon::k400BadRequest, "'entries' must be a non-empty array"));
                return;
            }
            for (const auto& ev : (*body)["entries"]) {
                if (!allowlist_entry_from_json(ev, *upstream.add_entries(), error)) {
                    callback(json_error(drogon::k400BadRequest, error));
                    return;
                }
            }
        } else {
            if (!allowlist_entry_from_json(*body, *upstream.add_entries(), error)) {
                callback(json_error(drogon::k400BadRequest, error));
                return;
            }
        }
        gate::v1::UpsertAllowlistResponse resp;
        const auto result = b->upsert_allowlist(std::move(upstream), resp);
        if (!result.ok()) {
            callback(json_error(http_status(result.code), result.message));
            return;
        }
        callback(drogon::HttpResponse::newHttpJsonResponse(to_json(resp)));
    }

    void remove(const drogon::HttpRequestPtr& /*req*/,
                std::function<void(const drogon::HttpResponsePtr&)>&& callback,
                std::string plate) const {
        const auto b = bridge_or_error(callback);
        if (b == nullptr) {
            return;
        }
        if (plate.empty()) {
            callback(json_error(drogon::k400BadRequest, "plate path segment is required"));
            return;
        }
        gate::v1::UpsertAllowlistResponse resp;
        const auto result = b->delete_allowlist(plate, resp);
        if (!result.ok()) {
            callback(json_error(http_status(result.code), result.message));
            return;
        }
        callback(drogon::HttpResponse::newHttpJsonResponse(to_json(resp)));
    }
};

}  // namespace gate::dash_api
