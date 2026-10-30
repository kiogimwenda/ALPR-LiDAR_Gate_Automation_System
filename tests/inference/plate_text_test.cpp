// plate_text_test.cpp — fixed-slot decoder, plate model config reader and
// Kenya plate-format normalisation (gate_plate_text; no GPU / OpenCV).

#include "inference/plate_text.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Catch::Matchers::WithinAbs;
using gate::inference::normalize_kenya_plate;
using gate::inference::parse_plate_config;
using gate::inference::slot_decode;

namespace {

constexpr std::string_view kAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_";

// [slots, C] probabilities where slot s peaks at kAlphabet.find(text[s]) with
// `peak`, the rest sharing the remainder; text shorter than `slots` is padded.
std::vector<float> slots_for(std::string text, int slots, float peak = 0.9f) {
    const int c = static_cast<int>(kAlphabet.size());
    text.resize(static_cast<std::size_t>(slots), '_');
    std::vector<float> p(static_cast<std::size_t>(slots) * c, (1.0f - peak) / (c - 1));
    for (int s = 0; s < slots; ++s) {
        p[static_cast<std::size_t>(s) * c + kAlphabet.find(text[static_cast<std::size_t>(s)])] =
            peak;
    }
    return p;
}

}  // namespace

TEST_CASE("slot_decode: drops pad slots and keeps repeated characters", "[slot_decode]") {
    const auto p = slots_for("KCC112A", 10);
    const auto d = slot_decode(p, 10, static_cast<int>(kAlphabet.size()), kAlphabet, '_');
    // CTC would collapse "CC" and "11"; independent slots must not.
    REQUIRE(d.text == "KCC112A");
    REQUIRE_THAT(d.mean_confidence, WithinAbs(0.9f, 1e-5f));
}

TEST_CASE("slot_decode: all-pad output is empty with zero confidence", "[slot_decode]") {
    const auto p = slots_for("", 10);
    const auto d = slot_decode(p, 10, static_cast<int>(kAlphabet.size()), kAlphabet, '_');
    REQUIRE(d.text.empty());
    REQUIRE(d.mean_confidence == 0.0f);
}

TEST_CASE("slot_decode: pad in the middle is skipped, confidence over kept slots only",
          "[slot_decode]") {
    auto p = slots_for("KM_A1", 5);
    // Make the pad slot low-confidence so it cannot leak into the mean.
    const int c = static_cast<int>(kAlphabet.size());
    for (int k = 0; k < c; ++k) {
        p[2 * c + k] = 0.0f;
    }
    p[2 * c + 36] = 0.3f;
    const auto d = slot_decode(p, 5, c, kAlphabet, '_');
    REQUIRE(d.text == "KMA1");
    REQUIRE_THAT(d.mean_confidence, WithinAbs(0.9f, 1e-5f));
}

TEST_CASE("slot_decode: class count must equal the alphabet size", "[slot_decode]") {
    const auto p = slots_for("KCA", 3);
    REQUIRE_THROWS_AS(slot_decode(p, 3, 37, kAlphabet.substr(0, 36), '_'), std::invalid_argument);
    REQUIRE_THROWS_AS(slot_decode(std::span<const float>{p}.first(10), 3, 37, kAlphabet, '_'),
                      std::invalid_argument);
}

TEST_CASE("parse_plate_config: reads the fast-plate-ocr v2 global config", "[plate_config]") {
    // Verbatim shape of cct_s_v2_global_plate_config.yaml (regions abridged).
    std::istringstream in{R"(# Config for Latin-alphabet plates

# Max number of plate slots supported. This represents the number of model classification heads.
max_plate_slots: 10
# All the possible character set for the model output.
alphabet: '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ_'
# Padding character for plates which length is smaller than MAX_PLATE_SLOTS.
pad_char: '_'
img_height: 64
img_width: 128
keep_aspect_ratio: false
interpolation: linear
image_color_mode: rgb
plate_regions: [ 'Albania', 'Andorra', 'Argentina',
                 'Unknown' ]
)"};
    const auto cfg = parse_plate_config(in);
    REQUIRE(cfg.max_plate_slots == 10);
    REQUIRE(cfg.alphabet == kAlphabet);
    REQUIRE(cfg.pad_char == '_');
    REQUIRE(cfg.img_height == 64);
    REQUIRE(cfg.img_width == 128);
    REQUIRE_FALSE(cfg.keep_aspect_ratio);
    REQUIRE(cfg.image_color_mode == "rgb");
}

