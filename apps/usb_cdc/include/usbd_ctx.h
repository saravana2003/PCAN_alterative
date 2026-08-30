/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C - minimal USB device (stack "next") context for apps/usb_cdc.
 *
 * Templated from zephyr/samples/subsys/usb/common/sample_usbd_init.c, whose
 * header says to copy it into the application rather than depend on it.
 */

#ifndef USB_CDC_USBD_CTX_H_
#define USB_CDC_USBD_CTX_H_

#include <zephyr/usb/usbd.h>

/**
 * @brief Build and initialise the USB device context (one FS + one HS config,
 *        all compiled-in classes registered, message callback attached).
 *
 * Does NOT call usbd_enable() - the caller decides when, based on VBUS.
 *
 * @return the initialised context, or NULL on failure.
 */
struct usbd_context *usb_cdc_init_usbd(usbd_msg_cb_t msg_cb);

#endif /* USB_CDC_USBD_CTX_H_ */
