# SPDX-License-Identifier: Apache-2.0
#
# Phase 2E: Ethernet static IP + DoIP-style log-retrieval skeleton
# (build-only, no board). Board target: ek_ra8d1. The on-chip Ethernet MAC
# (&eth), MDIO (&mdio) and PHY (ethernet-phy@5) are already status=okay in the
# stock board dts - no overlay needed.

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(eth_doip)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c
  src/doip.c
  src/doip_server.c
)
