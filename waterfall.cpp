#include <TFT_eSPI.h>
#include "zbitx.h"

// The internal waterfall pixel buffer is always WF_BUF_W columns wide.
// When f->w > WF_BUF_W the draw routine scales up with nearest-neighbour.
// This keeps the RAM footprint fixed (240*150*2 = 72 KB) on the Pico W.
#define WF_BUF_W 240
#define WF_BUF_ROWS 150          // total rows in buffer (spectrum + scroll)
#define WF_SPECTRUM_ROWS 49      // rows used for the spectrum analyser overlay
#define WF_SCROLL_ROWS 95        // scrolling waterfall rows (49+95=144 drawn)

static uint16_t waterfall[WF_BUF_W * WF_BUF_ROWS];
static int bandwidth_stop, bandwidth_start, center_line, tx_pitch;

void waterfall_bandwidth(int start, int stop, int center, int red_line){
	if (start > stop){
		bandwidth_stop = start;
		bandwidth_start = stop;
	}
	else{
		bandwidth_start = start;
		bandwidth_stop = stop;
	}

	center_line = center;	
	tx_pitch = red_line;
}

void waterfall_init(){  
	memset(waterfall, 0, sizeof(waterfall));
	waterfall_bandwidth(-10, -40, -25, 0);
}

uint16_t inline heat_map(uint16_t v){
	uint16_t r, g, b;

	v *= 4;

	r = g = b = 0;

	if (v < 32){
		r = 0;
		g = 0;
		b = v;
	}
	else if (v < 64){
		r = 0;
		g = (v-32) << 7;
		b = 0x001f;
	}
	else if (v < 96){
		r = 0;
		g = 0x7E00;
		b = (96-v);
	}
	else if (v < 128){
		r = (v-96) << 11;
		g = 0x07e0;
		b = 0;
	}
	else {
		r = 0xf800;
		g = (160-v) << 6;
		b = 0;
	}
	return r + g + b;
}

// How many extra rows to pad above/below each column's segment so flat
// stretches of the trace don't collapse to a single pixel. 0 = old behaviour.
#define TRACE_THICKNESS 1

// How much brightness the spectrum overlay keeps from one update to the
// next (numerator/denominator, e.g. 3/4 = keeps 75%, ~4-5 frames to black).
// Set NUM == DEN to disable persistence and go back to a hard clear.
#define TRACE_PERSIST_NUM 3
#define TRACE_PERSIST_DEN 4

// Decay one RGB565 pixel toward black by TRACE_PERSIST_NUM/DEN.
static inline uint16_t fade565(uint16_t c){
	if (!c) return 0;
	uint16_t r = (c >> 11) & 0x1F;
	uint16_t g = (c >> 5) & 0x3F;
	uint16_t b = c & 0x1F;
	r = (r * TRACE_PERSIST_NUM) / TRACE_PERSIST_DEN;
	g = (g * TRACE_PERSIST_NUM) / TRACE_PERSIST_DEN;
	b = (b * TRACE_PERSIST_NUM) / TRACE_PERSIST_DEN;
	return (r << 11) | (g << 5) | b;
}

// Draw a vertical line into the internal buffer (always WF_BUF_W wide).
void waterfall_line(int x, int y1, int y2, int color){
	if (x < 0)
		return;
	if (x >= WF_BUF_W)
		return;
	if (y1 < 0)  y1 = 0;
	if (y2 < 0)  y2 = 0;
	if (y1 > 48) y1 = 48;
	if (y2 > 48) y2 = 48;
	if (y1 > y2){
		int t = y2;
		y2 = y1;
		y1 = t;
	}
	for (; y1 <= y2; y1++)
		waterfall[(y1 * WF_BUF_W) + x] = color;
}

// Update the internal buffer with a new 240-sample spectrum line.
// bins[] must always contain exactly WF_BUF_W (240) entries.
void waterfall_update(struct field *f, uint8_t *bins){

	// Fade the spectrum-analyser overlay rows instead of clearing them
	// outright, so recent traces persist for a few frames and decay --
	// gives the trace a phosphor-style afterglow instead of popping
	// between frames with nothing left behind.
	for (int i = 0; i < WF_BUF_W * WF_SPECTRUM_ROWS; i++)
		waterfall[i] = fade565(waterfall[i]);

	// Bandwidth / centre / TX-pitch indicator lines.
	// All x-positions are relative to the centre of the internal buffer.
	// These are redrawn at full brightness every frame (after the fade
	// above), so they stay crisp and don't pick up the trace's afterglow.
	for (int i = bandwidth_start; i < bandwidth_stop; i++)
		waterfall_line(WF_BUF_W/2 + i, 0, 48, TFT_DARKGREY);
	waterfall_line(WF_BUF_W/2, 0, 48, TFT_WHITE);
	waterfall_line(WF_BUF_W/2 + center_line, 0, 48, TFT_GREEN);
	if (!strcmp(field_get("MODE")->value, "FT8"))
		waterfall_line(WF_BUF_W/2 + tx_pitch, 0, 48, TFT_RED);

	// Draw the spectrum waveform line, padded by TRACE_THICKNESS rows
	// above/below each column's segment so flat stretches don't collapse
	// to a single pixel.
	int last_y = 48 - waterfall[0];
	for (int i = 1; i < WF_BUF_W; i++){
		int y_now  = 48 - (bins[i]   / 2);
		int y_last = 48 - (bins[i-1] / 2);
		if (y_now < 0) y_now = 0;
		int ylo = (y_last < y_now ? y_last : y_now) - TRACE_THICKNESS;
		int yhi = (y_last > y_now ? y_last : y_now) + TRACE_THICKNESS;
		waterfall_line(WF_BUF_W - i, ylo, yhi, TFT_YELLOW);
		last_y = y_now;
	}

	// Scroll the waterfall down by one row.
	uint16_t *w = waterfall + (WF_BUF_W * WF_SPECTRUM_ROWS);
	memmove(w + WF_BUF_W, w, (WF_BUF_W * WF_SCROLL_ROWS) * sizeof(uint16_t));

	// Write the newest heat-map row at the top of the scroll area.
	// bins[0] is the lowest frequency; write right-to-left so the display
	// matches the spectrum waveform orientation.
	for (int i = 0; i < WF_BUF_W; i++)
		w[WF_BUF_W - 1 - i] = heat_map((uint16_t)bins[i]);
}

void waterfall_draw(struct field *f){
	if (!strcmp(f->value, "OFF"))
		return;

	// Total rendered height: spectrum rows + scroll rows.
	const int draw_h = WF_SPECTRUM_ROWS + WF_SCROLL_ROWS; // = 144

	if (f->w <= WF_BUF_W){
		// Normal 240-pixel wide path — single fast blit.
		screen_bitblt(f->x, f->y, f->w, draw_h, waterfall);
	} else {
		// Wide mode (e.g. 480 px): scale the 240-wide buffer horizontally
		// using nearest-neighbour interpolation, one row at a time.
		// A 480-element stack buffer costs only 960 bytes.
		uint16_t row_buf[SCREEN_WIDTH];
		for (int row = 0; row < draw_h; row++){
			const uint16_t *src = waterfall + (row * WF_BUF_W);
			for (int x = 0; x < f->w; x++)
				row_buf[x] = src[(x * WF_BUF_W) / f->w];
			screen_bitblt(f->x, f->y + row, f->w, 1, row_buf);
		}
	}
}