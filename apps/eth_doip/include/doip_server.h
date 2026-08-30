/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2E - DoIP socket server (UDP discovery on :13400, TCP diagnostics on
 * :13400). Thin transport wrapper around doip.c.
 */

#ifndef ETH_DOIP_DOIP_SERVER_H_
#define ETH_DOIP_DOIP_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the UDP and TCP DoIP server threads.
 *
 * Safe to call before the network interface is up; the threads block in
 * recvfrom()/accept() until traffic arrives.
 *
 * @retval 0   threads started
 * @retval <0  errno
 */
int doip_server_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ETH_DOIP_DOIP_SERVER_H_ */
