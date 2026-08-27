#ifndef FIELDS_LIST_H
#define FIELDS_LIST_H

#define FIELDS_ALWAYS_ON 20 

struct field main_list[] = {

  //the first fields are always visible.
  // Top status row: MENU + MODE + DRIVE + IF fill the left half, FREQ + AUDIO
  // the right. DRIVE and IF are frequently adjusted during operation, so they
  // live here at 64px each (up from the original cramped 48px). RIT is not on
  // the panel — it's reached through the MENU dialog (see the MENU handler),
  // since it's toggled far less often than drive/IF. FREQ keeps its 192px
  // width and x=240 anchor (freq_draw / the METERS overlay depend on it).
  {FIELD_BUTTON, 0, 0, 48, 48,  TFT_ORANGE, "MENU", "-" },
  {FIELD_SELECTION, 48, 0, 64, 48,  TFT_BLACK, "MODE", "USB", "USB/LSB/CW/CWR/FT8/AM/DIGI/2TONE"},
  {FIELD_NUMBER, 112, 0, 64, 48,  TFT_BLACK, "DRIVE", "100", "0/100/5"},
  {FIELD_NUMBER, 176, 0, 64, 48,  TFT_BLACK, "IF", "40", "0/100/1"},
  // FREQ + its S-meter/voltage overlay now sit flush against the right edge
  // (x=288..480). AUDIO takes the slot to their left at x=240. freq_draw()
  // positions all its text relative to the FREQ field's own x, so it follows
  // automatically; the METERS overlay is moved to match (see below).
  {FIELD_NUMBER, 240, 0, 48, 48,  TFT_BLACK, "AUDIO", "95", "0/100/1"},
  {FIELD_FREQ, 288, 0, 192, 48,  TFT_BLACK, "FREQ", "14074000", "500000/30000000/1"},

  {FIELD_SELECTION, 288, 48, 48, 48,  TFT_BLACK, "SPAN", "25K", "25K/10K/6K/2.5K"},
  {FIELD_NUMBER, 336, 48, 48, 48,  TFT_BLACK, "BW", "2200", "50/5000/50"},
  {FIELD_SELECTION, 384, 48, 48, 48,  TFT_BLACK, "STEP", "1K", "10K/1K/500H/100H/10H"},
  {FIELD_SELECTION, 432, 48, 48, 48,  TFT_BLACK, "AGC", "MED", "OFF/SLOW/MED/FAST"},
  // SET moved into the MENU dialog (see the MENU handler). Its field
  // definition now lives with the bandswitch/AGC/VFO group below, outside the
  // always-on region, so it only appears as a button inside the Radio menu.
 
   //LOGGER FIELDS
  {FIELD_BUTTON, 0, 48, 48, 48,  TFT_DARKGREEN, "OPEN"},
  {FIELD_BUTTON, 48, 48, 48, 48,  TFT_DARKGREEN, "WIPE"},
  {FIELD_BUTTON, 240, 48, 48, 48,  TFT_DARKGREEN, "SAVE"},
  {FIELD_TEXT, 96,48, 96, 24, TFT_BLACK, "CALL", "", "0,10"},
  {FIELD_TEXT, 192, 48, 48, 24, TFT_BLACK, "EXCH", "", "0,10"},
  {FIELD_TEXT, 96, 72, 48, 24, TFT_BLACK, "RECV", "", "0,10"},
  {FIELD_TEXT, 144, 72, 48, 24, TFT_BLACK, "SENT", "", "0,10"},
  {FIELD_TEXT, 192, 72, 48, 24, TFT_BLACK, "NR", "", "0,10"},

	//METERS is not SMETERS to prevent spiralling of request/responses.
	// Kept 3px inside FREQ's left edge; moved with FREQ to x=291 (FREQ x=288).
  {FIELD_SMETER, 291, 3, 180, 15, TFT_BLACK, "METERS", "0", "0/10000/1"},
  //keyboard fields, keeping them just under the permanent fields
  //ensures that they get detected first  

