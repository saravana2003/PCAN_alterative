/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2A demo entry point (build-only, no board).
 *
 * Exercises the self-contained CAN-FD module in can_iface.c so the whole
 * thing is compiled and linked. Nothing here runs without hardware; there is
 * no flashing in this phase.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "can_iface.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* RX handler - invoked from the CAN RX interrupt callback (ISR context). */
static void on_can_rx(const struct canlog_frame *frame, void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("CAN RX  id=0x%08x dlc=%u flags=0x%02x ts=%llu",
		frame->id, frame->dlc, frame->flags,
		(unsigned long long)frame->timestamp);
}

int main(void)
{
	int ret;

	ret = can_iface_init();
	if (ret != 0) {
		LOG_ERR("can_iface_init failed: %d", ret);
		return 0;
	}

	can_iface_set_rx_handler(on_can_rx, NULL);

	/* Example CAN-FD frame (BRS): standard ID 0x123, 8 data bytes. */
	struct canlog_frame tx = {
		.id = 0x123,
		.dlc = 8,
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
	};

	for (uint8_t i = 0; i < 8; i++) {
		tx.data[i] = i;
	}

	ret = can_iface_send(&tx, K_MSEC(100));
	if (ret != 0) {
		/* Expected off-bench: no ACK without a real CAN node. */
		LOG_WRN("can_iface_send returned %d (no bus attached)", ret);
	}

	return 0;
}
