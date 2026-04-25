// trt_engine.cpp
//
// Implementation of the RAII TensorRT wrapper declared in trt_engine.hpp.
// Targets TensorRT 10.x: no destroy(), V3 enqueue path, name-based I/O.

#include "inference/trt_engine.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>
#include <spdlog/spdlog.h>
#include <sstream>
#include <utility>

namespace gate::inference {

namespace {

// CUDA error → TrtException, with the failing call's name in the message.
inline void check_cuda(cudaError_t status, const char* what) {
    if (status != cudaSuccess) {
        throw TrtException(std::string{what} + " failed: " + cudaGetErrorString(status));
    }
}

// Read a serialized engine plan into a heap buffer. The plan is opaque bytes;
// we don't parse it — TensorRT does.
std::vector<std::byte> read_engine_blob(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary | std::ios::ate};
    if (!file) {
        throw TrtException("failed to open engine file: " + path.string());
    }
    const auto size = static_cast<std::streamsize>(file.tellg());
    if (size <= 0) {
        throw TrtException("engine file is empty: " + path.string());
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> blob(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(blob.data()), size)) {
        throw TrtException("failed to read engine file: " + path.string());
    }
    return blob;
}

// Map a TRT log severity to a spdlog level. We're conservative: kERROR and
// kINTERNAL_ERROR escalate to error/critical so they show up in production logs.
spdlog::level::level_enum to_spdlog(nvinfer1::ILogger::Severity s) noexcept {
    using S = nvinfer1::ILogger::Severity;
    switch (s) {
        case S::kINTERNAL_ERROR:
            return spdlog::level::critical;
        case S::kERROR:
            return spdlog::level::err;
        case S::kWARNING:
            return spdlog::level::warn;
        case S::kINFO:
            return spdlog::level::info;
        case S::kVERBOSE:
            return spdlog::level::debug;
    }
    return spdlog::level::info;
}

}  // namespace

// ---- TrtLogger -------------------------------------------------------------

TrtLogger::TrtLogger(Severity threshold) noexcept : threshold_(threshold) {}

void TrtLogger::log(Severity severity, const char* msg) noexcept {
    // ILogger::log can be called from any TRT-internal thread; spdlog is
    // thread-safe so this is fine.
    if (static_cast<int>(severity) > static_cast<int>(threshold_)) {
        return;  // less severe than threshold (kVERBOSE has the highest int)
    }
    spdlog::log(to_spdlog(severity), "[TensorRT] {}", msg ? msg : "");
}

// ---- Helpers ---------------------------------------------------------------

std::size_t element_size(nvinfer1::DataType dt) noexcept {
    using DT = nvinfer1::DataType;
    switch (dt) {
        case DT::kFLOAT:
            return 4;
        case DT::kHALF:
            return 2;
        case DT::kBF16:
            return 2;
        case DT::kINT64:
            return 8;
        case DT::kINT32:
            return 4;
        case DT::kINT8:
            return 1;
        case DT::kUINT8:
            return 1;
        case DT::kBOOL:
            return 1;
        case DT::kFP8:
            return 1;
        case DT::kINT4:
            return 1;  // packed: caller must round up
        case DT::kFP4:
            return 1;  // packed: caller must round up
        case DT::kE8M0:
            return 1;
    }
    return 0;
}

std::size_t volume(const nvinfer1::Dims& d) noexcept {
    if (d.nbDims <= 0)
        return 0;
    std::size_t v = 1;
    for (int i = 0; i < d.nbDims; ++i) {
        if (d.d[i] < 0)
            return 0;  // dynamic dim; caller must resolve first
        v *= static_cast<std::size_t>(d.d[i]);
    }
    return v;
}

std::string shape_to_string(const nvinfer1::Dims& d) {
    std::ostringstream os;
    os << '[';
    for (int i = 0; i < d.nbDims; ++i) {
        if (i)
            os << ',';
        os << d.d[i];
    }
    os << ']';
    return os.str();
}

// ---- TrtEngine: lifecycle --------------------------------------------------

TrtEngine TrtEngine::load(const std::filesystem::path& engine_path, nvinfer1::ILogger& logger) {
    auto blob = read_engine_blob(engine_path);

    TrtEngine self;
    self.logger_ = &logger;

    // Runtime / engine / context are managed by unique_ptrs whose deleters call
    // `delete` directly — TensorRT 10 dropped destroy() in favor of the C++
    // delete operator, since these are now real polymorphic objects.
    self.runtime_.reset(nvinfer1::createInferRuntime(logger));
    if (!self.runtime_) {
        throw TrtException("createInferRuntime returned null");
    }

    self.engine_.reset(self.runtime_->deserializeCudaEngine(blob.data(), blob.size()));
    if (!self.engine_) {
        throw TrtException("deserializeCudaEngine failed for: " + engine_path.string());
    }

    self.context_.reset(self.engine_->createExecutionContext());
    if (!self.context_) {
        throw TrtException("createExecutionContext returned null");
    }

    check_cuda(cudaStreamCreate(&self.stream_), "cudaStreamCreate");

    // Walk all I/O tensors and build descriptors. Name-based binding is the
    // only supported addressing scheme in TRT 10.
    const int n_io = self.engine_->getNbIOTensors();
    self.tensors_.reserve(static_cast<std::size_t>(n_io));
    for (int i = 0; i < n_io; ++i) {
        const char* name = self.engine_->getIOTensorName(i);
        TensorIo io;
        io.name = name;
        io.shape = self.engine_->getTensorShape(name);
        io.dtype = self.engine_->getTensorDataType(name);
        io.is_input = self.engine_->getTensorIOMode(name) == nvinfer1::TensorIOMode::kINPUT;
        io.elem_size = element_size(io.dtype);
        self.name_to_index_.emplace(io.name, self.tensors_.size());
        self.tensors_.push_back(std::move(io));
    }

    self.allocate_buffers_();

    spdlog::info("Loaded TensorRT engine: {} ({} I/O tensors)", engine_path.string(),
                 self.tensors_.size());
    return self;
}

