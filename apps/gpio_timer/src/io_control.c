/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2B - buttons + LEDs. Build-only; no board attached, nothing is flashed.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "io_control.h"

LOG_MODULE_REGISTER(io_control, LOG_LEVEL_INF);

/* --- devicetree handles (stock ek_ra8d1.dts) --------------------------------
 * button0 = S1 = P009 (start), button1 = S2 = P008 (stop). Both children of
 * the "buttons" gpio-keys node and both on ioport0, so a single gpio_callback
 * registered on that port covers both edges.
 */
#define BTN_START_NODE  DT_NODELABEL(button0)
#define BTN_STOP_NODE   DT_NODELABEL(button1)

BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR(BTN_START_NODE, gpios),
			  DT_GPIO_CTLR(BTN_STOP_NODE, gpios)),
	     "both user buttons are expected to share one ioport");

static const struct gpio_dt_spec btn_start = GPIO_DT_SPEC_GET(BTN_START_NODE, gpios);
static const struct gpio_dt_spec btn_stop  = GPIO_DT_SPEC_GET(BTN_STOP_NODE, gpios);

/* LED colour selectors - see io_control.h note on the mapping caveat. */
#define LED_GREEN_NODE  DT_NODELABEL(led2)   /* P40E */
#define LED_RED_NODE    DT_NODELABEL(led3)   /* P107 */
#define LED_AUX_NODE    DT_NODELABEL(led1)   /* P600 - unused for now */

static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);

/* --- module state --------------------------------------------------------- */
static struct gpio_callback btn_cb_data;
static io_capture_cb_t user_cb;
static void *user_cb_data;
static enum capture_state state = CAPTURE_IDLE;

/* Software debounce: drop repeat edges on the same button inside this window.
 * The gpio-keys nodes have no debounce-interval-ms and this SoC's IRQ input
 * has only a digital noise filter, so bounce rejection is our job.
 */
#define DEBOUNCE_MS 40
static int64_t last_edge_ms;

/* LED blink while logging. This k_timer is a status-indicator detail only; the
 * report's "Timer" deliverable is the separate periodic tick in log_timer.c.
 */
static void blink_expiry(struct k_timer *t);
static K_TIMER_DEFINE(blink_timer, blink_expiry, NULL);

static void blink_expiry(struct k_timer *t)
{
	ARG_UNUSED(t);
	gpio_pin_toggle_dt(&led_red);
}

static void apply_indication(enum capture_state s)
{
	if (s == CAPTURE_LOGGING) {
		gpio_pin_set_dt(&led_green, 0);
		gpio_pin_set_dt(&led_red, 1);
		k_timer_start(&blink_timer, K_MSEC(250), K_MSEC(250));
	} else {
		k_timer_stop(&blink_timer);
		gpio_pin_set_dt(&led_red, 0);
		gpio_pin_set_dt(&led_green, 1);
	}
}

static void set_state(enum capture_state s)
{
	if (s == state) {
		return;
	}
	state = s;
	apply_indication(s);

	if (user_cb != NULL) {
		user_cb(s, user_cb_data);
	}
}

/*
 * Button GPIO interrupt callback - ISR context.
 *
 * "Interrupt + NVIC" (university report): P009/S1 is routed to RA external
 * interrupt IRQ13 (NVIC vector 89, priority 12) and P008/S2 to IRQ12 (vector
 * 88, priority 12); both port_irq nodes are enabled by boards/renesas/
 * ek_ra8d1/ek_ra8d1.dts. The Zephyr RA ioport driver
 * (drivers/gpio/gpio_renesas_ra_ioport.c) owns the ISR and fans out here via
 * gpio_fire_callbacks(). Keep this handler short and non-blocking.
 */
static void button_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);

	int64_t now = k_uptime_get();

	if ((now - last_edge_ms) < DEBOUNCE_MS) {
		return;
	}
	last_edge_ms = now;

	if (pins & BIT(btn_start.pin)) {
		set_state(CAPTURE_LOGGING);
	} else if (pins & BIT(btn_stop.pin)) {
		set_state(CAPTURE_IDLE);
	}
}

int io_control_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_red)) {
		return -ENODEV;
	}
	if (!gpio_is_ready_dt(&btn_start) || !gpio_is_ready_dt(&btn_stop)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_INACTIVE);
	}
	if (ret != 0) {
		LOG_ERR("LED configure failed: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&btn_start, GPIO_INPUT);
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&btn_stop, GPIO_INPUT);
	}
	if (ret != 0) {
		LOG_ERR("button configure failed: %d", ret);
		return ret;
	}

	/* GPIO_INT_EDGE_TO_ACTIVE = falling edge, since the buttons are
	 * GPIO_ACTIVE_LOW - i.e. fire on press, not release.
	 */
	ret = gpio_pin_interrupt_configure_dt(&btn_start, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret == 0) {
		ret = gpio_pin_interrupt_configure_dt(&btn_stop, GPIO_INT_EDGE_TO_ACTIVE);
	}
	if (ret != 0) {
		LOG_ERR("button IRQ configure failed: %d "
			"(is CONFIG_RENESAS_RA_EXTERNAL_INTERRUPT set?)", ret);
		return ret;
	}

	gpio_init_callback(&btn_cb_data, button_isr,
			   BIT(btn_start.pin) | BIT(btn_stop.pin));
	ret = gpio_add_callback_dt(&btn_start, &btn_cb_data);
	if (ret != 0) {
		LOG_ERR("gpio_add_callback failed: %d", ret);
		return ret;
	}

	state = CAPTURE_IDLE;
	apply_indication(state);
	LOG_INF("io_control ready: S1(P009)=start S2(P008)=stop, green=idle");
	return 0;
}

int io_control_set_callback(io_capture_cb_t cb, void *user_data)
{
	user_cb = cb;
	user_cb_data = user_data;
	return 0;
}

enum capture_state io_control_get_state(void)
{
	return state;
}

void io_control_request(enum capture_state desired)
{
	set_state(desired);
}
