#include <TFT_eSPI.h>       // Hardware-specific library
#include "zbitx.h"
#include "logbook.h"

#define FT8_MAX 100 
struct ft8_message ft8_list[FT8_MAX];
void field_ft8_append(const char *msg);
void ft8_move_cursor(int by);
int ft8_next = 0;
int ft8_cursor = -1; // -1: nothing selected, follow the newest messages
int ft8_id = 1;
unsigned long ft8_cursor_timeout = 0;
unsigned long last_ft8_cursor_movement = 0;

// Rows currently on screen, as indexes into ft8_list (for tap-to-select).
// Indexes are < FT8_MAX (100), so bytes keep these small; RAM is tight.
static uint8_t ft8_rows[FT8_MAX];
// scratch list of the messages that pass the filter, shared by draw/scroll
static uint8_t ft8_vis[FT8_MAX];
static int ft8_row_count = 0;
// id of the message in the top row, so scrolling stays put between redraws
static uint32_t ft8_top_id = 0;


void ft8_init(){
  memset(ft8_list, 0, sizeof(ft8_list));
	ft8_next = 0;
	ft8_cursor = -1;
	ft8_id = 1;
	ft8_cursor_timeout = 0;
	last_ft8_cursor_movement = 0;
	ft8_row_count = 0;
	ft8_top_id = 0;
}

/* CQ / All filter (the FT8_FILTER field, shared with the radio and the web UI).
   In CQ mode the list keeps CQ calls plus anything to or from us. The radio
   marks those with style codes in the decorated line: #P = a decode addressed
   to us, #H = our own transmission, #Q = our callsign inside the message. */
static bool ft8_cq_only(){
	struct field *f = field_get("FT8_FILTER");
	return f && !strcmp(f->value, "CQ");
}

static bool ft8_passes_filter(const struct ft8_message *m){
	if (!m->id)
		return false;
	if (!ft8_cq_only())
		return true;
	if (m->data[0] == '#' && (m->data[1] == 'P' || m->data[1] == 'H'))
		return true;
	if (strstr(m->data, "#Q"))
		return true;
	// first word after the '~', skipping the "#x" style codes
	const char *p = strchr(m->data, '~');
	if (!p)
		return false;
	p++;
	while (*p == ' ' || (*p == '#' && p[1])){
		if (*p == '#')
			p += 2;
		else
			p++;
	}
	return !strncmp(p, "CQ ", 3) || !strcmp(p, "CQ");
}

// Messages that pass the filter, oldest first, as indexes into ft8_list
static int ft8_visible(uint8_t *out){
	int n = 0;
	for (int k = 0; k < FT8_MAX; k++){
		int idx = (ft8_next + k) % FT8_MAX; // ft8_next is the oldest slot
		if (ft8_passes_filter(ft8_list + idx))
			out[n++] = idx;
	}
	return n;
}

static int ft8_position(const uint8_t *vis, int n, int index){
	for (int i = 0; i < n; i++)
		if (vis[i] == index)
			return i;
	return -1;
}

// Called when FT8_FILTER changes (tap, encoder or a push from the radio)
void ft8_filter_changed(){
	ft8_cursor = -1;   // jump back to the newest messages
	ft8_top_id = 0;
	struct field *f = field_get("FT8_LIST");
	if (f)
		f->redraw = true;
}

// Draws the FT8_FILTER field as a slim "All | CQ" switch above the list
void ft8_filter_draw(struct field *f){
	bool cq = !strcmp(f->value, "CQ");
	int half = (f->w - 4) / 2;
	int x = f->x + 2, y = f->y + 1, h = f->h - 2;

	screen_fill_rect(f->x, f->y, f->w, f->h, SCREEN_BACKGROUND_COLOR);
	screen_fill_round_rect(x, y, half, h, cq ? TFT_BLACK : TFT_BLUE);
	screen_fill_round_rect(x + half, y, half, h, cq ? TFT_BLUE : TFT_BLACK);
	screen_draw_round_rect(x, y, half * 2, h, f == f_selected ? TFT_WHITE : TFT_DARKGREY);

	// non-const: screen_text_width() is only implemented for char *
	char left[] = "ALL", right[] = "CQ ONLY";
	int ty = y + (h - screen_text_height(2)) / 2;
	screen_draw_text(left, -1, x + (half - screen_text_width(left, 2)) / 2, ty,
		cq ? TFT_DARKGREY : TFT_WHITE, 2);
	screen_draw_text(right, -1, x + half + (half - screen_text_width(right, 2)) / 2, ty,
		cq ? TFT_WHITE : TFT_DARKGREY, 2);
}

void ft8_select(){
	char *p, *q;
	if (ft8_cursor < 0 || !ft8_list[ft8_cursor].id)
		return;
	struct ft8_message *m = ft8_list + ft8_cursor;

	// message_buffer is defined in the main .ino as char message_buffer[200];
	// (extern here), so we can't sizeof() it — guard against the known length.
	if (strlen(m->data) + 6 >= 200)
		return;

	// Build "FT8 <message>\n" into message_buffer, stripping the internal
	// "#x" colour/slot id prefixes that tag each callsign token so the radio
	// receives a clean message.
	p = m->data;
	strcpy(message_buffer, "FT8 ");
	q = message_buffer + strlen(message_buffer);
	while (*p){
		//skip the '#x'
		if (*p == '#'){
			p++;
			if (*p)
				p++;
			continue;
		}
		*q++ = *p++;
	}
	//close with a new line
	*q++ = '\n';
	*q = 0;
}

