/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2E - DoIP socket server. Build-only; no board, no live network.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include "doip.h"
#include "doip_server.h"

LOG_MODULE_REGISTER(doip_server, LOG_LEVEL_INF);

#define STACK_SIZE 3072
#define MSG_BUF    600   /* one DoIP message (header + bounded payload) */
#define RESP_BUF   600

K_THREAD_STACK_DEFINE(udp_stack, STACK_SIZE);
K_THREAD_STACK_DEFINE(tcp_stack, STACK_SIZE);
static struct k_thread udp_thread;
static struct k_thread tcp_thread;

/* -------------------------------------------------------------------------- */

static int recv_full(int fd, uint8_t *buf, size_t need)
{
	size_t got = 0;

	while (got < need) {
		ssize_t n = zsock_recv(fd, &buf[got], need - got, 0);

		if (n == 0) {
			return 0;              /* peer closed */
		}
		if (n < 0) {
			return -errno;
		}
		got += (size_t)n;
	}
	return (int)got;
}

static void handle_tcp_client(int fd)
{
	struct doip_conn conn;
	uint8_t msg[MSG_BUF];
	uint8_t resp[RESP_BUF];

	doip_conn_init(&conn);

	for (;;) {
		uint32_t plen;
		int rc = recv_full(fd, msg, DOIP_HEADER_SIZE);

		if (rc <= 0) {
			break;
		}

		plen = sys_get_be32(&msg[4]);
		if (plen > (uint32_t)(sizeof(msg) - DOIP_HEADER_SIZE)) {
			LOG_WRN("TCP: payload too large (%u) - dropping client", plen);
			break;
		}

		if (plen > 0U) {
			rc = recv_full(fd, &msg[DOIP_HEADER_SIZE], plen);
			if (rc <= 0) {
				break;
			}
		}

		rc = doip_handle_tcp(&conn, msg, DOIP_HEADER_SIZE + plen,
				     resp, sizeof(resp));
		if (rc > 0) {
			(void)zsock_send(fd, resp, (size_t)rc, 0);
		}
	}

	zsock_close(fd);
	LOG_INF("TCP: client disconnected");
}

static void tcp_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int srv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(DOIP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};

	if (srv < 0) {
		LOG_ERR("TCP: socket() failed: %d", -errno);
		return;
	}
	if (zsock_bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("TCP: bind() failed: %d", -errno);
		zsock_close(srv);
		return;
	}
	if (zsock_listen(srv, 1) < 0) {
		LOG_ERR("TCP: listen() failed: %d", -errno);
		zsock_close(srv);
		return;
	}

	LOG_INF("DoIP TCP server on :%d", DOIP_PORT);

	for (;;) {
		struct sockaddr_in peer;
		socklen_t plen = sizeof(peer);
		int fd = zsock_accept(srv, (struct sockaddr *)&peer, &plen);

		if (fd < 0) {
			LOG_ERR("TCP: accept() failed: %d", -errno);
			k_sleep(K_MSEC(200));
			continue;
		}
		LOG_INF("TCP: client connected");
		handle_tcp_client(fd);
	}
}

static void udp_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

	int fd = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(DOIP_PORT),
		.sin_addr.s_addr = htonl(INADDR_ANY),
	};
	uint8_t msg[MSG_BUF];
	uint8_t resp[RESP_BUF];

	if (fd < 0) {
		LOG_ERR("UDP: socket() failed: %d", -errno);
		return;
	}
	if (zsock_bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		LOG_ERR("UDP: bind() failed: %d", -errno);
		zsock_close(fd);
		return;
	}

	LOG_INF("DoIP UDP discovery on :%d", DOIP_PORT);

	for (;;) {
		struct sockaddr_in peer;
		socklen_t peerlen = sizeof(peer);
		ssize_t n = zsock_recvfrom(fd, msg, sizeof(msg), 0,
					   (struct sockaddr *)&peer, &peerlen);
		int rc;

		if (n < 0) {
			LOG_ERR("UDP: recvfrom() failed: %d", -errno);
			k_sleep(K_MSEC(200));
			continue;
		}

		rc = doip_handle_udp(msg, (size_t)n, resp, sizeof(resp));
		if (rc > 0) {
			(void)zsock_sendto(fd, resp, (size_t)rc, 0,
					   (struct sockaddr *)&peer, peerlen);
		}
	}
}

int doip_server_start(void)
{
	k_thread_create(&udp_thread, udp_stack, STACK_SIZE, udp_thread_fn,
			NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&udp_thread, "doip_udp");

	k_thread_create(&tcp_thread, tcp_stack, STACK_SIZE, tcp_thread_fn,
			NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&tcp_thread, "doip_tcp");

	return 0;
}
