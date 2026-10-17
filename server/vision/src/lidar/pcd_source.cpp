// pcd_source.cpp — minimal PCD v0.7 codec + directory replay.

#include "vision/lidar/pcd_source.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace gate::vision::lidar {

namespace fs = std::filesystem;

namespace {

[[noreturn]] void bad(const fs::path& p, const std::string& why) {
    throw std::runtime_error("pcd: " + p.string() + ": " + why);
}

}  // namespace

std::vector<Point> read_pcd(const fs::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        bad(path, "cannot open");
    }
    std::vector<std::string> fields;
    std::vector<int> sizes;
    std::vector<char> types;
    std::vector<int> counts;
    std::size_t npoints = 0;
    std::string data;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        std::istringstream ls{line};
        std::string key;
        ls >> key;
        if (key == "FIELDS") {
            for (std::string f; ls >> f;) {
                fields.push_back(f);
            }
        } else if (key == "SIZE") {
            for (int v; ls >> v;) {
                sizes.push_back(v);
            }
        } else if (key == "TYPE") {
            for (char v; ls >> v;) {
                types.push_back(v);
            }
        } else if (key == "COUNT") {
            for (int v; ls >> v;) {
                counts.push_back(v);
            }
        } else if (key == "POINTS") {
            ls >> npoints;
        } else if (key == "DATA") {
            ls >> data;
            break;
        }
    }
    if (data.empty()) {
        bad(path, "no DATA line");
    }
    if (fields.empty() || sizes.size() != fields.size() || types.size() != fields.size()) {
        bad(path, "FIELDS/SIZE/TYPE mismatch");
    }
    if (counts.empty()) {
        counts.assign(fields.size(), 1);
    }
    if (counts.size() != fields.size()) {
        bad(path, "COUNT mismatch");
    }
    auto find = [&](const char* name) {
        const auto it = std::find(fields.begin(), fields.end(), name);
        return it == fields.end() ? -1 : static_cast<int>(it - fields.begin());
    };
    const int fx = find("x"), fy = find("y"), fz = find("z"), fi = find("intensity");
    if (fx < 0 || fy < 0 || fz < 0) {
        bad(path, "needs x y z fields");
    }

    std::vector<Point> out;
    out.reserve(npoints);
    if (data == "ascii") {
        std::size_t columns = 0;
        for (const int c : counts) {
            columns += static_cast<std::size_t>(c);
        }
        std::vector<double> row(columns);
        // Column index of the first element of each field.
        std::vector<std::size_t> col(fields.size());
        for (std::size_t f = 1; f < fields.size(); ++f) {
            col[f] = col[f - 1] + static_cast<std::size_t>(counts[f - 1]);
        }
        while (out.size() < npoints && std::getline(in, line)) {
            std::istringstream ls{line};
            std::size_t got = 0;
            while (got < columns && ls >> row[got]) {
                ++got;
            }
            if (got == 0) {
                continue;
            }
            if (got != columns) {
                bad(path, "short ascii row");
            }
            Point p;
            p.x = static_cast<float>(row[col[static_cast<std::size_t>(fx)]]);
            p.y = static_cast<float>(row[col[static_cast<std::size_t>(fy)]]);
            p.z = static_cast<float>(row[col[static_cast<std::size_t>(fz)]]);
            p.intensity = fi >= 0 ? static_cast<float>(row[col[static_cast<std::size_t>(fi)]]) : 0;
            out.push_back(p);
        }
    } else if (data == "binary") {
        std::vector<std::size_t> offset(fields.size());
        std::size_t stride = 0;
        for (std::size_t f = 0; f < fields.size(); ++f) {
            offset[f] = stride;
            stride += static_cast<std::size_t>(sizes[f] * counts[f]);
        }
        auto read_f = [&](const char* rec, int f) -> float {
            const auto fu = static_cast<std::size_t>(f);
            if (types[fu] != 'F') {
                bad(path, "field " + fields[fu] + " is not floating point");
            }
            if (sizes[fu] == 4) {
                float v;
                std::memcpy(&v, rec + offset[fu], 4);
                return v;
            }
            if (sizes[fu] == 8) {
                double v;
                std::memcpy(&v, rec + offset[fu], 8);
                return static_cast<float>(v);
            }
            bad(path, "unsupported float size");
        };
        std::vector<char> rec(stride);
        for (std::size_t i = 0; i < npoints; ++i) {
            if (!in.read(rec.data(), static_cast<std::streamsize>(stride))) {
                bad(path, "truncated binary data");
            }
            Point p;
            p.x = read_f(rec.data(), fx);
            p.y = read_f(rec.data(), fy);
            p.z = read_f(rec.data(), fz);
            p.intensity = fi >= 0 ? read_f(rec.data(), fi) : 0.0f;
            out.push_back(p);
        }
    } else {
        bad(path, "unsupported DATA " + data + " (ascii or binary only)");
    }
    return out;
}