TEST_CASE("parse_plate_config: rejects incomplete or inconsistent configs", "[plate_config]") {
    const auto parse = [](const std::string& s) {
        std::istringstream in{s};
        return parse_plate_config(in);
    };
    const std::string ok_tail = "img_height: 64\nimg_width: 128\nmax_plate_slots: 10\n";
    REQUIRE_NOTHROW(parse("alphabet: 'AB_'\npad_char: '_'\n" + ok_tail));
    REQUIRE_THROWS_AS(parse("pad_char: '_'\n" + ok_tail), std::runtime_error);    // no alphabet
    REQUIRE_THROWS_AS(parse("alphabet: 'AB_'\n" + ok_tail), std::runtime_error);  // no pad_char
    REQUIRE_THROWS_AS(parse("alphabet: 'AB'\npad_char: '_'\n" + ok_tail),
                      std::runtime_error);  // pad not in alphabet
    REQUIRE_THROWS_AS(parse("alphabet: 'ABA_'\npad_char: '_'\n" + ok_tail),
                      std::runtime_error);  // repeated class
    REQUIRE_THROWS_AS(parse("alphabet: 'AB_'\npad_char: '_'\nimg_height: 64\nimg_width: 128\n"),
                      std::runtime_error);  // no slots
    REQUIRE_THROWS_AS(parse("alphabet: 'AB_'\npad_char: '_'\nimg_height: x\n" + ok_tail),
                      std::runtime_error);  // not an integer
}

TEST_CASE("normalize_kenya_plate: well-formed plates pass unchanged", "[kenya]") {
    for (const char* p : {"KCA123B", "KMDA023C", "KTWA751Z", "GKA123B", "GK123B"}) {
        const auto n = normalize_kenya_plate(p);
        INFO(p);
        REQUIRE(n.text == p);
        REQUIRE(n.kenya_format);
    }
}

TEST_CASE("normalize_kenya_plate: cleans case, spaces and separators", "[kenya]") {
    const auto n = normalize_kenya_plate(" kca 123-b\t");
    REQUIRE(n.text == "KCA123B");
    REQUIRE(n.kenya_format);
}

TEST_CASE("normalize_kenya_plate: repairs look-alikes by position", "[kenya]") {
    struct Case {
        const char* raw;
        const char* want;
    };
    for (const auto& [raw, want] : {
             Case{"KCA1Z3B", "KCA123B"},    // Z in a digit slot -> 2
             Case{"KBZ3998", "KBZ399B"},    // 8 in the final letter slot -> B
             Case{"KCD4945", "KCD494S"},    // 5 -> S (the true plate was KCD494E: unrecoverable)
             Case{"K8V6O6E", "KBV606E"},    // 8 -> B in a letter slot, O -> 0 in a digit slot
             Case{"KDQ0472", "KDQ047Z"},    // final 2 -> Z
             Case{"KTWA75IZ", "KTWA751Z"},  // I -> 1
             Case{"KMDA0Z3C", "KMDA023C"},
         }) {
        INFO(raw);
        const auto n = normalize_kenya_plate(raw);
        REQUIRE(n.text == want);
        REQUIRE(n.kenya_format);
    }
}

TEST_CASE("normalize_kenya_plate: foreign, diplomatic and old plates are not forced", "[kenya]") {
    for (const char* p : {
             "UAY730S",  // Uganda
             "T371AED",  // Tanzania
             "RAD290X",  // Rwanda
             "27CD42K",  // diplomatic
             "KBP942",   // old KAA 000 (no suffix letter)
             "KBB7",     // old short
             "XCA123B",  // the K prefix is never invented from a look-alike
             "4CA123B",
         }) {
        INFO(p);
        const auto n = normalize_kenya_plate(p);
        REQUIRE(n.text == p);
        REQUIRE_FALSE(n.kenya_format);
    }
}

TEST_CASE("normalize_kenya_plate: unrepairable characters leave the text alone", "[kenya]") {
    // 'X' has no digit look-alike, so the digit slot cannot be repaired.
    const auto n = normalize_kenya_plate("KCAX23B");
    REQUIRE(n.text == "KCAX23B");
    REQUIRE_FALSE(n.kenya_format);
    REQUIRE(normalize_kenya_plate("").text.empty());
}
