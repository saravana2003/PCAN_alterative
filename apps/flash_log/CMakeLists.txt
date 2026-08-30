# SPDX-License-Identifier: Apache-2.0
#
# Phase 2D: littlefs on the Octo-SPI flash + batched CAN-frame logger
# (build-only, no board). Board target: ek_ra8d1. The littlefs partition and
# fstab node are defined in boards/ek_ra8d1.overlay (auto-applied by filename).

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(flash_log)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c
  src/flash_log.c
)