  {FIELD_KEY, 0, 120, 48, 40,  TFT_BLACK, "1", "1"},
  {FIELD_KEY, 48, 120, 48, 40,  TFT_BLACK, "2", "2"},
  {FIELD_KEY, 96, 120, 48, 40,  TFT_BLACK, "3", "3"},
  {FIELD_KEY, 144, 120, 48, 40,  TFT_BLACK, "4", "4"},
  {FIELD_KEY, 192, 120, 48, 40,  TFT_BLACK, "5", "5"},
  {FIELD_KEY, 240, 120, 48, 40,  TFT_BLACK, "6", "6"},
  {FIELD_KEY, 288, 120, 48, 40,  TFT_BLACK, "7", "7"},
  {FIELD_KEY, 336, 120, 48, 40,  TFT_BLACK, "8", "8"},
  {FIELD_KEY, 384, 120, 48, 40,  TFT_BLACK, "9", "9"},
  {FIELD_KEY, 432, 120, 48, 40,  TFT_BLACK, "0", "0"},

  {FIELD_KEY, 0, 160, 48, 40,  TFT_BLACK, "Q", "@"},
  {FIELD_KEY, 48, 160, 48, 40,  TFT_BLACK, "W", "AR"},
  {FIELD_KEY, 96, 160, 48, 40,  TFT_BLACK, "E", "BT"},
  {FIELD_KEY, 144, 160, 48, 40,  TFT_BLACK, "R", "#"},
  {FIELD_KEY, 192, 160, 48, 40,  TFT_BLACK, "T", "$"},
  {FIELD_KEY, 240, 160, 48, 40,  TFT_BLACK, "Y", "*"},
  {FIELD_KEY, 288, 160, 48, 40,  TFT_BLACK, "U", "("},
  {FIELD_KEY, 336, 160, 48, 40,  TFT_BLACK, "I", ")"},
  {FIELD_KEY, 384, 160, 48, 40,  TFT_BLACK, "O", "-"},
  {FIELD_KEY, 432, 160, 48, 40,  TFT_BLACK, "P", "="},

  {FIELD_KEY, 24, 200, 48, 40,  TFT_BLACK, "A", "F1"},
  {FIELD_KEY, 72, 200, 48, 40,  TFT_BLACK, "S", "F2"},
  {FIELD_KEY, 120, 200, 48, 40,  TFT_BLACK, "D", "F3"},
  {FIELD_KEY, 168, 200, 48, 40,  TFT_BLACK, "F", "F4"},
  {FIELD_KEY, 216, 200, 48, 40,  TFT_BLACK, "G", "F5"},
  {FIELD_KEY, 264, 200, 48, 40,  TFT_BLACK, "H", "F6"},
  {FIELD_KEY, 312, 200, 48, 40,  TFT_BLACK, "J", "F7"},
  {FIELD_KEY, 360, 200, 48, 40,  TFT_BLACK, "K", "F8"},
  {FIELD_KEY, 408, 200, 48, 40,  TFT_BLACK, "L", "F9"},
  
  {FIELD_KEY, 0, 240, 72, 40,  TFT_BLACK, "Sym", "ABC"}, 
  {FIELD_KEY, 72, 240, 48, 40,  TFT_BLACK, "Z", "~"},
  {FIELD_KEY, 120, 240, 48, 40,  TFT_BLACK, "X", "_"},
  {FIELD_KEY, 168, 240, 48, 40,  TFT_BLACK, "C", "'"},
  {FIELD_KEY, 216, 240, 48, 40,  TFT_BLACK, "V", ":"},
  {FIELD_KEY, 264, 240, 48, 40,  TFT_BLACK, "B", "\""},
  {FIELD_KEY, 312, 240, 48, 40,  TFT_BLACK, "N", "!"},
  {FIELD_KEY, 360, 240, 48, 40,  TFT_BLACK, "M", ";"},
  {FIELD_KEY, 408, 240, 72, 40,  TFT_BLACK, "del", "del"},
  
  {FIELD_KEY, 0, 280, 72, 40,  TFT_BLACK, "Start", "Start"},
  {FIELD_KEY, 72, 280, 72, 40,  TFT_BLACK, "Stop", "Stop"},
  {FIELD_KEY, 168, 280, 48, 40,  TFT_BLACK, "/", "\\"},
  {FIELD_KEY, 216, 280, 96, 40,  TFT_BLACK, "space", "space"},
  {FIELD_KEY, 312, 280, 48, 40,  TFT_BLACK, ".", ":"},
  {FIELD_KEY, 360, 280, 48, 40,  TFT_BLACK, "?", ","},
  {FIELD_KEY, 432, 280, 48, 40,  TFT_BLACK, "[x]", "[x]"},  

