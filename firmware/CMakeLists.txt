# gate-firmware — top-level ESP-IDF project.
#
# Targets the ESP32-S3 (per ADR-005). Build with:
#
#     . $IDF_PATH/export.sh
#     idf.py -B build set-target esp32s3
#     idf.py -B build build
#
# Local development assumes an ESP-IDF v6.0+ checkout at $IDF_PATH (the
# install path produced by `~/esp/esp-idf/install.sh`). Phase 4.4.5 will
# add a CI workflow that does the same setup in GitHub Actions.

cmake_minimum_required(VERSION 3.16)

# ESP-IDF's top-level project hook discovers components, hands the cross
# toolchain to CMake, generates the partition table image, and so on.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Pull in our custom components from `components/` (the default ESP-IDF
# search path) plus any future third-party components in `managed_components/`.
project(gate_firmware
    VERSION 0.0.1
    DESCRIPTION "Gate-side ESP32-S3 firmware"
    LANGUAGES C CXX
)
