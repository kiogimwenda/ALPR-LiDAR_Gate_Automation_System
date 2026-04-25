// trt_engine.hpp
//
// Modern C++ RAII wrapper around TensorRT IRuntime / ICudaEngine /
// IExecutionContext. Owns CUDA stream and device buffers for one engine.
//
// Design notes
//   * One TrtEngine instance owns one engine + one execution context. Engines
//     and contexts are NOT thread-safe: callers may share an engine but each
//     thread needs its own TrtEngine (or at least its own context).
//   * All CUDA/TRT errors throw TrtException; no return-code error handling
//     leaks into the call sites.
//   * Tensor I/O is described via TensorIo descriptors collected at engine
//     load time; callers index by name, not by binding number, so engine
//     re-exports don't break the call sites.
//   * The class is move-only.
#pragma once

#include <NvInferRuntime.h>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace gate::inference {

class TrtException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Logger that bridges TensorRT diagnostics to spdlog. Severity threshold is
// configurable; the default (kWARNING) keeps INFO chatter out of the server log.
class TrtLogger : public nvinfer1::ILogger {
public:
    explicit TrtLogger(Severity threshold = Severity::kWARNING) noexcept;
    void log(Severity severity, const char* msg) noexcept override;

private:
    Severity threshold_;
};

// Description of one engine I/O tensor.
struct TensorIo {
    std::string name;
    nvinfer1::Dims shape;  // -1 marks dynamic dims
    nvinfer1::DataType dtype;
    bool is_input;
    std::size_t elem_size;  // bytes per element
};

// One TrtEngine == one engine + one execution context + one CUDA stream
// + per-binding device buffers sized to current shapes.
class TrtEngine {
public:
    // Load a serialized engine ("*.plan") from disk.
    static TrtEngine load(const std::filesystem::path& engine_path, nvinfer1::ILogger& logger);

    TrtEngine(TrtEngine&&) noexcept;
    TrtEngine& operator=(TrtEngine&&) noexcept;
    TrtEngine(const TrtEngine&) = delete;
    TrtEngine& operator=(const TrtEngine&) = delete;
    ~TrtEngine();

    // ---- Introspection ----
    [[nodiscard]] const std::vector<TensorIo>& tensors() const noexcept { return tensors_; }
    [[nodiscard]] const TensorIo& tensor(std::string_view name) const;
    [[nodiscard]] std::size_t num_io() const noexcept { return tensors_.size(); }

    // Native handle access for advanced use. Prefer the high-level API.
    [[nodiscard]] nvinfer1::ICudaEngine* engine() const noexcept { return engine_.get(); }
    [[nodiscard]] nvinfer1::IExecutionContext* context() const noexcept { return context_.get(); }
    [[nodiscard]] cudaStream_t stream() const noexcept { return stream_; }

    // ---- Shape configuration ----
    // Set runtime input shape. Required when the engine has dynamic dims.
    // Throws if the shape isn't a valid optimization-profile shape.
    void set_input_shape(std::string_view name, nvinfer1::Dims shape);

    // ---- Buffer access ----
    // Device pointer for a binding. Buffer is sized to fit the largest
    // optimization-profile dimensions; safe to memcpy host data into.
    [[nodiscard]] void* device_buffer(std::string_view name);
    [[nodiscard]] const void* device_buffer(std::string_view name) const;

    // Bytes currently allocated for a binding (= product(shape) * elem_size).
    [[nodiscard]] std::size_t buffer_bytes(std::string_view name) const;

    // ---- Execution ----
    // Async H→D copy, enqueue, D→H copy, all on stream(). Caller follows up
    // with sync() (or sync_event(...)) when it wants the result.
    //
    // The host_in / host_out maps refer to TensorIo names. Each span's size
    // must equal buffer_bytes(name) for the corresponding tensor's current
    // shape.
    void enqueue(const std::unordered_map<std::string, std::span<const std::byte>>& host_in,
                 const std::unordered_map<std::string, std::span<std::byte>>& host_out);

    // Block until stream() reaches the current point.
    void sync();

private:
    TrtEngine() = default;

    std::unique_ptr<nvinfer1::IRuntime, void (*)(nvinfer1::IRuntime*)> runtime_{
        nullptr, [](nvinfer1::IRuntime* p) { delete p; }};
    std::unique_ptr<nvinfer1::ICudaEngine, void (*)(nvinfer1::ICudaEngine*)> engine_{
        nullptr, [](nvinfer1::ICudaEngine* p) { delete p; }};
    std::unique_ptr<nvinfer1::IExecutionContext, void (*)(nvinfer1::IExecutionContext*)> context_{
        nullptr, [](nvinfer1::IExecutionContext* p) { delete p; }};

    std::vector<TensorIo> tensors_;
    std::unordered_map<std::string, std::size_t> name_to_index_;
    std::vector<void*> device_buffers_;
    std::vector<std::size_t> device_buffer_bytes_;
    cudaStream_t stream_{nullptr};
    nvinfer1::ILogger* logger_{nullptr};

    void allocate_buffers_();
    [[nodiscard]] std::size_t buffer_index_(std::string_view name) const;
};

// Helpers
std::size_t element_size(nvinfer1::DataType dt) noexcept;
std::size_t volume(const nvinfer1::Dims& d) noexcept;
std::string shape_to_string(const nvinfer1::Dims& d);

}  // namespace gate::inference