  {FIELD_BUTTON, 0, 272, 48, 48,  TFT_BLACK, "ESC", ""},
  {FIELD_BUTTON, 48, 272, 48, 48,  TFT_BLACK, "F1", "CQ"},
  {FIELD_BUTTON, 96, 272, 48, 48,  TFT_BLACK, "F2", "CALL"},
  {FIELD_BUTTON, 144, 272, 48, 48,  TFT_BLACK, "F3", "REPLY"},
  {FIELD_BUTTON, 192, 272, 48, 48,  TFT_BLACK, "F4", "RREPLY"},
  {FIELD_BUTTON, 240, 272, 48, 48,  TFT_BLACK, "F5", "73"},
  {FIELD_BUTTON, 288, 272, 48, 48,  TFT_BLACK, "F6", "RRR"}, 
  {FIELD_BUTTON, 336, 272, 48, 48,  TFT_BLACK, "F7", "RRR"},
  {FIELD_BUTTON, 384, 272, 48, 48,  TFT_BLACK, "F8", "RRR"},
  {FIELD_BUTTON, 432, 272, 48, 48,  TFT_BLACK, "F9", "RRR"},

	// this will get over written by the keyboard 
  {FIELD_CONSOLE, 240, 120, 240, 152,  TFT_BLACK, "CONSOLE", "Hello!"},  
  {FIELD_TEXT, 48,48, 96, 24, TFT_BLACK, "CALLSIGN", "", "0,10"},
  {FIELD_TEXT, 240, 96, 240, 24,  TFT_BLACK, "TEXT", "", "0/40"},  
  {FIELD_NUMBER, 288, 272, 48, 48,  TFT_BLACK, "TX_PITCH", "2200", "300/3000/10"},
  {FIELD_SELECTION, 336, 272, 48, 48,  TFT_BLACK, "TX1ST", "ON", "ON/OFF"},
  {FIELD_SELECTION, 384, 272, 48, 48,  TFT_BLACK, "AUTO", "ON", "ON/OFF"},
  {FIELD_NUMBER, 432, 272, 48, 48,  TFT_BLACK, "FT8_REPEAT", "5", "1/10/1"},
  {FIELD_FT8, 240, 96, 240, 176,  TFT_BLACK, "FT8_LIST", "", "1/10/1"},

  //CW controls
  {FIELD_NUMBER, 382, 272, 48, 48, TFT_BLACK, "WPM", "12", "3/50/1"},
  {FIELD_NUMBER, 432, 272, 48, 48, TFT_BLACK, "PITCH", "600", "100/3000/10"},   

  //SSB/AM other voice modes
  // Bottom row: MIC (72) + TX (156) + RX (156) + TUNE (96, flush right),
  // filling the full 480px. TUNE emits a timed tuning carrier: tapping ON
  // sends "TUNE ON" (keys TX at TNPWR power for TNDUR seconds, auto-stops);
  // OFF sends "TUNE OFF". Orange to flag that it keys the transmitter.
  // TNDUR/TNPWR live in the OPTIONS menu.
  {FIELD_NUMBER, 0, 272, 72, 48, TFT_BLACK, "MIC", "12", "0/100/5"},
  {FIELD_BUTTON, 72, 272, 156, 48, TFT_RED, "TX", ""},
  {FIELD_BUTTON, 228, 272, 156, 48, TFT_BLUE, "RX", ""},
  {FIELD_SELECTION, 384, 272, 96, 48, TFT_ORANGE, "TUNE", "OFF", "ON/OFF"},

  //logbook
  {FIELD_LOGBOOK, 0, 48, 480, 224, TFT_BLACK, "LOGB", "", "", logbook_draw},
  {FIELD_BUTTON, 24, 272, 96, 48, TFT_BLUE, "FINISH", ""},
  
  //waterfall can get hidden by keyboard et al (or even removed by FT8 etc
  {FIELD_WATERFALL, 0, 96, 240, 176,  TFT_BLACK, "WF", "ON"}, //WARNING: Keep the height of the waterfall to be a multiple of 48 (see waterfal_update() code)


  /* These fields are never visible */
  {FIELD_KEY, 20000, 20000, 0, 0,  TFT_BLACK, "VFOA", "14074000"},  
  {FIELD_KEY, 20000, 20000, 0, 0,  TFT_BLACK, "VFOB", "7050000"},      
  {FIELD_KEY, 20000, 20000, 0, 0,  TFT_BLACK, "RIT_DELTA", "0", "-25000/25000/1"},      

