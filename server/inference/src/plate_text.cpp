// plate_text.cpp

#include "inference/plate_text.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <stdexcept>

namespace gate::inference {

DecodedText slot_decode(std::span<const float> probs, int slots, int num_classes,
                        std::string_view alphabet, char pad_char) {
    if (slots <= 0 || num_classes <= 0) {
        throw std::invalid_argument("slot_decode: slots and num_classes must be positive");
    }
    if (static_cast<std::size_t>(num_classes) != alphabet.size()) {
        throw std::invalid_argument("slot_decode: model has " + std::to_string(num_classes) +
                                    " classes but the alphabet has " +
                                    std::to_string(alphabet.size()));
    }
    if (probs.size() < static_cast<std::size_t>(slots) * static_cast<std::size_t>(num_classes)) {
        throw std::invalid_argument("slot_decode: probability tensor smaller than slots x classes");
    }

    DecodedText out;
    double conf_sum = 0.0;
    int kept = 0;
    for (int s = 0; s < slots; ++s) {
        const auto row = probs.subspan(static_cast<std::size_t>(s) * num_classes,
                                       static_cast<std::size_t>(num_classes));
        const auto best = std::max_element(row.begin(), row.end());
        const char c = alphabet[static_cast<std::size_t>(best - row.begin())];
        if (c == pad_char) {
            continue;
        }
        out.text.push_back(c);
        conf_sum += *best;
        ++kept;
    }
    out.mean_confidence = kept > 0 ? static_cast<float>(conf_sum / kept) : 0.0f;
    return out;
}

namespace {

std::string trim(std::string_view s) {
    const auto b = s.find_first_not_of(" \t\r");
    if (b == std::string_view::npos) {
        return {};
    }
    const auto e = s.find_last_not_of(" \t\r");
    return std::string{s.substr(b, e - b + 1)};
}

int to_int(const std::string& key, const std::string& v) {
    try {
        std::size_t used = 0;
        const int n = std::stoi(v, &used);
        if (used != v.size()) {
            throw std::invalid_argument(v);
        }
        return n;
    } catch (const std::exception&) {
        throw std::runtime_error("plate config: '" + key + "' is not an integer: " + v);
    }
}

}  // namespace

PlateModelConfig parse_plate_config(std::istream& in) {
    PlateModelConfig cfg;
    bool have_alphabet = false;
    bool have_pad = false;
    std::string line;
    while (std::getline(in, line)) {
        // Indented lines continue a multi-line value (plate_regions: [...]).
        if (line.empty() || line[0] == ' ' || line[0] == '\t' || line[0] == '#') {
            continue;
        }
        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = trim(std::string_view{line}.substr(0, colon));
        std::string value = trim(std::string_view{line}.substr(colon + 1));
        if (!value.empty() && (value[0] == '\'' || value[0] == '"')) {
            const auto close = value.find(value[0], 1);
            if (close == std::string::npos) {
                throw std::runtime_error("plate config: unterminated quote for '" + key + "'");
            }
            value = value.substr(1, close - 1);
        } else {
            value = trim(value.substr(0, value.find('#')));
        }

        if (key == "max_plate_slots") {
            cfg.max_plate_slots = to_int(key, value);
        } else if (key == "alphabet") {
            cfg.alphabet = value;
            have_alphabet = true;
        } else if (key == "pad_char") {
            if (value.size() != 1) {
                throw std::runtime_error("plate config: pad_char must be one character");
            }
            cfg.pad_char = value[0];
            have_pad = true;
        } else if (key == "img_height") {
            cfg.img_height = to_int(key, value);
        } else if (key == "img_width") {
            cfg.img_width = to_int(key, value);
        } else if (key == "keep_aspect_ratio") {
            cfg.keep_aspect_ratio = value == "true" || value == "True";
        } else if (key == "image_color_mode") {
            cfg.image_color_mode = value;
        }
    }

    if (!have_alphabet || cfg.alphabet.empty()) {
        throw std::runtime_error("plate config: missing 'alphabet'");
    }
    if (cfg.max_plate_slots <= 0 || cfg.img_height <= 0 || cfg.img_width <= 0) {
        throw std::runtime_error(
            "plate config: max_plate_slots, img_height and img_width must be positive");
    }
    if (!have_pad || cfg.alphabet.find(cfg.pad_char) == std::string::npos) {
        throw std::runtime_error("plate config: pad_char must be present in the alphabet");
    }
    std::string sorted = cfg.alphabet;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        throw std::runtime_error("plate config: alphabet has a repeated character");
    }
    return cfg;
}

namespace {

constexpr std::array<std::string_view, 4> kTemplates = {
    "KLLDDDL",   // civilian
    "KLLLDDDL",  // motorcycle (KMxx) / taxi (KTWx)
    "GKLDDDL",   // government
    "GKDDDL",    // government, short
};

std::optional<char> to_digit(char c) {
    switch (c) {
        case 'O':
        case 'Q':
        case 'D':
        case 'U':
            return '0';
        case 'I':
        case 'L':
            return '1';
        case 'Z':
            return '2';
        case 'S':
            return '5';
        case 'B':
            return '8';
        case 'G':
            return '6';
        case 'A':
            return '4';
        case 'T':
            return '7';
        default:
            return std::nullopt;
    }
}

std::optional<char> to_letter(char c) {
    switch (c) {
        case '0':
            return 'O';
        case '1':
            return 'I';
        case '2':
            return 'Z';
        case '5':
            return 'S';
        case '8':
            return 'B';
        case '6':
            return 'G';
        case '4':
            return 'A';
        case '7':
            return 'T';
        default:
            return std::nullopt;
    }
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

}  // namespace

NormalizedPlate normalize_kenya_plate(std::string_view raw) {
    std::string clean;
    clean.reserve(raw.size());
    for (const char ch : raw) {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if ((c >= 'A' && c <= 'Z') || is_digit(c)) {
            clean.push_back(c);
        }
    }

    std::optional<std::pair<int, std::string>> best;
    for (const auto tpl : kTemplates) {
        if (tpl.size() != clean.size()) {
            continue;
        }
        std::string out;
        out.reserve(tpl.size());
        int cost = 0;
        bool ok = true;
        for (std::size_t i = 0; i < tpl.size() && ok; ++i) {
            const char want = tpl[i];
            char c = clean[i];
            if (want == 'D' && !is_digit(c)) {
                const auto d = to_digit(c);
                ok = d.has_value();
                c = d.value_or(c);
                ++cost;
            } else if (want != 'D' && is_digit(c)) {
                const auto l = to_letter(c);
                ok = l.has_value();
                c = l.value_or(c);
                ++cost;
            }
            // Literal prefix characters (the K of KAA, the GK of GK) must
            // match exactly — never invented from a look-alike.
            if (ok && want != 'L' && want != 'D' && c != want) {
                ok = false;
            }
            out.push_back(c);
        }
        if (ok && (!best || cost < best->first)) {
            best.emplace(cost, std::move(out));
        }
    }
    if (best) {
        return {std::move(best->second), true};
    }
    return {std::move(clean), false};
}

}  // namespace gate::inference
