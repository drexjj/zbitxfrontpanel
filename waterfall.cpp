#include "tft_ili9488.h"
#include "zbitx.h"

static uint16_t waterfall[240*100]; //Very Arbitrary!
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
  memset(waterfall, 0,  sizeof(waterfall));
	waterfall_bandwidth(-10,-40,-25, 0);
}

uint16_t inline heat_map(uint16_t v){
  uint16_t r, g, b;

  v *= 4;
  /*
  if (v > 100)    //we limit ourselves to 100 db range
    v = 100;
  */
	//red 0xF800;
	//green 0x07e0;
	//blue 0x001f;

  r =g = b = 0;
  
  if (v < 32){                  // r = 0, g= 0, increase blue
    r = 0;
    g = 0;
    b = v; 
  }
  else if (v < 64){             // r = 0, increase g, blue is max
    r = 0;
    g = (v-32) << 7;
    b = 0x001f; 
  }
  else if (v < 96){             // r = 0, g= max, decrease b
    r = 0;
    g = 0x7E00; 
    b = (96-v); 
  }
  else if (v < 128){             // increase r, g = max, b = 0
    r = (v-96) << 11;
    g = 0x07e0;
    b = 0; 
  }
  else {                       // r = max, decrease g, b = 0
    r = 0xf800;
    g = (160-v) << 6;
    b = 0; 
  }
  //0x3c00 = red, 0x1f = green, blue=0x1e00

   // it took two days to figure this out! the bitfields are all garbled on the ili9488
  return  r + g + b;
}

void waterfall_line(int x, int y1, int y2, int color){
	if (x < 0)
			return;
	if (x > 240)
			return;
	if (y1 < 0)
			y1 = 0;
	if (y2 < 0)
			y2 = 0;
	if (y1 > 48)
			y1 = 48;
	if (y2 > 48)
			y2 = 48;
	if (y1 > y2){
		int t = y2;
		y2 = y1;
		y1 = t;
	}
	for (;y1 <= y2; y1++)
		waterfall[(y1 * 240) + x] = color;
}

void waterfall_draw(struct field *f){
	if (!strcmp(f->value, "OFF"))
			return;
	screen_bitblt(f->x, f->y, f->w, 96, waterfall);
}

//always 240 values!
static uint16_t color = 0;
void waterfall_update(struct field *f, uint8_t *bins){

	memset(waterfall, 0, 240 * 49 * sizeof(uint16_t));
	//paint the bw strip 
	for (int i = bandwidth_start; i < bandwidth_stop; i++)
		waterfall_line(f->w/2 + i, 0, 48, TFT_DARKGREY);
	waterfall_line(f->w/2,0,48, TFT_WHITE);
	waterfall_line(f->w/2 + center_line,0,48, TFT_GREEN);
	if (!strcmp(field_get("MODE")->value, "FT8"))
		waterfall_line(f->w/2 + tx_pitch,0,48, TFT_RED);

  int last_y = 48-waterfall[0];
  for (int i = 1; i < 240; i++){
    int y_now = 48 - (bins[i]/2);
		int y_last = 48 - (bins[i-1]/2);
    if(y_now < 0)
      y_now = 0;
		waterfall_line(f->w-i, y_last, y_now, TFT_YELLOW);
		//waterfall[(y_now * 240) + i] = TFT_YELLOW;
    last_y = y_now;
  }

  //scroll down the waterfall
  int waterfall_length = sizeof(waterfall);
	uint16_t *w = waterfall + (240 * 49);
  double scale = 240.0/f->w;

  memmove(w+240, w, (240 * 45) * sizeof(uint16_t));
  for (int i = 0; i < f->w ; i++)
      w[f->w - i] = heat_map((uint16_t)bins[(int16_t)(scale * i)]);
	//draw the central line

}
