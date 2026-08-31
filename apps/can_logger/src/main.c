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

	/* Boot-time self-transmit: proves the TX path against the bench rig.
	 *
	 * BRING-UP CONSTRAINT (2026-08-31): our ground-truth analyser is a
	 * PCAN-USB, which is CLASSIC CAN ONLY - it cannot decode CAN-FD. An
	 * FD/BRS frame on that bus is seen as a malformed frame, so the
	 * analyser emits error frames; the RA CAN-FD controller then
	 * AUTO-RETRANSMITS (zephyr/include/zephyr/drivers/can.h:1316 - retry
	 * on lost arbitration or missing ACK is the default), and because
	 * can_renesas_ra_get_capabilities() does NOT advertise
	 * CAN_MODE_ONE_SHOT (only NORMAL|LOOPBACK|FD|MANUAL_RECOVERY, driver
	 * line 413) that retry CANNOT be disabled. One FD frame therefore
	 * becomes a sustained error storm -> PCAN-View shows BUSHEAVY
	 * immediately on connect, before any manual frame is sent.
	 *
	 * So the boot frame is CLASSIC (no CAN_FRAME_FDF / CAN_FRAME_BRS):
	 * classic frames are still valid on a CAN-FD controller in FD mode,
	 * and the analyser can decode and ACK them. Flip BOOT_TX_USE_FD to 1
	 * when a genuine CAN-FD analyser (e.g. PCAN-USB FD) is available -
	 * FD/BRS is a real project requirement that this rig cannot validate.
	 */
#define BOOT_TX_USE_FD 0

	struct canlog_frame tx = {
		.id = 0x123,
		.dlc = 8,
#if BOOT_TX_USE_FD
		.flags = CAN_FRAME_FDF | CAN_FRAME_BRS,
#else
		.flags = 0,
#endif
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
