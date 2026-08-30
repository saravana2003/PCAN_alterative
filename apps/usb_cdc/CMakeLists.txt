# SPDX-License-Identifier: Apache-2.0
#
# Phase 2C: USB CDC-ACM device + CLP framing protocol (build-only, no board).
# Board target: ek_ra8d1. The CDC-ACM DT node is added by
# boards/ek_ra8d1.overlay (auto-applied by filename).

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(usb_cdc)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c
  src/clp_proto.c
  src/usb_link.c
  src/usbd_ctx.c
)
