# opencv-cuda-shim.cmake — pre-populate legacy FindCUDA variables so
# OpenCVConfig.cmake (from the CUDA source build) loads under CMake 3.27+.
#
# OpenCV built with CUDA calls the legacy FindCUDA module unconditionally
# (`find_host_package(CUDA REQUIRED)`). Policy CMP0146 (NEW default since
# 3.27) removes that module, and policy pushes don't propagate through
# vcpkg's find_package wrapper — so we source the variables the config
# actually reads from the modern CUDA::* imported targets instead. The
# `if(NOT CUDA_FOUND)` check in OpenCV's config then skips the legacy
# lookup entirely.
#
# include() this before find_package(OpenCV ...) in any directory scope
# that needs OpenCV components (server/inference, server/vision's camera
# target). Variables are directory-scoped, hence a shared include rather
# than doing it once at the root.

if(NOT CUDA_FOUND)
    set(CUDA_FOUND          TRUE)
    set(CUDA_VERSION_STRING "${CUDAToolkit_VERSION_MAJOR}.${CUDAToolkit_VERSION_MINOR}")
    set(CUDA_VERSION        "${CUDA_VERSION_STRING}")
    get_target_property(_cudart_lib  CUDA::cudart   IMPORTED_LOCATION)
    get_target_property(_nppc_lib    CUDA::nppc     IMPORTED_LOCATION)
    get_target_property(_nppial_lib  CUDA::nppial   IMPORTED_LOCATION)
    get_target_property(_npps_lib    CUDA::npps     IMPORTED_LOCATION)
    get_target_property(_cublas_lib  CUDA::cublas   IMPORTED_LOCATION)
    get_target_property(_cufft_lib   CUDA::cufft    IMPORTED_LOCATION)
    set(CUDA_LIBRARIES        "${_cudart_lib}")
    set(CUDA_nppc_LIBRARY     "${_nppc_lib}")
    set(CUDA_nppi_LIBRARY     "${_nppial_lib}")
    set(CUDA_npps_LIBRARY     "${_npps_lib}")
    set(CUDA_CUBLAS_LIBRARIES "${_cublas_lib}")
    set(CUDA_CUFFT_LIBRARIES  "${_cufft_lib}")
    # OpenCV's config also calls FindCUDA's helper macro find_cuda_helper_libs
    # for nppc/nppi/npps. Since we never loaded FindCUDA, the macro is unknown.
    # Provide a no-op shim — the libraries are already populated above.
    if(NOT COMMAND find_cuda_helper_libs)
        macro(find_cuda_helper_libs)
        endmacro()
    endif()
endif()