	{FIELD_STATIC, 24, 96, SCREEN_WIDTH-96,  100, TFT_BLACK, "QSODEL", "0", ""}, 	

  {FIELD_NUMBER, 0, 272, 48, 48, TFT_BLACK, "REF", "0", "0/5000/1"},
  {FIELD_NUMBER, 0, 272, 48, 48, TFT_BLACK, "POWER", "0", "0/10000/1"},
  {FIELD_NUMBER, 0, 272, 48, 48, TFT_BLACK, "VBATT", "0", "0/5000/1"},
  {FIELD_NUMBER, 0, 272, 192, 15, TFT_BLACK, "SMETER", "0", "0/10000/1"},
  {FIELD_NUMBER, 0, 272, 192, 15, TFT_BLACK, "IN_TX", "0", "0/10000/1"},

  {FIELD_NUMBER, 0, 272, 48, 48, TFT_BLACK, "HIGH", "0", "0/5000/1"},
  {FIELD_NUMBER, 0, 272, 48, 48, TFT_BLACK, "LOW", "0", "0/5000/1"},

	/* alert box */

	{FIELD_TITLE, 24, 8, SCREEN_WIDTH-96, 48, TFT_BLACK, "TITLE", "Hi", ""}, 	
	{FIELD_STATIC, 24, 96, SCREEN_WIDTH-96,  100, TFT_BLACK, "MESSAGE", "Hi", ""}, 	
  {FIELD_BUTTON, 24, 248, 96, 48, TFT_RED, "OK", ""},   
  {FIELD_BUTTON, 24, 248, 96, 48, TFT_RED, "DELETE", ""},   
  {FIELD_BUTTON, 24, 248, 96, 48, TFT_BLACK, "CLOSE", ""},   
  {FIELD_BUTTON, 140, 248, 96, 48, TFT_BLUE, "CANCEL", ""},

  /* bandswitch, agc, etc. */
  {FIELD_BUTTON, 24, 48, 48, 48,  TFT_BLACK, "10M", "1"},
  {FIELD_BUTTON, 72, 48, 48, 48,  TFT_BLACK, "12M", "1"},
  {FIELD_BUTTON, 120, 48, 48, 48,  TFT_BLACK, "15M", "1"},
  {FIELD_BUTTON, 168, 48, 48, 48,  TFT_BLACK, "17M", "1"},
  {FIELD_BUTTON, 216, 48, 48, 48,  TFT_BLACK, "20M", "1"},
  {FIELD_BUTTON, 264, 48, 48, 48,  TFT_BLACK, "30M", "1"}, 
  {FIELD_BUTTON, 312, 48, 48, 48,  TFT_BLACK, "40M", "1"},
  {FIELD_BUTTON, 360, 48, 48, 48,  TFT_BLACK, "60M", "1"},
  {FIELD_BUTTON, 408, 48, 48, 48,  TFT_BLACK, "80M", "1"},
  // AGC now lives on the main panel (top second row), so it's no longer
  // defined here or shown in the Radio dialog. The y=96 dialog row is
  // VFO / SPLIT / RIT / SET, tiled contiguously from the left.
  {FIELD_SELECTION, 24, 96, 48, 48,  TFT_BLACK, "VFO", "A", "A/B"},
  {FIELD_SELECTION, 72, 96, 48, 48,  TFT_BLACK, "SPLIT", "OFF", "ON/OFF"},
  // RIT moved off the main panel into the MENU dialog, next to SPLIT.
  {FIELD_SELECTION, 120, 96, 48, 48,  TFT_BLACK, "RIT", "OFF", "ON/OFF"},
  // SETUP moved to the 4th row (y=196), right of the OPTIONS button, so the
  // y=96 row is just VFO/SPLIT/RIT. Opens the station settings menu
  // (callsign / grid / passkey) and, in parallel, the GTK settings dialog.
  {FIELD_BUTTON, 126, 196, 96, 48,  TFT_BLUE, "SETUP", ""},
  // OPTIONS button opens the GTK-mirrored audio/DSP control menu. Shown only
  // inside the Radio dialog; shares the y=196 row with SET (bands are y=48,
  // VFO/SPLIT/RIT y=96, CW input/delay/sidetone y=144, CLOSE/SHUTDOWN y=248),
  // so it doesn't overlap any other Radio-dialog control. Its label must stay
  // distinct from the main-panel "MENU" button above.
  {FIELD_BUTTON, 24, 196, 96, 48,  TFT_BLUE, "OPTIONS", ""},
  /* Shutdown button — only shown inside the MENU dialog, lower-right corner */
  {FIELD_BUTTON, 360, 248, 96, 48,  TFT_RED, "SHUTDOWN", ""},

