/*
 * fw_proto.h — front-panel firmware update over I2C (Pi -> Pico).
 *
 * The Pi and the Pico are joined only by I2C, so a new firmware image is sent
 * as a series of small checksummed frames. The Pico stores it in its LittleFS
 * area with the core's Updater, checks the MD5, and reboots; the core's OTA
 * loader (ota.o, linked into every arduino-pico build) then copies it into
 * place. This file is the protocol state machine only — no Arduino calls —
 * so the same code can be compiled and tested on a PC.
 *
 * ---- Pi -> Pico -------------------------------------------------------------
 * Text query, through the normal {LABEL value} channel:
 *   {FWQUERY 1}                 ask whether this firmware can be updated
 *
 * Binary frames, written with I2C command byte FWP_CMD (0x02) instead of '{':
 *   'B' size:u32 md5:32 hex chars             start, size of the (gzip) image
 *   'D' offset:u32 len:u8 data[len]           one chunk, len <= FWP_CHUNK
 *   'E'                                       end: verify MD5, install, reboot
 *   'X'                                       abort: reboot, nothing installed
 * Every frame ends with a CRC-16/CCITT (lo, hi) of the bytes before it,
 * starting at the type byte. Integers are little-endian.
 *
 * ---- Pico -> Pi (the normal length-prefixed I2C read) -----------------------
 *   FWCAP <id> <area> <heap>         answer to FWQUERY: bytes available for
 *                                    the image (0 = no update area) and free
 *                                    RAM (the Updater needs about 6 KB)
 *   FWBUSY                           frame received, not processed yet
 *   FWACK <n>                        everything up to byte n is stored
 *   FWNAK <n> <why>                  frame rejected; resend from byte n
 *   FWDONE                           image verified; the panel reboots now
 *   FWERR <why>                      update failed; the panel reboots
 * While an update is running, every read returns the current status, so a
 * failed read can simply be repeated.
 */
#ifndef FW_PROTO_H
#define FW_PROTO_H

#include <stdint.h>
#include <stdbool.h>

#define FWP_CMD        0x02   /* I2C command byte for binary frames */
#define FWP_CHUNK      192    /* max data bytes per 'D' frame */
#define FWP_FRAME_MAX  (1 + 4 + 1 + FWP_CHUNK + 2)
#define FWP_STATUS_MAX 64
#define FWP_TIMEOUT_MS 30000  /* give up if the Pi goes quiet this long */

/* What the firmware supplies: Updater calls on the Pico, fakes in tests. */
struct fwp_ops {
	bool (*begin)(uint32_t size, const char *md5hex, int *err);
	bool (*write)(const uint8_t *data, int len);
	bool (*end)(int *err);
	uint32_t (*free_heap)(void);  /* reported in FWCAP, for diagnosis */
	uint32_t area_size;     /* bytes available for the image (0 = none) */
	const char *fw_id;      /* "ZBITX-PANEL-FW 5.16a" */
};

/* What fwp_service() reports back to the caller. */
enum fwp_event {
	FWP_EV_NONE = 0,
	FWP_EV_STARTED,    /* 'B' accepted: show the update screen */
	FWP_EV_PROGRESS,   /* a chunk was stored */
	FWP_EV_DONE,       /* verified and committed: reboot shortly */
	FWP_EV_FAILED      /* failed or aborted: reboot */
};

uint16_t fwp_crc16(const uint8_t *data, int len);

/* Interrupt side (I2C receive / request handlers). */
void fwp_isr_receive(const uint8_t *data, int len);
/* Status to send for this read, or NULL to send the normal panel data. */
const char *fwp_isr_status(void);

/* Main-loop side. */
void fwp_query(void);                       /* {FWQUERY} arrived */
enum fwp_event fwp_service(const struct fwp_ops *ops, unsigned long now_ms);
bool fwp_active(void);
uint32_t fwp_received(void);
uint32_t fwp_size(void);
const char *fwp_error(void);

#endif
