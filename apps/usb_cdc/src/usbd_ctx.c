/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Phase 2C - USB device context. Build-only; no board attached.
 *
 * Templated from zephyr/samples/subsys/usb/common/sample_usbd_init.c.
 *
 * VID/PID: the Zephyr project test values (VID 0x2fe3 / PID 0x0001).
 * PLACEHOLDER - must be replaced with an allocated VID/PID before this
 * firmware is distributed outside the bench.
 */

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

#include "usbd_ctx.h"

LOG_MODULE_REGISTER(usbd_ctx, LOG_LEVEL_INF);

#define USB_CDC_VID          0x2fe3   /* PLACEHOLDER - Zephyr project test VID */
#define USB_CDC_PID          0x0001   /* PLACEHOLDER                          */
#define USB_CDC_MANUFACTURER "BITS WILP ESD"
#define USB_CDC_PRODUCT      "EK-RA8D1 CAN Logger"
#define USB_CDC_MAX_POWER    125      /* in 2 mA units => 250 mA             */

USBD_DEVICE_DEFINE(usb_cdc_usbd,
		   DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
		   USB_CDC_VID, USB_CDC_PID);

USBD_DESC_LANG_DEFINE(usb_cdc_lang);
USBD_DESC_MANUFACTURER_DEFINE(usb_cdc_mfr, USB_CDC_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(usb_cdc_prod, USB_CDC_PRODUCT);
IF_ENABLED(CONFIG_HWINFO, (USBD_DESC_SERIAL_NUMBER_DEFINE(usb_cdc_sn)));

USBD_DESC_CONFIG_DEFINE(usb_cdc_fs_desc, "CLP FS Configuration");
USBD_DESC_CONFIG_DEFINE(usb_cdc_hs_desc, "CLP HS Configuration");

static const uint8_t usb_cdc_attributes = USB_SCD_SELF_POWERED;

USBD_CONFIGURATION_DEFINE(usb_cdc_fs_config, usb_cdc_attributes,
			  USB_CDC_MAX_POWER, &usb_cdc_fs_desc);
USBD_CONFIGURATION_DEFINE(usb_cdc_hs_config, usb_cdc_attributes,
			  USB_CDC_MAX_POWER, &usb_cdc_hs_desc);

/* Do not register the USB DFU class DFU-mode instance. */
static const char *const usb_cdc_blocklist[] = { "dfu_dfu", NULL };

/*
 * CDC-ACM presents multiple interfaces and carries an Interface Association
 * Descriptor, so the device-level class triple must advertise MISC/0x02/0x01
 * (same rule the sample applies).
 */
static void usb_cdc_fix_code_triple(struct usbd_context *ctx, enum usbd_speed speed)
{
	if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS)) {
		usbd_device_set_code_triple(ctx, speed,
					    USB_BCC_MISCELLANEOUS, 0x02, 0x01);
	} else {
		usbd_device_set_code_triple(ctx, speed, 0, 0, 0);
	}
}

static int usb_cdc_add_descriptors(void)
{
	int err;

	err = usbd_add_descriptor(&usb_cdc_usbd, &usb_cdc_lang);
	if (err == 0) {
		err = usbd_add_descriptor(&usb_cdc_usbd, &usb_cdc_mfr);
	}
	if (err == 0) {
		err = usbd_add_descriptor(&usb_cdc_usbd, &usb_cdc_prod);
	}
	IF_ENABLED(CONFIG_HWINFO, (
		if (err == 0) {
			err = usbd_add_descriptor(&usb_cdc_usbd, &usb_cdc_sn);
		}
	))
	return err;
}

static int usb_cdc_add_speed_config(enum usbd_speed speed,
				    struct usbd_config_node *config)
{
	int err;

	err = usbd_add_configuration(&usb_cdc_usbd, speed, config);
	if (err) {
		LOG_ERR("add configuration (speed %d) failed: %d", speed, err);
		return err;
	}

	err = usbd_register_all_classes(&usb_cdc_usbd, speed, 1,
				       usb_cdc_blocklist);
	if (err) {
		LOG_ERR("register classes (speed %d) failed: %d", speed, err);
		return err;
	}

	usb_cdc_fix_code_triple(&usb_cdc_usbd, speed);
	return 0;
}

struct usbd_context *usb_cdc_init_usbd(usbd_msg_cb_t msg_cb)
{
	int err;

	err = usb_cdc_add_descriptors();
	if (err) {
		LOG_ERR("add descriptors failed: %d", err);
		return NULL;
	}

	if (USBD_SUPPORTS_HIGH_SPEED &&
	    usbd_caps_speed(&usb_cdc_usbd) == USBD_SPEED_HS) {
		if (usb_cdc_add_speed_config(USBD_SPEED_HS,
					    &usb_cdc_hs_config) != 0) {
			return NULL;
		}
	}

	if (usb_cdc_add_speed_config(USBD_SPEED_FS, &usb_cdc_fs_config) != 0) {
		return NULL;
	}

	usbd_self_powered(&usb_cdc_usbd,
			  usb_cdc_attributes & USB_SCD_SELF_POWERED);

	if (msg_cb != NULL) {
		err = usbd_msg_register_cb(&usb_cdc_usbd, msg_cb);
		if (err) {
			LOG_ERR("msg cb register failed: %d", err);
			return NULL;
		}
	}

	err = usbd_init(&usb_cdc_usbd);
	if (err) {
		LOG_ERR("usbd_init failed: %d", err);
		return NULL;
	}

	return &usb_cdc_usbd;
}
