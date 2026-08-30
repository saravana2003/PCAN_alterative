# SPDX-License-Identifier: Apache-2.0
#
# Phase 2A: CAN-FD interface module (build-only, no board).
# Board target: ek_ra8d1. The CANFD0 pin remap (CTX0=P203 / CRX0=P202) and
# bitrates are applied automatically from boards/ek_ra8d1.overlay.

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(can_logger)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c
  src/can_iface.c
)
