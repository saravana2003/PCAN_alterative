/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2B - button-driven capture control + LED status indication for the
 * EK-RA8D1. Self-contained: this module knows nothing about CAN, flash or the
 * log timer. main.c wires it to the rest for the build/demo only.
 *
 * Buttons (stock ek_ra8d1.dts "buttons" gpio-keys node):
 *   S1 = button0 = P009 (ioport0 pin 9)  -> external IRQ13 (NVIC vector 89)
 *   S2 = button1 = P008 (ioport0 pin 8)  -> external IRQ12 (NVIC vector 88)
 *   Both are wired GPIO_PULL_UP | GPIO_ACTIVE_LOW (pressed = logic low).
 *   S1 => start capture, S2 => stop capture. Edge-triggered interrupts,
 *   handled via gpio_add_callback() -> callback in ISR context. This is a
 *   second concrete "Interrupt + NVIC" data point for the university report
 *   (the CAN RX path in apps/can_logger is the primary one).
 *
 * LEDs (stock ek_ra8d1.dts "leds" gpio-leds node, all GPIO_ACTIVE_HIGH):
 *   led1 = P600 (ioport6.0), led2 = P40E (ioport4.14), led3 = P107 (ioport1.7)
 *   Colour<->designator mapping is per the EK-RA8D1 user manual silkscreen and
 *   is NOT verifiable from the Zephyr tree (anti-hallucination rule 2). Adjust
 *   the LED_* selectors in io_control.c if the board disagrees.
 *   Indication: IDLE => green steady, LOGGING => red blinking.
 */

#ifndef GPIO_TIMER_IO_CONTROL_H_
#define GPIO_TIMER_IO_CONTROL_H_

#include <stdint.h>

enum capture_state {
	CAPTURE_IDLE = 0,
	CAPTURE_LOGGING = 1,
};

/**
 * @brief Capture state-change notification.
 *
 * Invoked from a button GPIO interrupt callback (ISR context) or from
 * io_control_request(). Keep it short and non-blocking.
 */
typedef void (*io_capture_cb_t)(enum capture_state new_state, void *user_data);

/**
 * @brief Configure the two user buttons (edge interrupts) and three user LEDs,
 *        set the initial IDLE indication.
 *
 * @retval 0 on success
 * @retval -ENODEV  a gpio-keys / gpio-leds port is not ready
 * @retval <0       errno from gpio_pin_configure()/gpio_pin_interrupt_configure()
 */
int io_control_init(void);

/** Register the state-change callback. Pass NULL to detach. Returns 0. */
int io_control_set_callback(io_capture_cb_t cb, void *user_data);

/** Current capture state. */
enum capture_state io_control_get_state(void);

/**
 * @brief Programmatic equivalent of a button press (same path the ISR takes).
 *        Useful for the build/demo and for a future GUI "start/stop" command.
 */
void io_control_request(enum capture_state desired);

#endif /* GPIO_TIMER_IO_CONTROL_H_ */
