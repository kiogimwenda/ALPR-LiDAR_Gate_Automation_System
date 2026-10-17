// reports_controller.cpp — history, analytics and search (any signed-in user).
//
// Every range endpoint takes ?from=<epoch-ms>&to=<epoch-ms> (default: the
// last 24 h) and, where it buckets by hour or day, ?tz=<minutes east of
// UTC> so the operator's local clock is the one on the chart.
//
//   GET /api/reports/stats        headline numbers
//   GET /api/reports/hourly       24 local-hour buckets
//   GET /api/reports/daily        one bucket per local day
//   GET /api/reports/classes      captures per vehicle class
//   GET /api/reports/verdicts     per verdict + per deny reason
//   GET /api/reports/top-plates   ?limit=10
//   GET /api/reports/sessions     dwell categories, longest, on site now
//   GET /api/reports/rejected     ?limit=&offset= unreadable / low-confidence frames
//   GET /api/decisions            search: ?plate=&gate=&verdict=&class=
//                                 &direction=&minConfidence=&limit=&offset=
//   GET /api/decisions/export     the same filters as text/csv (≤ 100k rows)
//   GET /api/plates/{plate}       one plate's visits, verdicts and sessions

#include <charconv>
#include <chrono>
#include <drogon/HttpController.h>

#include "dash_api/history_store.hpp"
#include "grpc_http.hpp"

namespace gate::dash_api {

namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t int_param(const drogon::HttpRequestPtr& req, const char* name, std::int64_t dflt) {
    const auto& v = req->getParameter(name);
    if (v.empty()) {
        return dflt;
    }
    try {
        std::size_t used = 0;
        const auto n = std::stoll(v, &used);
        return used == v.size() ? n : dflt;
    } catch (...) {
        return dflt;
    }
}

struct Range {
    std::int64_t from;
    std::int64_t to;
    int tz;
};

Range range_of(const drogon::HttpRequestPtr& req) {
    const std::int64_t to = int_param(req, "to", now_ms());
    const std::int64_t from = int_param(req, "from", to - 86'400'000);
    const auto tz = static_cast<int>(std::clamp<std::int64_t>(int_param(req, "tz", 0), -840, 840));
    return {std::min(from, to), std::max(from, to), tz};
}

DecisionQuery query_of(const drogon::HttpRequestPtr& req) {
    const Range r = range_of(req);
    DecisionQuery q;
    q.from_ms = r.from;
    q.to_ms = r.to;
    q.plate = req->getParameter("plate");
    q.gate_id = req->getParameter("gate");
    q.verdict = req->getParameter("verdict");
    q.vehicle_class = req->getParameter("class");
    q.direction = req->getParameter("direction");
    if (const auto& mc = req->getParameter("minConfidence"); !mc.empty()) {
        // Unparseable → no floor, as if the parameter were absent.
        float v = 0.0f;
        if (std::from_chars(mc.data(), mc.data() + mc.size(), v).ec == std::errc{}) {
            q.min_confidence = v;
        }
    }
    q.limit = static_cast<std::uint32_t>(
        std::clamp<std::int64_t>(int_param(req, "limit", 100), 1, 100'000));
    q.offset = static_cast<std::uint32_t>(std::max<std::int64_t>(int_param(req, "offset", 0), 0));
    return q;
}

std::shared_ptr<HistoryStore> history_or_error(
    const std::function<void(const drogon::HttpResponsePtr&)>& callback) {
    auto h = history();
    if (h == nullptr) {
        callback(json_error(drogon::k503ServiceUnavailable, "history store not initialised"));
    }
    return h;
}

}  // namespace

class ReportsController : public drogon::HttpController<ReportsController> {
public:
    using Callback = std::function<void(const drogon::HttpResponsePtr&)>;

    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ReportsController::stats, "/api/reports/stats", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::hourly, "/api/reports/hourly", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::daily, "/api/reports/daily", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::classes, "/api/reports/classes", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::verdicts, "/api/reports/verdicts", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::top_plates, "/api/reports/top-plates", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::sessions, "/api/reports/sessions", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::rejected, "/api/reports/rejected", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::search, "/api/decisions", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::export_csv, "/api/decisions/export", drogon::Get,
                  "gate::dash_api::AuthFilter");
    ADD_METHOD_TO(ReportsController::plate, "/api/plates/{plate}", drogon::Get,
                  "gate::dash_api::AuthFilter");
    METHOD_LIST_END

    void stats(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->stats(r.from, r.to, r.tz)));
        }
    }
    void hourly(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->hourly(r.from, r.to, r.tz)));
        }
    }
    void daily(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->daily(r.from, r.to, r.tz)));
        }
    }
    void classes(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->classes(r.from, r.to)));
        }
    }
    void verdicts(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->verdicts(r.from, r.to)));
        }
    }
    void top_plates(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            const auto limit = static_cast<std::uint32_t>(int_param(req, "limit", 10));
            cb(drogon::HttpResponse::newHttpJsonResponse(h->top_plates(r.from, r.to, limit)));
        }
    }
    void sessions(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->sessions(r.from, r.to)));
        }
    }
    void rejected(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            const auto r = range_of(req);
            cb(drogon::HttpResponse::newHttpJsonResponse(
                h->rejected(r.from, r.to, static_cast<std::uint32_t>(int_param(req, "limit", 25)),
                            static_cast<std::uint32_t>(int_param(req, "offset", 0)))));
        }
    }
    void search(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            auto q = query_of(req);
            q.limit = std::min<std::uint32_t>(q.limit, 1000);
            cb(drogon::HttpResponse::newHttpJsonResponse(h->search(q)));
        }
    }
    void export_csv(const drogon::HttpRequestPtr& req, Callback&& cb) const {
        if (const auto h = history_or_error(cb)) {
            auto q = query_of(req);
            if (req->getParameter("limit").empty()) {
                q.limit = 100'000;
            }
            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setContentTypeString("text/csv; charset=utf-8");
            resp->addHeader("Content-Disposition", "attachment; filename=\"gate-decisions.csv\"");
            resp->setBody(h->export_csv(q));
            cb(resp);
        }
    }
    void plate(const drogon::HttpRequestPtr& req, Callback&& cb, const std::string& plate) const {
        if (const auto h = history_or_error(cb)) {
            cb(drogon::HttpResponse::newHttpJsonResponse(
                h->plate_history(plate, static_cast<std::uint32_t>(int_param(req, "limit", 100)))));
        }
    }
};

}  // namespace gate::dash_api
