/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2E demo entry point (build-only, no board, no live network).
 *
 * 1. A transport-free DoIP self-test: routing activation + a UDS
 *    ReadDataByIdentifier, checked end to end against doip.c.
 * 2. Report the statically configured IPv4 address and start the DoIP server
 *    threads. Nothing actually talks to a network in this phase.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "doip.h"
#include "doip_server.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static bool doip_selftest(void)
{
	struct doip_conn conn;
	uint8_t msg[128];
	uint8_t resp[256];
	int rc;

	doip_conn_init(&conn);

	/* --- Routing Activation Request (tester SA 0x0E80) --- */
	msg[0] = DOIP_PROTOCOL_VERSION;
	msg[1] = (uint8_t)~DOIP_PROTOCOL_VERSION;
	sys_put_be16(DOIP_PT_ROUTING_ACTIVATION_REQ, &msg[2]);
	sys_put_be32(7, &msg[4]);
	sys_put_be16(0x0E80, &msg[8]);   /* source address */
	msg[10] = 0x00;                  /* activation type: default */
	memset(&msg[11], 0, 4);          /* reserved */

	rc = doip_handle_tcp(&conn, msg, 8 + 7, resp, sizeof(resp));
	if (rc < (int)(DOIP_HEADER_SIZE + 9) ||
	    sys_get_be16(&resp[2]) != DOIP_PT_ROUTING_ACTIVATION_RESP ||
	    resp[8 + 4] != DOIP_RA_CODE_SUCCESS || !conn.routing_active) {
		LOG_ERR("selftest: routing activation failed (rc=%d)", rc);
		return false;
	}

	/* --- Diagnostic Message carrying UDS ReadDataByIdentifier(0xF190) --- */
	sys_put_be16(DOIP_PT_DIAG_MESSAGE, &msg[2]);
	sys_put_be32(4 + 3, &msg[4]);
	sys_put_be16(0x0E80, &msg[8]);                  /* SA */
	sys_put_be16(DOIP_ENTITY_LOGICAL_ADDR, &msg[10]); /* TA */
	msg[12] = UDS_SID_READ_DATA_BY_ID;
	sys_put_be16(0xF190, &msg[13]);

	rc = doip_handle_tcp(&conn, msg, 8 + 4 + 3, resp, sizeof(resp));
	if (rc <= 0) {
		LOG_ERR("selftest: diag message rc=%d", rc);
		return false;
	}

	/* Expect: 0x8002 positive ack, then 0x8001 with UDS 0x62 F1 90 + VIN. */
	if (sys_get_be16(&resp[2]) != DOIP_PT_DIAG_MESSAGE_POS_ACK) {
		LOG_ERR("selftest: missing diag positive ack");
		return false;
	}

	int off = DOIP_HEADER_SIZE + 5; /* header + SA/TA/ackcode */

	if (sys_get_be16(&resp[off + 2]) != DOIP_PT_DIAG_MESSAGE) {
		LOG_ERR("selftest: missing UDS diag message");
		return false;
	}
	const uint8_t *uds = &resp[off + DOIP_HEADER_SIZE + 4];

	if (uds[0] != (UDS_SID_READ_DATA_BY_ID | UDS_POSITIVE_RESPONSE_MASK) ||
	    sys_get_be16(&uds[1]) != 0xF190) {
		LOG_ERR("selftest: bad UDS response 0x%02x", uds[0]);
		return false;
	}

	LOG_INF("selftest PASS (routing activation + UDS RDBI/VIN)");
	return true;
}

static void report_ipv4(void)
{
	struct net_if *iface = net_if_get_default();

	/* The address is applied at boot by CONFIG_NET_CONFIG_SETTINGS from the
	 * CONFIG_NET_CONFIG_MY_IPV4_* values in prj.conf. On real hardware it
	 * only takes effect once the PHY link is up.
	 */
	LOG_INF("configured static IPv4: %s / %s  gw %s",
		CONFIG_NET_CONFIG_MY_IPV4_ADDR,
		CONFIG_NET_CONFIG_MY_IPV4_NETMASK,
		CONFIG_NET_CONFIG_MY_IPV4_GW);

	if (iface == NULL) {
		LOG_WRN("no default network interface");
	} else {
		LOG_INF("default iface %p, up=%d", (void *)iface,
			net_if_is_up(iface));
	}
}

int main(void)
{
	if (!doip_selftest()) {
		LOG_ERR("DoIP self-test FAILED");
		return 0;
	}

	/* Give CONFIG_NET_CONFIG_SETTINGS a moment to configure the iface. */
	k_sleep(K_MSEC(100));
	report_ipv4();

	int ret = doip_server_start();

	if (ret != 0) {
		LOG_ERR("doip_server_start failed: %d", ret);
		return 0;
	}

	LOG_INF("DoIP skeleton up on 192.168.1.50:%d (UDP discovery + TCP diag)",
		DOIP_PORT);
	return 0;
}
