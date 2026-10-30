// plate_text.hpp
//
// The text half of the ALPR layer: recognizer output decoders, the plate
// model config reader, and Kenya plate-format normalisation. Pure C++ — no
// TensorRT, CUDA or OpenCV — so it is built (as `gate_plate_text`) and
// unit-tested in every configuration, including the CPU-only CI build.
#pragma once

#include <istream>
#include <span>
#include <string>
#include <string_view>

namespace gate::inference {

// Decoded recognizer output.
struct DecodedText {
    std::string text;
    float mean_confidence = 0.0f;  // mean of the kept characters' argmax probabilities
};

// Fixed-slot decode (fast-plate-ocr style heads): `probs` is a [slots, C]
// post-softmax tensor, one independent classifier per character position.
// Each slot's argmax indexes `alphabet` (so C must equal alphabet.size());
// slots that decode to `pad_char` are dropped. Unlike CTC, consecutive equal
// characters are kept — "KCC 112A" needs both Cs and both 1s.
// Throws std::invalid_argument on a size mismatch.
DecodedText slot_decode(std::span<const float> probs, int slots, int num_classes,
                        std::string_view alphabet, char pad_char);

// The subset of a fast-plate-ocr `plate_config.yaml` the recognizer needs.
struct PlateModelConfig {
    int max_plate_slots = 0;
    std::string alphabet;  // one ASCII character per class, in class order
    char pad_char = '_';
    int img_height = 0;
    int img_width = 0;
    bool keep_aspect_ratio = false;
    std::string image_color_mode = "rgb";
};

// Read a plate_config.yaml. Only top-level `key: value` scalars are read
// (quoted or bare); list values such as `plate_regions` are skipped. Throws
// std::runtime_error if alphabet / max_plate_slots / img_height / img_width
// are missing, the pad character is not in the alphabet, or the alphabet has
// a repeated character.
PlateModelConfig parse_plate_config(std::istream& in);

// Result of Kenya plate-format normalisation.
struct NormalizedPlate {
    std::string text;           // normalised text (or the cleaned raw text)
    bool kenya_format = false;  // text matches a Kenyan template
};

// Normalise an OCR string against the Kenyan plate templates:
//   K L L D D D L      civilian            (KCA 123B)
//   K L L L D D D L    motorcycle / taxi   (KMDA 123C, KTWA 751Z)
//   G K L D D D L      government          (GK A123B)
//   G K D D D L        government, short   (GK 123B)
// (L = letter, D = digit, other characters literal.) The input is upper-cased
// and stripped of anything outside [A-Z0-9]; then each position that holds
// the wrong class is repaired with its usual look-alike (O/Q/D/U→0, I/L→1,
// Z→2, S→5, B→8, G→6, A→4, T→7 for digits, and the reverse for letters) and
// the template needing the fewest repairs wins. Strings no template can reach
// — old KAA 000 plates, diplomatic CD plates, foreign plates (UAA 000A,
// T 000 AAA) — are returned cleaned but otherwise untouched, with
// kenya_format = false. The literal K / GK prefix is never invented.
NormalizedPlate normalize_kenya_plate(std::string_view raw);

}  // namespace gate::inference
