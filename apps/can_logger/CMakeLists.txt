# SPDX-License-Identifier: Apache-2.0
#
# can_logger — integrated CAN-FD capture + CLP host link.
# Board target: ek_ra8d1. The CANFD0 pin remap (CTX0=P704 / CRX0=P705) and
# bitrates are applied automatically from boards/ek_ra8d1.overlay.
#
# clp_proto.c / clp_proto.h are shared verbatim with apps/usb_cdc (the CLP
# protocol codec is transport-agnostic by design — see docs/clp_protocol.md).
# They are referenced in place rather than copied so there is a single source
# of truth; clp_uart.c is the UART-specific transport glue (cf. usb_cdc's
# usb_link.c for the CDC-ACM one).

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(can_logger)

target_include_directories(app PRIVATE
  include
  ${CMAKE_CURRENT_SOURCE_DIR}/../usb_cdc/include
)
target_sources(app PRIVATE
  src/main.c
  src/can_iface.c
  src/clp_uart.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../usb_cdc/src/clp_proto.c
)
