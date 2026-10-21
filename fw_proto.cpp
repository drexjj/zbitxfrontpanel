/*
 * fw_proto.cpp — front-panel firmware update protocol (see fw_proto.h).
 *
 * Plain C++, no Arduino calls, so it can be compiled and tested on a PC.
 *
 * Interrupt safety: the I2C handlers run in interrupt context. The receive
 * handler only copies a frame into a buffer the main loop is not using (it
 * drops the frame if the previous one hasn't been processed yet; the Pi
 * waits for an answer before sending the next, so that only happens on
 * errors). Status strings are written by the main loop into whichever of two
 * buffers the read handler is NOT returning, then switched over with a
 * single int store.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "fw_proto.h"

static uint8_t frame[FWP_FRAME_MAX];
static volatile int frame_len = 0;
static volatile bool frame_ready = false;
static volatile bool busy = false;       /* a frame is waiting: answer FWBUSY */

static char status_buf[2][FWP_STATUS_MAX];
static volatile int status_idx = -1;     /* -1: no status, send panel data */
static volatile bool status_once = false;/* clear after one read (FWCAP etc.) */

static bool query = false;
static bool active = false;              /* an image is being received */
static bool finished = false;            /* done or failed: waiting to reboot */
static uint32_t img_size = 0;
static uint32_t received = 0;
static unsigned long last_frame_ms = 0;
static char err_text[40] = "";

uint16_t fwp_crc16(const uint8_t *data, int len)
{
	uint16_t crc = 0xFFFF;               /* CRC-16/CCITT-FALSE */
	for (int i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int b = 0; b < 8; b++)
			crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	}
	return crc;
}

static uint32_t rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void set_status(bool once, const char *fmt, ...)
{
	int next = (status_idx == 0) ? 1 : 0;
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(status_buf[next], FWP_STATUS_MAX, fmt, ap);
	va_end(ap);
	status_once = once;
	status_idx = next;
	busy = false;
}

static enum fwp_event fail(const char *why)
{
	active = false;
	finished = true;
	snprintf(err_text, sizeof(err_text), "%s", why);
	set_status(false, "FWERR %s", why);
	return FWP_EV_FAILED;
}

/* ---- interrupt side ------------------------------------------------------ */

void fwp_isr_receive(const uint8_t *data, int len)
{
	if (finished || frame_ready || len <= 0 || len > FWP_FRAME_MAX)
		return;
	memcpy(frame, data, len);
	frame_len = len;
	frame_ready = true;
	busy = true;
}

const char *fwp_isr_status(void)
{
	if (busy)
		return "FWBUSY";
	int i = status_idx;
	if (i < 0)
		return 0;
	if (status_once && !active && !finished) {
		status_idx = -1;
		status_once = false;
	}
	return status_buf[i];
}

/* ---- main-loop side ------------------------------------------------------ */

void fwp_query(void)
{
	query = true;
}

bool fwp_active(void) { return active; }
uint32_t fwp_received(void) { return received; }
uint32_t fwp_size(void) { return img_size; }
const char *fwp_error(void) { return err_text; }

enum fwp_event fwp_service(const struct fwp_ops *ops, unsigned long now_ms)
{
	if (query) {
		query = false;
		if (!active && !finished)
			set_status(true, "FWCAP %s %lu %lu", ops->fw_id, (unsigned long)ops->area_size,
				(unsigned long)(ops->free_heap ? ops->free_heap() : 0));
	}

	if (!frame_ready) {
		if (active && now_ms - last_frame_ms > FWP_TIMEOUT_MS)
			return fail("timeout");
		return FWP_EV_NONE;
	}

	uint8_t f[FWP_FRAME_MAX];
	int len = frame_len;
	memcpy(f, frame, len);
	frame_ready = false;
	last_frame_ms = now_ms;

	if (len < 3 || fwp_crc16(f, len - 2) != (uint16_t)(f[len - 2] | (f[len - 1] << 8))) {
		set_status(!active, "FWNAK %lu crc", (unsigned long)received);
		return FWP_EV_NONE;
	}
	int body = len - 2;

	switch (f[0]) {
	case 'B': {
		if (body != 1 + 4 + 32) {
			set_status(!active, "FWNAK %lu len", (unsigned long)received);
			return FWP_EV_NONE;
		}
		if (active) {   /* repeated: our earlier answer was lost */
			set_status(false, "FWACK %lu", (unsigned long)received);
			return FWP_EV_NONE;
		}
		uint32_t size = rd32(f + 1);
		char md5[33];
		memcpy(md5, f + 5, 32);
		md5[32] = 0;
		int e = 0;
		if (ops->area_size == 0 || size == 0 || size > ops->area_size) {
			set_status(true, "FWERR noroom %lu", (unsigned long)ops->area_size);
			return FWP_EV_NONE;
		}
		if (!ops->begin(size, md5, &e)) {
			set_status(true, "FWERR begin %d", e);
			return FWP_EV_NONE;
		}
		active = true;
		img_size = size;
		received = 0;
		err_text[0] = 0;
		set_status(false, "FWACK 0");
		return FWP_EV_STARTED;
	}
	case 'D': {
		if (!active) {
			set_status(true, "FWERR notstarted");
			return FWP_EV_NONE;
		}
		uint32_t off = rd32(f + 1);
		int n = f[5];
		if (n == 0 || n > FWP_CHUNK || body != 6 + n) {
			set_status(false, "FWNAK %lu len", (unsigned long)received);
			return FWP_EV_NONE;
		}
		if (off == received) {
			if (received + n > img_size)
				return fail("toolong");
			if (!ops->write(f + 6, n))
				return fail("write");
			received += n;
			set_status(false, "FWACK %lu", (unsigned long)received);
			return FWP_EV_PROGRESS;
		}
		if (off + n <= received) {   /* repeated chunk: already stored */
			set_status(false, "FWACK %lu", (unsigned long)received);
			return FWP_EV_NONE;
		}
		set_status(false, "FWNAK %lu order", (unsigned long)received);
		return FWP_EV_NONE;
	}
	case 'E': {
		if (!active) {
			set_status(true, "FWERR notstarted");
			return FWP_EV_NONE;
		}
		if (received != img_size) {
			set_status(false, "FWNAK %lu short", (unsigned long)received);
			return FWP_EV_NONE;
		}
		int e = 0;
		if (!ops->end(&e)) {
			char why[24];
			snprintf(why, sizeof(why), "verify %d", e);
			return fail(why);
		}
		active = false;
		finished = true;
		set_status(false, "FWDONE");
		return FWP_EV_DONE;
	}
	case 'X':
		if (!active) {
			set_status(true, "FWERR notstarted");
			return FWP_EV_NONE;
		}
		return fail("aborted");
	default:
		set_status(!active, "FWNAK %lu type", (unsigned long)received);
		return FWP_EV_NONE;
	}
}
