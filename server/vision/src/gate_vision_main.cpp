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
// LiDAR (Phase 6.1), added on top of either source:
//   --lidar <spec>      synthetic:<scene.json> | pcd:<dir> |
//                       unitree-serial:<dev> | unitree-udp:<ip>:<port>@<ip>:<port>
//                       (unitree-* when built with UNITREE_LIDAR_SDK_DIR)
//   --lidar-config <json>  site mounting pose + zones (vision/lidar/lidar_config.hpp)
//   --record-lidar <dir>   also save every raw batch as .pcd for replay
// Each plate frame then carries the LiDAR's vehicles (vision/fused_source.hpp).
//
// Exactly one primary source must be given. Transport security is the standard
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

#include "tls_runtime.hpp"
#include "vision/frame_builder.hpp"
#include "vision/fused_source.hpp"
#include "vision/lidar/lidar_config.hpp"
#include "vision/lidar/lidar_worker.hpp"
#include "vision/lidar/pcd_source.hpp"
#include "vision/lidar/synthetic_source.hpp"
#include "vision/scenario_source.hpp"
#include "vision/stop.hpp"
#include "vision/submit_client.hpp"

#if defined(GATE_VISION_HAVE_UNITREE)
#include "vision/lidar/unitree_l1_source.hpp"
#endif

#if defined(GATE_VISION_HAVE_CAMERA)
#include "vision/camera_source.hpp"
#endif

namespace {

// The handler only sets a lock-free atomic (async-signal-safe). Every
// wait in the daemon goes through vision::sleep_until (stop.hpp), so a
// SIGTERM ends a scripted gap, a camera cadence or a submit backoff
// within ~50 ms instead of whenever the next frame was due.
void handle_signal(int /*sig*/) {
    gate::vision::request_stop();
}

// --lidar <spec> → driver. Throws on an unknown or unavailable scheme.
std::unique_ptr<gate::vision::lidar::CloudSource> open_lidar(const std::string& spec,
                                                             float pcd_rate_hz) {
    namespace lidar = gate::vision::lidar;
    if (spec.rfind("synthetic:", 0) == 0) {
        auto scene = lidar::load_scene(spec.substr(10));
        return std::make_unique<lidar::SyntheticSource>(std::move(scene));
    }
    if (spec.rfind("pcd:", 0) == 0) {
        lidar::PcdReplaySource::Config c;
        c.dir = spec.substr(4);
        c.rate_hz = pcd_rate_hz;
        return std::make_unique<lidar::PcdReplaySource>(std::move(c));
    }
    if (spec.rfind("unitree-", 0) == 0) {
#if defined(GATE_VISION_HAVE_UNITREE)
        return std::make_unique<lidar::UnitreeL1Source>(lidar::UnitreeL1Source::parse_spec(spec));
#else
        throw std::runtime_error(
            "this gate-vision was built without the Unitree SDK — reconfigure with "
            "-DUNITREE_LIDAR_SDK_DIR=<unilidar_sdk>/unitree_lidar_sdk");
#endif
    }
    throw std::runtime_error("unknown --lidar spec '" + spec +
                             "' (synthetic:<scene.json> | pcd:<dir> | unitree-serial:<dev> | "
                             "unitree-udp:<ip>:<port>@<ip>:<port>)");
}

}  // namespace

