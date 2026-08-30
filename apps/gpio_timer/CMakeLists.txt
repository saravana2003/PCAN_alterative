# SPDX-License-Identifier: Apache-2.0
#
# Phase 2B: GPIO buttons/LEDs + periodic timer module (build-only, no board).
# Board target: ek_ra8d1. Uses only stock ek_ra8d1.dts nodes (gpio-keys,
# gpio-leds, port_irq12/13) - no devicetree overlay required.

cmake_minimum_required(VERSION 3.28.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gpio_timer)

target_include_directories(app PRIVATE include)
target_sources(app PRIVATE
  src/main.c
  src/io_control.c
  src/log_timer.c
)