   /* ---- Menu 1 : audio / DSP signal-path controls ---- */
  /* TXEQ and RXEQ were removed (equalizer is configured on the GTK side).
     TUNE moved to the main Radio menu (below the 80M button). */
  /* row 1 : toggles */
  {FIELD_SELECTION, 8,   40, 112, 48, TFT_BLACK, "NOTCH",  "OFF", "ON/OFF"},
  {FIELD_SELECTION, 128, 40, 112, 48, TFT_BLACK, "ANR",    "OFF", "ON/OFF"},
  {FIELD_SELECTION, 248, 40, 112, 48, TFT_BLACK, "DSP",    "OFF", "ON/OFF"},
  {FIELD_SELECTION, 368, 40, 112, 48, TFT_BLACK, "VFOLK",  "OFF", "ON/OFF"},
  /* row 2 : numerics */
  {FIELD_NUMBER,    8,   92, 112, 48, TFT_BLACK, "NFREQ",  "50",  "60/3000/10"},
  {FIELD_NUMBER,    128, 92, 112, 48, TFT_BLACK, "BNDWTH", "10",  "60/1000/10"},
  {FIELD_NUMBER,    248, 92, 112, 48, TFT_BLACK, "BFO",    "0",   "-3000/3000/50"},
  {FIELD_NUMBER,    368, 92, 112, 48, TFT_BLACK, "TXMON",  "0",   "0/10/1"},
  /* row 3 : numerics */
  {FIELD_NUMBER,    8,   144, 112, 48, TFT_BLACK, "TNDUR", "5",   "2/30/1"},
  {FIELD_NUMBER,    128, 144, 112, 48, TFT_BLACK, "TNPWR", "20",  "1/100/1"},
  // COMP = speech compressor level, 0 (off) .. 10, mirroring the GTK's
  // #comp_plugin selection. A SELECTION so it steps through the same discrete
  // levels the GTK offers; editing posts "COMP <level>" and the GTK's
  // do_comp_edit callback latches it (via the generic cmd_exec callback fire).
  {FIELD_SELECTION, 248, 144, 112, 48, TFT_BLACK, "COMP", "0", "0/1/2/3/4/5/6/7/8/9/10"},

  /* Menu 2 (scope / waterfall / display controls) was intentionally removed —
     those settings don't apply to the front panel. If you ever want them back,
     they lived in the GTK menu2_display() as WFMIN/WFMAX/WFSPD/SCOPEGAIN/
     SCOPEAVG/SCOPESIZE/INTENSITY/AUTOSCOPE/TXPANAFAL. */

  /* settings */

  {FIELD_STATIC, 26,48, 96, 0, TFT_BLACK, "MY CALL", "MY CALL:", "0/10"},
  {FIELD_TEXT, 24, 62, 96, 24, TFT_BLACK, "MYCALLSIGN", "", "0/10"},
  {FIELD_STATIC, 146,48, 96, 0, TFT_BLACK, "MY GRID", "MY GRID:", "0/10"},
  {FIELD_TEXT, 144, 62, 96, 24, TFT_BLACK, "MYGRID", "", "0/10"},
  {FIELD_STATIC, 266, 48, 96, 0, TFT_BLACK, "PASS KEY", "PASS KEY:", "0/10"},
  {FIELD_TEXT, 264, 62, 96, 24, TFT_BLACK, "PASSKEY", "", "0/10"},
  // CW keyer input, CW delay, and sidetone level. Moved from the Settings
  // dialog into the Radio menu (see the MENU handler). Placed on the y=144
  // row, below the AGC/VFO/SPLIT/RIT/SET row, so nothing overlaps. These
  // fields draw their own labels (INPUT / DELAY / SIDETONE), so no separate
  // static labels are needed.
  {FIELD_SELECTION, 24, 144, 96, 48, TFT_BLACK, "CW_INPUT", "", "IAMBIC/IAMBICB/STRAIGHT"},
  {FIELD_NUMBER, 144, 144, 96, 48, TFT_BLACK, "CW_DELAY", "300", "50/1000/50"},
  {FIELD_NUMBER, 264, 144, 96, 48,  TFT_BLACK, "SIDETONE", "80", "0/100/5"},
	
  {-1}
};
#endif
