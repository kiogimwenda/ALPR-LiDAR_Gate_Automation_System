// auth.cpp — PBKDF2 credential check + HS256 JWT mint/verify.

#include "dash_api/auth.hpp"

#include <chrono>
#include <cstdio>
#include <jwt-cpp/traits/nlohmann-json/traits.h>
#include <mutex>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gate::dash_api {

namespace {

// The vcpkg port builds jwt-cpp without its bundled picojson
// (JWT_DISABLE_PICOJSON), so a JSON traits type is explicit — and
// nlohmann-json is already in the dependency set.
using JwtTraits = jwt::traits::nlohmann_json;

constexpr const char* kIssuer = "gate-dashboard";
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kKeyBytes = 32;

std::mutex g_auth_mutex;
std::shared_ptr<AuthService> g_auth;

std::string to_hex(const unsigned char* data, std::size_t len) {
    std::string out(len * 2, '\0');
    for (std::size_t i = 0; i < len; ++i) {
        std::snprintf(&out[i * 2], 3, "%02x", data[i]);
    }
    return out;
}

bool from_hex(const std::string& hex, std::vector<unsigned char>& out) {
    if (hex.empty() || hex.size() % 2 != 0) {
        return false;
    }
    out.resize(hex.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) {
        unsigned int byte = 0;
        if (std::sscanf(hex.c_str() + i * 2, "%2x", &byte) != 1) {
            return false;
        }
        out[i] = static_cast<unsigned char>(byte);
    }
    return true;
}

std::vector<unsigned char> pbkdf2(const std::string& password,
                                  const std::vector<unsigned char>& salt,
                                  std::uint32_t iterations) {
    std::vector<unsigned char> key(kKeyBytes);
    PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                      static_cast<int>(salt.size()), static_cast<int>(iterations), EVP_sha256(),
                      static_cast<int>(key.size()), key.data());
    return key;
}

std::string random_secret() {
    unsigned char buf[kKeyBytes];
    if (RAND_bytes(buf, sizeof buf) != 1) {
        throw std::runtime_error("RAND_bytes failed — no entropy for the JWT secret");
    }
    return to_hex(buf, sizeof buf);
}

}  // namespace

AuthService::AuthService(Config cfg) : cfg_(std::move(cfg)) {
    if (cfg_.jwt_secret.empty()) {
        // Per-process secret: tokens die with the process, which is
        // the safe default for a single-instance backend.
        cfg_.jwt_secret = random_secret();
    }
}

bool AuthService::check_credentials(const std::string& user, const std::string& password) const {
    if (!enabled() || user != cfg_.admin_user) {
        return false;
    }
    return verify_password(password, cfg_.password_hash);
}

std::string AuthService::issue_token(const std::string& user) const {
    const auto now = std::chrono::system_clock::now();
    return jwt::create<JwtTraits>()
        .set_issuer(kIssuer)
        .set_subject(user)
        .set_issued_at(now)
        .set_expires_at(now + std::chrono::minutes(cfg_.token_ttl_min))
        .sign(jwt::algorithm::hs256{cfg_.jwt_secret});
}

bool AuthService::verify_token(const std::string& token, std::string& subject) const {
    try {
        const auto decoded = jwt::decode<JwtTraits>(token);
        jwt::verify<JwtTraits>()
            .allow_algorithm(jwt::algorithm::hs256{cfg_.jwt_secret})
            .with_issuer(kIssuer)
            .verify(decoded);
        subject = decoded.get_subject();
        return true;
    } catch (const std::exception&) {
        // Bad signature, expired, malformed, wrong issuer — all the
        // same answer. The caller doesn't get to distinguish.
        return false;
    }
}

std::string AuthService::hash_password(const std::string& password, std::uint32_t iterations) {
    unsigned char salt[kSaltBytes];
    if (RAND_bytes(salt, sizeof salt) != 1) {
        throw std::runtime_error("RAND_bytes failed — no entropy for the password salt");
    }
    const std::vector<unsigned char> salt_v(salt, salt + sizeof salt);
    const auto key = pbkdf2(password, salt_v, iterations);
    return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + to_hex(salt, sizeof salt) + "$" +
           to_hex(key.data(), key.size());
}

bool AuthService::verify_password(const std::string& password, const std::string& stored) {
    // pbkdf2-sha256$<iterations>$<salt-hex>$<hash-hex>
    const auto d1 = stored.find('$');
    const auto d2 = stored.find('$', d1 + 1);
    const auto d3 = stored.find('$', d2 + 1);
    if (d1 == std::string::npos || d2 == std::string::npos || d3 == std::string::npos ||
        stored.substr(0, d1) != "pbkdf2-sha256") {
        return false;
    }
    std::uint32_t iterations = 0;
    try {
        const unsigned long parsed = std::stoul(stored.substr(d1 + 1, d2 - d1 - 1));
        if (parsed == 0 || parsed > 10'000'000) {
            return false;
        }
        iterations = static_cast<std::uint32_t>(parsed);
    } catch (const std::exception&) {
        return false;
    }
    std::vector<unsigned char> salt;
    std::vector<unsigned char> expected;
    if (!from_hex(stored.substr(d2 + 1, d3 - d2 - 1), salt) ||
        !from_hex(stored.substr(d3 + 1), expected) || expected.size() != kKeyBytes) {
        return false;
    }
    const auto actual = pbkdf2(password, salt, iterations);
    return CRYPTO_memcmp(actual.data(), expected.data(), kKeyBytes) == 0;
}

void set_auth(std::shared_ptr<AuthService> a) {
    const std::lock_guard<std::mutex> lock(g_auth_mutex);
    g_auth = std::move(a);
}

std::shared_ptr<AuthService> auth() {
    const std::lock_guard<std::mutex> lock(g_auth_mutex);
    return g_auth;
}

}  // namespace gate::dash_api