void write_pcd(const fs::path& path, const std::vector<Point>& points) {
    std::ofstream out{path, std::ios::binary};
    if (!out) {
        throw std::runtime_error("pcd: cannot write " + path.string());
    }
    out << "# .PCD v0.7 - written by gate-vision\n"
           "VERSION 0.7\nFIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\n"
        << "WIDTH " << points.size() << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS "
        << points.size() << "\nDATA binary\n";
    static_assert(sizeof(Point) == 16, "Point must be four packed floats");
    out.write(reinterpret_cast<const char*>(points.data()),
              static_cast<std::streamsize>(points.size() * sizeof(Point)));
    if (!out) {
        throw std::runtime_error("pcd: write failed " + path.string());
    }
}

PcdReplaySource::PcdReplaySource(Config cfg) : cfg_(std::move(cfg)) {
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(cfg_.dir, ec)) {
        if (e.is_regular_file() && e.path().extension() == ".pcd") {
            files_.push_back(e.path());
        }
    }
    if (ec) {
        throw std::runtime_error("pcd replay: cannot list " + cfg_.dir.string() + ": " +
                                 ec.message());
    }
    if (files_.empty()) {
        throw std::runtime_error("pcd replay: no .pcd files in " + cfg_.dir.string());
    }
    if (cfg_.rate_hz <= 0) {
        throw std::runtime_error("pcd replay: rate_hz must be > 0");
    }
    std::sort(files_.begin(), files_.end());
    t0_ = std::chrono::steady_clock::now();
    sys_t0_ = std::chrono::system_clock::now();
}

std::optional<PointCloud> PcdReplaySource::next() {
    if (index_ == files_.size()) {
        index_ = 0;
        ++pass_;
    }
    if (cfg_.loop != 0 && pass_ >= cfg_.loop) {
        return std::nullopt;
    }
    if (cfg_.realtime) {
        const auto due =
            t0_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                      std::chrono::duration<double>(static_cast<double>(emitted_) / cfg_.rate_hz));
        std::this_thread::sleep_until(due);
    }
    PointCloud pc;
    try {
        pc.points = read_pcd(files_[index_]);
    } catch (const std::exception& e) {
        failure_ = e.what();
        return std::nullopt;
    }
    // Paced replay is stamped live; unpaced replay keeps the recorded
    // cadence so the detector's window still spans the right time.
    pc.stamp =
        cfg_.realtime
            ? std::chrono::system_clock::now()
            : sys_t0_ +
                  std::chrono::duration_cast<std::chrono::system_clock::duration>(
                      std::chrono::duration<double>(static_cast<double>(emitted_) / cfg_.rate_hz));
    ++index_;
    ++emitted_;
    return pc;
}

std::string PcdReplaySource::describe() const {
    return "pcd replay " + cfg_.dir.string() + " (" + std::to_string(files_.size()) + " files)";
}

}  // namespace gate::vision::lidar
