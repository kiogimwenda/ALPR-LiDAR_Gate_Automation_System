# CI/CD Documentation

## Current Workflows

| Workflow | Trigger | Description |
|----------|---------|-------------|
| `build.yml` | push/PR to main, develop | Matrix build: GCC 13/14, Clang 17/18 × Debug/Release (CPU-only) |
| `lint.yml` | push/PR to main, develop | clang-format check, clang-tidy, cppcheck |
| `test.yml` | push/PR to main, develop | Unit tests under ASan and UBSan |
| `codeql.yml` | push to main, PRs, weekly | GitHub CodeQL C++ security analysis |
| `commitlint.yml` | PRs to main, develop | Enforce Conventional Commits on PR titles and commit messages |

## GPU CI (TODO)

The current CI matrix runs CPU-only builds (`-DENABLE_GPU=OFF`) on GitHub-hosted
`ubuntu-24.04` runners, which do not have GPUs.

To run GPU tests (TensorRT inference, CUDA kernels, OpenCV CUDA pipelines), a
**self-hosted runner** with an NVIDIA GPU is required. Setup steps:

1. Provision a machine with an NVIDIA GPU (RTX 4060 or better) running Ubuntu 24.04
2. Install the CUDA toolkit, cuDNN, TensorRT, and OpenCV-with-CUDA
3. Register as a GitHub Actions self-hosted runner per
   https://docs.github.com/en/actions/hosting-your-own-runners
4. Add a `gpu-test.yml` workflow targeting `runs-on: self-hosted` with the `gpu` label
5. Run the full test suite including TensorRT engine build + inference benchmarks

This will be set up in a later phase once the inference pipeline is implemented.
