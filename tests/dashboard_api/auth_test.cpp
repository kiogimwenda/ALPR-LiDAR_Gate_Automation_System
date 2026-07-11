// auth_test.cpp — the 4.10.2 admin auth core: PBKDF2 credential
// verification and HS256 token mint/verify. Every rejection path is
// pinned — a malformed stored hash must fail closed, never open.

#include "dash_api/auth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

namespace dash = gate::dash_api;

namespace {

// Low iteration count: these tests hash dozens of passwords and the
// KDF's slowness is the production feature, not the tested logic.
std::string hash_of(const std::string& password) {
    return dash::AuthService::hash_password(password, 1000);
}

dash::AuthService make_service(std::uint32_t ttl_min = 60) {
    dash::AuthService::Config cfg;
    cfg.admin_user = "admin";
    cfg.password_hash = hash_of("correct horse");
    cfg.token_ttl_min = ttl_min;
    return dash::AuthService{cfg};
}

}  // namespace

TEST_CASE("password hash round-trips and rejects the wrong password") {
    const auto stored = hash_of("hunter2");
    CHECK(dash::AuthService::verify_password("hunter2", stored));
    CHECK_FALSE(dash::AuthService::verify_password("hunter3", stored));
    CHECK_FALSE(dash::AuthService::verify_password("", stored));
}

TEST_CASE("two hashes of the same password differ (fresh salt) but both verify") {
    const auto a = hash_of("same-password");
    const auto b = hash_of("same-password");
    CHECK(a != b);
    CHECK(dash::AuthService::verify_password("same-password", a));
    CHECK(dash::AuthService::verify_password("same-password", b));
}

TEST_CASE("malformed stored hashes fail closed") {
    CHECK_FALSE(dash::AuthService::verify_password("pw", ""));
    CHECK_FALSE(dash::AuthService::verify_password("pw", "not-a-hash"));
    CHECK_FALSE(dash::AuthService::verify_password("pw", "md5$1000$aa$bb"));
    CHECK_FALSE(dash::AuthService::verify_password("pw", "pbkdf2-sha256$abc$aa$bb"));
    CHECK_FALSE(dash::AuthService::verify_password("pw", "pbkdf2-sha256$0$aa$bb"));
    CHECK_FALSE(dash::AuthService::verify_password("pw", "pbkdf2-sha256$1000$zz$bb"));
    // Truncated digest — right prefix, wrong length.
    CHECK_FALSE(dash::AuthService::verify_password("pw", "pbkdf2-sha256$1000$aabb$aabb"));
}

TEST_CASE("check_credentials wants both halves right") {
    const auto svc = make_service();
    CHECK(svc.check_credentials("admin", "correct horse"));
    CHECK_FALSE(svc.check_credentials("admin", "wrong"));
    CHECK_FALSE(svc.check_credentials("root", "correct horse"));
}

TEST_CASE("auth disabled when no hash is configured") {
    const dash::AuthService svc{dash::AuthService::Config{}};
    CHECK_FALSE(svc.enabled());
    CHECK_FALSE(svc.check_credentials("admin", "anything"));
}

TEST_CASE("token round-trips with the subject intact") {
    const auto svc = make_service();
    const auto token = svc.issue_token("admin");
    REQUIRE_FALSE(token.empty());
    std::string subject;
    REQUIRE(svc.verify_token(token, subject));
    CHECK(subject == "admin");
}

TEST_CASE("tampered and foreign tokens are rejected") {
    const auto svc = make_service();
    auto token = svc.issue_token("admin");
    std::string subject;

    SECTION("flipped character in the signature") {
        token.back() = (token.back() == 'A') ? 'B' : 'A';
        CHECK_FALSE(svc.verify_token(token, subject));
    }
    SECTION("token minted under a different secret") {
        const auto other = make_service();  // fresh random per-process secret
        CHECK_FALSE(other.verify_token(token, subject));
    }
    SECTION("garbage is not a token") {
        CHECK_FALSE(svc.verify_token("not.a.jwt", subject));
        CHECK_FALSE(svc.verify_token("", subject));
    }
}

TEST_CASE("expired tokens are rejected") {
    const auto svc = make_service(/*ttl_min=*/0);  // exp == iat
    const auto token = svc.issue_token("admin");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    std::string subject;
    CHECK_FALSE(svc.verify_token(token, subject));
}

TEST_CASE("a pinned secret keeps tokens valid across service instances") {
    dash::AuthService::Config cfg;
    cfg.password_hash = hash_of("pw");
    cfg.jwt_secret = "shared-secret-for-restart-survival";
    const dash::AuthService first{cfg};
    const dash::AuthService second{cfg};  // "the restarted backend"
    std::string subject;
    CHECK(second.verify_token(first.issue_token("admin"), subject));
    CHECK(subject == "admin");
}
