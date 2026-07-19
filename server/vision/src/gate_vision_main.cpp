// gate_vision_main.cpp — gate-vision daemon entry point (Phase 5.3).
//
// The capture-side process on the GPU host: pulls ObservationSets from
// exactly one source, converts them to DetectionFrames, and submits
// them to gate-server's SubmitDetection — where an AUTHORIZED verdict
// auto-dispatches OPEN_GATE to the detected gate's Control stream.
//
//   gate-vision --server 127.0.0.1:50051 --gate-id gate-01
//               --scenario driveway.json
//
// Sources:
//   --scenario <json>   scripted replay (every build) — demos, E2E,
//                       bench dry-runs with no camera attached
//   --camera <uri>      RTSP/file/device through the TensorRT ALPR
//                       pipeline (ENABLE_GPU builds only), with
//                       --detector-engine / --recognizer-engine /
//                       --ocr-dictionary
//
// Exactly one source must be given. Transport security is the standard
// ladder (plaintext-but-loud → TLS → mTLS) via --tls-ca/--tls-cert/
// --tls-key; any configured-but-unreadable PEM refuses to start.

#include <CLI/CLI.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>

#include "vision/frame_builder.hpp"
#include "vision/scenario_source.hpp"
#include "vision/submit_client.hpp"

#if defined(GATE_VISION_HAVE_CAMERA)
#include "vision/camera_source.hpp"
#endif

namespace {

// Same async-signal-safe pattern as gate-server: the handler only sets
// the flag; the submit loop polls it between frames.
volatile std::sig_atomic_t g_shutdown_requested = 0;

void handle_signal(int /*sig*/) {
    g_shutdown_requested = 1;
}

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"gate-vision: detection ingest daemon (scenario or camera source)"};

    gate::vision::SubmitClient::Config client_cfg;
    std::string scenario_path;
    std::string log_level = "info";

    app.add_option("--server", client_cfg.server, "gate-server gRPC address (host:port).")
        ->capture_default_str();
    app.add_option("--site-id", client_cfg.site_id, "Site identifier (logs only).")
        ->capture_default_str();
    app.add_option("--gate-id", client_cfg.gate_id,
                   "Gate this sensor watches — stamped on every DetectionFrame.")
        ->capture_default_str();
    app.add_option("--scenario", scenario_path, "Scenario JSON file (scripted source).");
    app.add_option("--deadline-ms", client_cfg.deadline_ms, "Per-attempt RPC deadline.")
        ->capture_default_str();
    app.add_option("--log-level", log_level,
                   "spdlog level: trace, debug, info, warn, error, critical, off.")
        ->default_str(log_level);

    app.add_option("--tls-ca", client_cfg.tls_ca_path, "Site CA bundle (PEM); enables TLS.");
    app.add_option("--tls-cert", client_cfg.tls_cert_path, "Client certificate (PEM) for mTLS.")
        ->needs(app.get_option("--tls-ca"));
    app.add_option("--tls-key", client_cfg.tls_key_path, "Client private key (PEM).")
        ->needs(app.get_option("--tls-cert"));

#if defined(GATE_VISION_HAVE_CAMERA)
    gate::vision::CameraSource::Config cam_cfg;
    app.add_option("--camera", cam_cfg.uri,
                   "Capture URI: rtsp://…, a video file, or a V4L2 index.");
    app.add_option("--detector-engine", cam_cfg.alpr.detector.engine_path,
                   "YOLOv9 plate detector TensorRT .plan")
        ->needs(app.get_option("--camera"));
    app.add_option("--recognizer-engine", cam_cfg.alpr.recognizer.engine_path,
                   "PaddleOCR recognizer TensorRT .plan")
        ->needs(app.get_option("--camera"));
    app.add_option("--ocr-dictionary", cam_cfg.alpr.recognizer.dictionary_path,
                   "Recognizer character dictionary (one UTF-8 char per line)")
        ->needs(app.get_option("--camera"));
    app.add_option("--frame-interval-ms", cam_cfg.frame_interval_ms,
                   "Inference cadence — frames are throttled to this interval.")
        ->capture_default_str();
    app.add_option("--max-frames", cam_cfg.max_frames,
                   "Stop after N frames (0 = run until signalled).")
        ->capture_default_str();
#endif

    CLI11_PARSE(app, argc, argv);

    spdlog::set_level(spdlog::level::from_str(log_level));

    // Exactly one source. The daemon refuses ambiguity instead of
    // picking — a box with a camera configured AND a scenario is a
    // provisioning error, not a preference.
    std::string camera_uri;
#if defined(GATE_VISION_HAVE_CAMERA)
    camera_uri = cam_cfg.uri;
#endif
    if (scenario_path.empty() == camera_uri.empty()) {
#if defined(GATE_VISION_HAVE_CAMERA)
        spdlog::error("exactly one of --scenario or --camera is required");
#else
        spdlog::error(
            "a --scenario file is required (this build has no camera support — ENABLE_GPU=OFF)");
#endif
        return 1;
    }

    spdlog::info("gate-vision starting (server={}, site={}, gate={})", client_cfg.server,
                 client_cfg.site_id, client_cfg.gate_id);

    try {
        // Construction order is the fail-closed gauntlet: credentials
        // first (unreadable PEM throws), then the source (missing
        // scenario/camera/engines throw). Nothing submits until both
        // stand.
        gate::vision::SubmitClient client{client_cfg};
        gate::vision::FrameBuilder builder{
            {client_cfg.site_id, client_cfg.gate_id, /*lane_id=*/""}};

        std::unique_ptr<gate::vision::ObservationSource> source;
        if (!scenario_path.empty()) {
            source = std::make_unique<gate::vision::ScenarioSource>(
                gate::vision::ScenarioSource::from_file(scenario_path));
            spdlog::info("source: scenario {}", scenario_path);
        }
#if defined(GATE_VISION_HAVE_CAMERA)
        else {
            source = std::make_unique<gate::vision::CameraSource>(
                gate::vision::CameraSource::open(cam_cfg));
            spdlog::info("source: camera {} (interval {} ms)", cam_cfg.uri,
                         cam_cfg.frame_interval_ms);
        }
#endif

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        std::uint64_t submitted = 0;
        std::uint64_t authorized = 0;
        while (g_shutdown_requested == 0) {
            const auto obs = source->next();
            if (!obs) {
                break;  // Stream ended (scenario done, camera dead).
            }
            // Empty sets are a heartbeat of "nothing in the driveway" —
            // real information for a camera, but not worth an RPC.
            if (obs->plates.empty() && obs->vehicles.empty()) {
                continue;
            }
            const auto frame = builder.build(*obs);
            const auto decision = client.submit(frame);
            ++submitted;
            if (decision.verdict() == gate::v1::AUTH_VERDICT_AUTHORIZED) {
                ++authorized;
            }
            spdlog::info("decision: frame_id={} verdict={} plate='{}' conf={:.2f} {}",
                         frame.frame_id(), gate::v1::AuthVerdict_Name(decision.verdict()),
                         decision.matched_plate(), decision.combined_conf(),
                         decision.reason_text());
        }

        spdlog::info("gate-vision stopping ({}): {} frames submitted, {} authorized",
                     g_shutdown_requested ? "signal" : "end of stream", submitted, authorized);
        return 0;
    } catch (const std::exception& e) {
        // One exit path for every fail-closed throw: bad PEM, bad
        // scenario, dead camera, exhausted submit retries. systemd
        // restarts us; a scripted E2E sees the non-zero exit.
        spdlog::error("gate-vision: fatal: {}", e.what());
        return 1;
    }
}