void ft8_update(const char *msg){
  //#G121145  16 -16 1797 ~ #GDG5YPR #RIZ2FOS #SJN55
  char buff[100], *p;

  struct ft8_message *m = ft8_list + ft8_next;
  
  strcpy(buff, msg);

  p = strtok(buff, " ");
  if(!p)return;  

  p = strtok(NULL, " "); //skip the confidence score
  p = strtok(NULL, " ");
  if (!p) return;
  m->signal_strength = atoi(p);
 
  p = strtok(NULL, " ");
  if (!p) return;
  m->frequency = atoi(p);
  m->id = ft8_id++; 
  
  p = strchr(msg, '~');
  if (!p)
    return;

  p+= 2; //skip the tilde and the next space

  if (strlen(p) >= FT8_MAX_DATA){
    return;
  }
  strcpy(m->data, msg);  
  ft8_next++;
  if (ft8_next >= FT8_MAX)
		ft8_next = 0;
	if (ft8_next == ft8_cursor)
		ft8_cursor = -1;
}

void ft8_move_cursor(int by){
	uint8_t *vis = ft8_vis;
	int n = ft8_visible(vis);
	if (n == 0)
		return;

	int pos = (ft8_cursor >= 0) ? ft8_position(vis, n, ft8_cursor) : -1;
	if (pos < 0)
		pos = n - 1; // nothing (visible) selected: start from the newest
	else if (by < 0 && pos > 0)
		pos--;
	else if (by > 0 && pos < n - 1)
		pos++;
	ft8_cursor = vis[pos];
	last_ft8_cursor_movement = millis();
}

void ft8_draw(field *f){
  int count = f->h / screen_text_height(2);
	uint8_t *vis = ft8_vis;
	int n = ft8_visible(vis);

	if (last_ft8_cursor_movement + 30000 < millis())
		ft8_cursor = -1;

	int cursor_pos = (ft8_cursor >= 0) ? ft8_position(vis, n, ft8_cursor) : -1;
	if (cursor_pos < 0)
		ft8_cursor = -1; // selection was filtered out or overwritten

	// top row: the newest page when nothing is selected, otherwise keep the
	// previous top and scroll only as far as needed to keep the cursor on screen
	int top = n - count;
	if (ft8_cursor != -1){
		top = 0;
		while (top < n && ft8_list[vis[top]].id < ft8_top_id)
			top++;
		if (cursor_pos < top)
			top = cursor_pos;
		else if (cursor_pos > top + count - 1)
			top = cursor_pos - count + 1;
		if (top > n - count)
			top = n - count;
	}
	if (top < 0)
		top = 0;
	ft8_top_id = (n > 0) ? ft8_list[vis[top]].id : 0;

	screen_fill_rect(f->x, f->y, f->w, f->h, TFT_BLACK);

	ft8_row_count = 0;
  for (int i=0; i < count && top + i < n; i++){
    char buff[100], *p;
    int x = f->x+2;
		int index = vis[top + i];
		ft8_rows[ft8_row_count++] = index;
		{
			char slot = '0';
			char slot1 = ft8_list[index].data[6];
			char slot2 = ft8_list[index].data[7];
			if (slot1 == '0' && slot2 == '0')
				slot = '1';
			else if (slot1 == '1' && slot2 == '5')
				slot = '2';
			else if (slot1 == '3' && slot2 == '0')
				slot = '3';
			else
				slot = '4';
    	strcpy(buff+3, ft8_list[index].data + 12);
			buff[0] = '#';
			buff[1] = 'G';
			buff[2] = slot;
 	   for (char *p = strtok(buff, "#"); p; p = strtok(NULL, "#")){
  	    //F=white G=Green R=Red, S=Orange
    	  uint16_t color = TFT_WHITE;
      	switch(*p){
   	    case 'G':
    	    color = TFT_GREEN;
      	  break;
      	case 'R':
        	color = TFT_CYAN;
        	break;
		case 'Q':
			color = TFT_BLUE;
			break;
		case 'O':
			color = TFT_ORANGE;
			break;
		case 'H':
			color = TFT_DARKGREY;
			break;
   	    case 'S':
    	    color = TFT_YELLOW;
      	  break;
    	  default:
      	  color= TFT_WHITE;
        	break;
      	}
      	screen_draw_text(p+1, -1, x, f->y + (screen_text_height(2) * i), color, 2);
      	x += screen_text_width(p+1,2);
				if(index == ft8_cursor && f == f_selected)
      		screen_draw_rect(f->x+2, f->y + (screen_text_height(2) * i), f->w - 4, 16, TFT_WHITE);
    	}
    }
  } 
}

void ft8_input(int input){
	if (input == ZBITX_KEY_DOWN){
		ft8_move_cursor(+1);
	}
	else if (input == ZBITX_KEY_UP)
		ft8_move_cursor(-1);
	else if (input == ZBITX_KEY_ENTER){
		ft8_select();
	}
}

// Tap-to-call addition
void ft8_touched(int x_offset, int y_offset){
	int from_top = y_offset / screen_text_height(2);
	if (from_top < 0 || from_top >= ft8_row_count)
		return; // tapped below the last message
	ft8_cursor = ft8_rows[from_top];
	last_ft8_cursor_movement = millis();

	struct field *single_tap = field_get("1-TAP");
	if (single_tap && !strcmp(single_tap->value, "ON"))
		ft8_select();
}