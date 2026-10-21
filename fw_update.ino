/*
 * fw_update.ino — receive new front-panel firmware from the Pi over I2C.
 *
 * Protocol: see fw_proto.h. This file connects it to the hardware:
 *   - the image is stored with the core's Updater (LittleFS file
 *     "firmware.bin", MD5-checked), and on success the core's OTA loader
 *     installs it on the next boot;
 *   - while an image is coming in, fw_update_screen() takes over the display
 *     (touch and encoder ignored) and ends with a reboot either way.
 *
 * Needs a build with an update area, e.g. "2MB (Sketch: 1536KB, FS: 512KB)"
 * (see build.sh). A build without one still answers FWQUERY, with size 0,
 * so the Pi can say the panel has to be flashed by USB once.
 */

#include <Updater.h>
#include "fw_proto.h"

// The linker's update-area bounds (equal when the build has no FS area).
extern uint8_t _FS_start;
extern uint8_t _FS_end;

// Identifies a zBitx panel image. The Pi's flashing tool refuses images that
// don't contain "ZBITX-PANEL-FW", so this string must stay in the firmware.
const char fw_id[] = "ZBITX-PANEL-FW " ZBITX_PANEL_VERSION;

// The Updater allocates a 4 KB buffer and LittleFS its own caches; with the
// panel's RAM nearly full, an allocation failure would crash rather than
// fail cleanly, so check first.
#define FWU_MIN_HEAP 7168
#define FWU_ERR_LOWMEM 98

static uint32_t fwu_free_heap(){
	return rp2040.getFreeHeap();
}

static bool fwu_begin(uint32_t size, const char *md5hex, int *err){
	if (rp2040.getFreeHeap() < FWU_MIN_HEAP){
		*err = FWU_ERR_LOWMEM;
		return false;
	}
	if (!Update.begin(size)){
		*err = Update.getError();
		return false;
	}
	if (!Update.setMD5(md5hex)){
		*err = 99;
		return false;
	}
	return true;
}

static bool fwu_write(const uint8_t *data, int len){
	return Update.write((uint8_t *)data, len) == (size_t)len;
}

static bool fwu_end(int *err){
	if (Update.end())
		return true;
	*err = Update.getError();
	return false;
}

static struct fwp_ops fw_ops;

static void fw_ops_init(){
	if (fw_ops.fw_id)
		return;
	fw_ops.begin = fwu_begin;
	fw_ops.write = fwu_write;
	fw_ops.end = fwu_end;
	fw_ops.fw_id = fw_id;
	fw_ops.free_heap = fwu_free_heap;
	// Keep a margin for LittleFS's own blocks, so an image the Pi was told
	// fits really does fit.
	uint32_t area = (uint32_t)(&_FS_end - &_FS_start);
	fw_ops.area_size = area > 16384 ? area - 16384 : 0;
}

// Takes over the screen until the update ends, then reboots. Never returns.
static void fw_update_screen(){
	for (struct field *f = field_list; f->type != -1; f++)
		f->is_visible = false;
	field_set("TITLE", "Updating front panel", false);
	field_set("UPD_CUR", "Do not power off the radio.", false);
	field_set("UPD_NEW", "Receiving: 0%", false);
	field_set("UPD_STAT",
		"New panel firmware is coming from\n"
		"the radio. The panel restarts by\n"
		"itself when it is done.", false);
	field_show("TITLE", true);
	field_show("UPD_CUR", true);
	field_show("UPD_NEW", true);
	field_show("UPD_STAT", true);
	screen_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_BACKGROUND_COLOR);
	screen_fill_rect(0, 30, SCREEN_WIDTH, 1, TFT_WHITE);
	f_selected = NULL;
	in_dialog = true;
	field_draw_all(false);

	int shown_pct = 0;
	unsigned long reboot_at = 0;
	while (1){
		now = millis();
		enum fwp_event ev = fwp_service(&fw_ops, now);
		if (ev == FWP_EV_PROGRESS && fwp_size()){
			int pct = (int)((100ULL * fwp_received()) / fwp_size());
			if (pct != shown_pct){
				char b[64];
				shown_pct = pct;
				snprintf(b, sizeof(b), "Receiving: %d%% (%lu of %lu KB)", pct,
					(unsigned long)(fwp_received() / 1024),
					(unsigned long)(fwp_size() / 1024));
				field_set("UPD_NEW", b, false);
			}
		}
		else if (ev == FWP_EV_DONE){
			field_set("UPD_NEW", "Received and checked.", false);
			field_set("UPD_STAT",
				"Installing. The screen may freeze\n"
				"for up to a minute while the panel\n"
				"restarts. Don't power off.", false);
			reboot_at = now + 1500;   // time for the Pi to read FWDONE
		}
		else if (ev == FWP_EV_FAILED){
			char b[64];
			snprintf(b, sizeof(b), "Panel update failed (%s).", fwp_error());
			field_set("UPD_NEW", b, false);
			field_set("UPD_STAT",
				"Nothing was changed. The panel\n"
				"restarts with its current firmware.", false);
			reboot_at = now + 3000;
		}

		// The Pi sends no text while flashing; discard anything queued.
		while (q_length(&q_incoming))
			q_read(&q_incoming);
		field_draw_all(false);

		if (reboot_at && (long)(now - reboot_at) >= 0){
			delay(50);
			rp2040.reboot();
		}
		delay(1);
	}
}

/* Called from ui_slice(), the update lock screen and loop(): handles FWQUERY
 * and the first frame of an update, then hands over to fw_update_screen(). */
void fw_poll(){
	fw_ops_init();
	if (fwp_service(&fw_ops, millis()) == FWP_EV_STARTED || fwp_active())
		fw_update_screen();
}