TrtEngine::TrtEngine(TrtEngine&& other) noexcept
    : runtime_(std::move(other.runtime_)),
      engine_(std::move(other.engine_)),
      context_(std::move(other.context_)),
      tensors_(std::move(other.tensors_)),
      name_to_index_(std::move(other.name_to_index_)),
      device_buffers_(std::move(other.device_buffers_)),
      device_buffer_bytes_(std::move(other.device_buffer_bytes_)),
      stream_(other.stream_),
      logger_(other.logger_) {
    other.stream_ = nullptr;
}

TrtEngine& TrtEngine::operator=(TrtEngine&& other) noexcept {
    if (this != &other) {
        // Tear down current state in the right order: free CUDA buffers and
        // stream before letting the unique_ptrs drop the TRT objects.
        for (void* p : device_buffers_) {
            if (p)
                cudaFree(p);
        }
        device_buffers_.clear();
        device_buffer_bytes_.clear();
        if (stream_) {
            cudaStreamDestroy(stream_);
            stream_ = nullptr;
        }
        context_.reset();
        engine_.reset();
        runtime_.reset();

        runtime_ = std::move(other.runtime_);
        engine_ = std::move(other.engine_);
        context_ = std::move(other.context_);
        tensors_ = std::move(other.tensors_);
        name_to_index_ = std::move(other.name_to_index_);
        device_buffers_ = std::move(other.device_buffers_);
        device_buffer_bytes_ = std::move(other.device_buffer_bytes_);
        stream_ = other.stream_;
        logger_ = other.logger_;
        other.stream_ = nullptr;
    }
    return *this;
}

TrtEngine::~TrtEngine() {
    for (void* p : device_buffers_) {
        if (p)
            cudaFree(p);
    }
    if (stream_) {
        cudaStreamDestroy(stream_);
    }
    // unique_ptrs handle context_ / engine_ / runtime_ in destruction order.
}

// ---- Introspection ---------------------------------------------------------

const TensorIo& TrtEngine::tensor(std::string_view name) const {
    return tensors_[buffer_index_(name)];
}

// ---- Shape configuration ---------------------------------------------------

void TrtEngine::set_input_shape(std::string_view name, nvinfer1::Dims shape) {
    const std::string key{name};
    if (!context_->setInputShape(key.c_str(), shape)) {
        throw TrtException("setInputShape rejected " + key + " = " + shape_to_string(shape) +
                           "; not in optimization profile");
    }
}

// ---- Buffer access ---------------------------------------------------------

void* TrtEngine::device_buffer(std::string_view name) {
    return device_buffers_[buffer_index_(name)];
}

const void* TrtEngine::device_buffer(std::string_view name) const {
    return device_buffers_[buffer_index_(name)];
}

std::size_t TrtEngine::buffer_bytes(std::string_view name) const {
    const std::string key{name};
    // Use the context's view of the shape: for inputs it reflects the most
    // recent setInputShape; for outputs it's resolved from those inputs.
    const auto dims = context_->getTensorShape(key.c_str());
    const auto& io = tensors_[buffer_index_(name)];
    return volume(dims) * io.elem_size;
}

// ---- Execution -------------------------------------------------------------