int main(int argc, char** argv) {
    gate::proto::init_tls_runtime();
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

    std::string lidar_spec;
    std::string lidar_config_path;
    std::string record_lidar;
    std::uint32_t lidar_max_skew_ms = 300;
    std::uint32_t lidar_stale_ms = 1500;
    float pcd_rate_hz = 10.0f;
    app.add_option("--lidar", lidar_spec,
                   "LiDAR source: synthetic:<scene.json> | pcd:<dir> | unitree-serial:<dev> | "
                   "unitree-udp:<ip>:<port>@<ip>:<port>");
    app.add_option("--lidar-config", lidar_config_path,
                   "Site LiDAR config JSON (mounting pose, trigger/exclude zones).")
        ->needs(app.get_option("--lidar"));
    app.add_option("--record-lidar", record_lidar, "Also save each raw LiDAR batch as .pcd here.")
        ->needs(app.get_option("--lidar"));
    app.add_option("--lidar-max-skew-ms", lidar_max_skew_ms,
                   "Max camera↔LiDAR timestamp gap for pairing.")
        ->capture_default_str()
        ->needs(app.get_option("--lidar"));
    app.add_option("--lidar-stale-ms", lidar_stale_ms,
                   "LiDAR silence that counts as a fault (daemon exits non-zero).")
        ->capture_default_str()
        ->needs(app.get_option("--lidar"));
    app.add_option("--pcd-rate-hz", pcd_rate_hz, "Replay rate for --lidar pcd:<dir>.")
        ->capture_default_str()
        ->needs(app.get_option("--lidar"));

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

        std::shared_ptr<gate::vision::lidar::LidarWorker> lidar_worker;
        if (!lidar_spec.empty()) {
            namespace lidar = gate::vision::lidar;
            lidar::DetectorConfig dcfg = lidar_config_path.empty()
                                             ? lidar::DetectorConfig{}
                                             : lidar::load_detector_config(lidar_config_path);
            if (lidar_config_path.empty()) {
                spdlog::warn(
                    "lidar: no --lidar-config — using the identity mount and default "
                    "zones; commission the site before production use");
            }
            lidar::LidarWorker::Config wcfg;
            wcfg.stale_after = std::chrono::milliseconds{lidar_stale_ms};
            wcfg.record_dir = record_lidar;
            lidar_worker = std::make_shared<lidar::LidarWorker>(
                open_lidar(lidar_spec, pcd_rate_hz), lidar::VehicleDetector{std::move(dcfg)}, wcfg);
            lidar_worker->start();
            source = std::make_unique<gate::vision::FusedSource>(
                std::move(source), lidar_worker,
                gate::vision::FusedSource::Config{std::chrono::milliseconds{lidar_max_skew_ms}});
            spdlog::info("lidar: {} (max skew {} ms, stale after {} ms){}",
                         lidar_worker->describe(), lidar_max_skew_ms, lidar_stale_ms,
                         record_lidar.empty() ? "" : ", recording to " + record_lidar);
        }

        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);

        std::uint64_t submitted = 0;
        std::uint64_t authorized = 0;
        while (!gate::vision::stop_requested()) {
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
            spdlog::info(
                "decision: frame_id={} verdict={} plate='{}' class={} vehicles={} "
                "conf={:.2f} {}",
                frame.frame_id(), gate::v1::AuthVerdict_Name(decision.verdict()),
                decision.matched_plate(), gate::v1::VehicleClass_Name(decision.matched_class()),
                frame.vehicles_size(), decision.combined_conf(), decision.reason_text());
        }

        const std::string failure =
            gate::vision::stop_requested() ? std::string{} : source->failure();
        spdlog::info("gate-vision stopping ({}): {} frames submitted, {} authorized",
                     gate::vision::stop_requested() ? "signal"
                     : failure.empty()              ? "end of stream"
                                                    : "source failed",
                     submitted, authorized);
        if (lidar_worker) {
            lidar_worker->stop();
        }
        if (!failure.empty()) {
            // A dead camera or LiDAR is a fault, not a clean exit — the
            // non-zero status is what makes systemd's Restart=on-failure
            // re-open the device.
            spdlog::error("gate-vision: source failed: {}", failure);
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        // One exit path for every fail-closed throw: bad PEM, bad
        // scenario, dead camera, exhausted submit retries. systemd
        // restarts us; a scripted E2E sees the non-zero exit.
        if (gate::vision::stop_requested()) {
            // A submit cut short by SIGTERM is a clean stop, not a fault.
            spdlog::info("gate-vision stopping (signal): {}", e.what());
            return 0;
        }
        spdlog::error("gate-vision: fatal: {}", e.what());
        return 1;
    }
}
