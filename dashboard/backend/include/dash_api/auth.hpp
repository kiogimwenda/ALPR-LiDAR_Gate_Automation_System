// auth.hpp — admin authentication for the dashboard surface
// (Phase 4.10.2, the JWT half of the Phase 4.10 hardening pass).
//
// Two independent mechanisms compose here:
//
//   password → PBKDF2-HMAC-SHA256, stored as
//              pbkdf2-sha256$<iterations>$<salt-hex>$<hash-hex>
//              (mint one with scripts/gen-admin-hash.sh)
//   session  → HS256 JWT minted on login, verified per request by
//              the AuthFilter; the signing secret is per-process
//              random unless --jwt-secret-file pins it (pinning
//              keeps sessions alive across backend restarts)
//
// Posture mirrors 4.10.1's TLS ladder: no password hash configured =
// auth disabled (dev/tests), and main() logs that loudly at startup.
// A *malformed* hash never falls back to open — verification just
// always fails.

#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace gate::dash_api {

class AuthService {
public:
    struct Config {
        std::string admin_user = "admin";
        std::string password_hash;  // empty = auth disabled
        std::string jwt_secret;     // empty = random per-process secret
        std::uint32_t token_ttl_min = 480;
        [[nodiscard]] bool enabled() const { return !password_hash.empty(); }
    };

    explicit AuthService(Config cfg);

    [[nodiscard]] bool enabled() const { return cfg_.enabled(); }
    [[nodiscard]] std::uint32_t ttl_minutes() const { return cfg_.token_ttl_min; }

    // Username + password against the configured admin credential.
    // The hash comparison is constant-time (CRYPTO_memcmp).
    [[nodiscard]] bool check_credentials(const std::string& user,
                                         const std::string& password) const;

    // Mints an HS256 JWT for `user`. Call only after check_credentials.
    [[nodiscard]] std::string issue_token(const std::string& user) const;

    // Signature + expiry + issuer check; fills `subject` on success.
    [[nodiscard]] bool verify_token(const std::string& token, std::string& subject) const;

    // PBKDF2 helpers, exposed for tooling and tests. verify_password
    // returns false on any malformed `stored` string — never open.
    [[nodiscard]] static std::string hash_password(const std::string& password,
                                                   std::uint32_t iterations = 210000);
    [[nodiscard]] static bool verify_password(const std::string& password,
                                              const std::string& stored);

private:
    Config cfg_;
};

// Process-wide accessor, same pattern as the bridge: main() builds
// the service, controllers and the filter reach it through here.
void set_auth(std::shared_ptr<AuthService> a);
std::shared_ptr<AuthService> auth();

}  // namespace gate::dash_api