void TrtEngine::enqueue(const std::unordered_map<std::string, std::span<const std::byte>>& host_in,
                        const std::unordered_map<std::string, std::span<std::byte>>& host_out) {
    // 1) Bind every I/O tensor's device address. enqueueV3 requires this even
    //    if the address didn't change; we do it unconditionally so callers
    //    don't accidentally inherit a stale binding from a prior context use.
    for (std::size_t i = 0; i < tensors_.size(); ++i) {
        if (!context_->setTensorAddress(tensors_[i].name.c_str(), device_buffers_[i])) {
            throw TrtException("setTensorAddress failed for " + tensors_[i].name);
        }
    }

    // 2) Async H→D for every supplied input. The caller is responsible for
    //    keeping the host buffers alive until sync().
    for (const auto& [name, src] : host_in) {
        const auto idx = buffer_index_(name);
        if (!tensors_[idx].is_input) {
            throw TrtException("enqueue: '" + name + "' is not an input tensor");
        }
        const auto need = buffer_bytes(name);
        if (src.size() != need) {
            throw TrtException("enqueue: input '" + name + "' size mismatch (got " +
                               std::to_string(src.size()) + ", need " + std::to_string(need) + ")");
        }
        check_cuda(cudaMemcpyAsync(device_buffers_[idx], src.data(), src.size(),
                                   cudaMemcpyHostToDevice, stream_),
                   "cudaMemcpyAsync H->D");
    }

    // 3) Run inference. enqueueV3 returns false on a configuration error
    //    (missing input shape, unbound output, etc.).
    if (!context_->enqueueV3(stream_)) {
        throw TrtException("enqueueV3 failed (inputs unbound or shape unset?)");
    }

    // 4) Async D→H for every requested output.
    for (const auto& [name, dst] : host_out) {
        const auto idx = buffer_index_(name);
        if (tensors_[idx].is_input) {
            throw TrtException("enqueue: '" + name + "' is an input, not an output");
        }
        const auto need = buffer_bytes(name);
        if (dst.size() != need) {
            throw TrtException("enqueue: output '" + name + "' size mismatch (got " +
                               std::to_string(dst.size()) + ", need " + std::to_string(need) + ")");
        }
        check_cuda(cudaMemcpyAsync(dst.data(), device_buffers_[idx], dst.size(),
                                   cudaMemcpyDeviceToHost, stream_),
                   "cudaMemcpyAsync D->H");
    }
}

void TrtEngine::sync() {
    check_cuda(cudaStreamSynchronize(stream_), "cudaStreamSynchronize");
}

// ---- Internal helpers ------------------------------------------------------

void TrtEngine::allocate_buffers_() {
    device_buffers_.assign(tensors_.size(), nullptr);
    device_buffer_bytes_.assign(tensors_.size(), 0);

    // Buffers are sized to the optimization profile's MAX dims so a single
    // allocation works across every shape the engine accepts. For static
    // engines the static shape itself is the max.
    constexpr int profile_idx = 0;

    for (std::size_t i = 0; i < tensors_.size(); ++i) {
        const auto& io = tensors_[i];

        nvinfer1::Dims max_shape = io.shape;
        const bool has_dynamic =
            std::any_of(io.shape.d, io.shape.d + io.shape.nbDims, [](int64_t x) { return x < 0; });

        if (has_dynamic && io.is_input) {
            max_shape = engine_->getProfileShape(io.name.c_str(), profile_idx,
                                                 nvinfer1::OptProfileSelector::kMAX);
        }
        // Output shapes can also be dynamic; resolve them by setting each
        // dynamic input to its kMAX and asking the context to infer outputs.
        // We do that pass once below, after every input is sized.
        if (has_dynamic && !io.is_input) {
            // Defer: handled in second pass.
        } else {
            const auto bytes = volume(max_shape) * io.elem_size;
            if (bytes == 0) {
                throw TrtException("zero-byte buffer for tensor " + io.name +
                                   " (shape=" + shape_to_string(max_shape) + ")");
            }
            check_cuda(cudaMalloc(&device_buffers_[i], bytes), "cudaMalloc");
            device_buffer_bytes_[i] = bytes;
        }
    }

    // Second pass for dynamic outputs: prime the context with kMAX inputs so
    // the engine can resolve every output shape, then allocate.
    bool any_dynamic_outputs = false;
    for (const auto& io : tensors_) {
        if (!io.is_input && std::any_of(io.shape.d, io.shape.d + io.shape.nbDims,
                                        [](int64_t x) { return x < 0; })) {
            any_dynamic_outputs = true;
            break;
        }
    }
    if (!any_dynamic_outputs)
        return;

    for (const auto& io : tensors_) {
        if (!io.is_input)
            continue;
        const bool has_dynamic =
            std::any_of(io.shape.d, io.shape.d + io.shape.nbDims, [](int64_t x) { return x < 0; });
        if (!has_dynamic)
            continue;
        const auto kmax = engine_->getProfileShape(io.name.c_str(), profile_idx,
                                                   nvinfer1::OptProfileSelector::kMAX);
        if (!context_->setInputShape(io.name.c_str(), kmax)) {
            throw TrtException("priming setInputShape failed for " + io.name);
        }
    }

    for (std::size_t i = 0; i < tensors_.size(); ++i) {
        if (device_buffers_[i])
            continue;  // already allocated
        const auto& io = tensors_[i];
        const auto dims = context_->getTensorShape(io.name.c_str());
        const auto bytes = volume(dims) * io.elem_size;
        if (bytes == 0) {
            throw TrtException("could not resolve dynamic output shape for " + io.name);
        }
        check_cuda(cudaMalloc(&device_buffers_[i], bytes), "cudaMalloc");
        device_buffer_bytes_[i] = bytes;
    }
}

std::size_t TrtEngine::buffer_index_(std::string_view name) const {
    const auto it = name_to_index_.find(std::string{name});
    if (it == name_to_index_.end()) {
        throw TrtException("unknown tensor: " + std::string{name});
    }
    return it->second;
}

}  // namespace gate::inference
