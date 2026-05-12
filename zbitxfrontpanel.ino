/*
TO GET GOING WITH PICO ON ARDUINO:

1. Download the post_install.sh and add execute permissions and execute as sudo
2. Follow the instructions on https://github.com/earlephilhower/arduino-pico

You may need to copy over the uf2 for the first time.
The blink.ino should work (note that the pico w and pico have different gpios for the LED)
 */
#include <WiFi.h>
#include "zbitx.h"
extern "C" {
#include "pico.h"
#include "pico/time.h"
#include "pico/bootrom.h"
}

auto &Debug = Serial;

int freq = 7000000;
unsigned long now = 0;
unsigned long last_blink = 0;
uint8_t buff[1000]; // HTTP reads into this before sending to MP3

bool mouse_down = false;
uint8_t encoder_state = 0;
boolean encoder_switch = false;
unsigned long next_repeat_time = 0;
unsigned int last_wheel_moved = 0;
unsigned int wheel_count = 0;

int vfwd=0, vswr=0, vref = 0, vbatt=0;
int wheel_move = 0;

//wifi connectivity stuff
WiFiClient client;
WiFiMulti multi;
extern char message_buffer[];
char temp_ssid[32];
char temp_key[32];
uint8_t stream_state = STREAM_WIFI_OFFLINE;
const char* host = "192.168.4.1";
const uint16_t port = 8081;
unsigned long last_rx_ms = 0;
const unsigned long SERVER_RX_TIMEOUT_MS = 10000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;


unsigned int core1_time = 0;

void core1_check(){
  
  if (millis() > core1_time + 10000){
    Debug.println("Core1 is dead!");
    rp2040.restartCore1();
  }
  
}

int enc_state(){
  return  (digitalRead(ENC_A)? 1:0) + (digitalRead(ENC_B) ? 2:0);
}

void on_enc(){

  uint8_t encoder_now = enc_state();
	if (encoder_now == encoder_state)
		return;

	if (enc_state() != encoder_now)
		return;
  
  if ((encoder_state == 0 && encoder_now == 1) 
    || (encoder_state == 1 && encoder_now == 3) 
    || (encoder_state == 3 && encoder_now == 2)
    || (encoder_state == 2 && encoder_now == 0)) {
    	wheel_move--;
			wheel_count++;
	}
  else if ((encoder_state == 0 && encoder_now == 2)
    || (encoder_state == 2 && encoder_now == 3)
    || (encoder_state == 3 && encoder_now == 1)
    || (encoder_state == 1 && encoder_now == 0)){
      wheel_move++;
			wheel_count++;
	}
  encoder_state = encoder_now;    
}

char last_sent[1000]={0};
int req_count = 0;
int total = 0;

//comannd tokenizer

static char cmd_label[FIELD_TEXT_MAX_LENGTH];
static char cmd_value[1000]; // this should be enough
static bool cmd_in_label = true;
static bool cmd_in_field = false;

void command_init(){
  cmd_label[0] = 0;
  cmd_value[0] = 0;
  //cmd_p = cmd_label;
  cmd_in_label = false;
  cmd_in_field = false;
}

boolean in_tx(){
	struct field *f = field_get("IN_TX");
	if (!f)
		return false;
	if (!strcmp(f->value, "0"))
		return false;
	else
		return true;
}

void set_bandwidth_strip(){
	struct field *f_span = field_get("SPAN");
	struct field *f_high = field_get("HIGH");
	struct field *f_low  = field_get("LOW");
	struct field *f_mode = field_get("MODE");
	struct field *f_pitch = field_get("PITCH");
	struct field *f_tx_pitch = field_get("TX_PITCH");

	if (!f_span || !f_high || !f_low || !f_mode || !f_pitch)
		return;

	int span = 25000;
	if (!strcmp(f_span->value, "10K"))
		span = 10000;
	else if (!strcmp(f_span->value, "6K"))
		span = 6000;
	else if (!strcmp(f_span->value, "2.5K"))
		span = 2500;	

	int high = (atoi(f_high->value) * 240)/span;
	int low = (atoi(f_low->value) * 240)/span;
	int pitch = (atoi(f_pitch->value) * 240)/span;
	int tx_pitch = (atoi(f_tx_pitch->value) * 240)/span;

/*	if (!strcmp(f_mode->value, "CW"))
		
	else if(!strcmp(f_mode->value, "CWR"))
		
	}
*/
	if (!strcmp(f_mode->value, "LSB") || !strcmp(f_mode->value, "CWR")){
		high = -high;
		low = -low;
		pitch = -pitch;
		tx_pitch = -tx_pitch;
	}
	
	waterfall_bandwidth(low, high, pitch, tx_pitch);
}

void reset_tokenizer(){
	cmd_in_label = true;
  cmd_in_field = true;
	memset(cmd_label, 0, sizeof(cmd_label));
	memset(cmd_value, 0, sizeof(cmd_value));
}
void command_tokenize(char c){

  if (c == '\n'){
		if (strlen(cmd_label)){
			struct field *f = field_get(cmd_label);
	
			if (!f)  // some are not really fields but just updates, like QSO
     		field_set(cmd_label, cmd_value, false);
      else if (f->last_user_change + 1000 < now || f->type == FIELD_TEXT)
     		field_set(cmd_label, cmd_value, false);
			else
				Debug.printf("skipped %s= %s %uv:%u\n", cmd_label, cmd_value, f->last_user_change, now);
			if (!strcmp(cmd_label, "HIGH") || !strcmp(cmd_label, "LOW") 
				|| !strcmp(cmd_label, "PITCH")
				|| !strcmp(cmd_label, "SPAN") || !strcmp(cmd_label, "MODE"))
				set_bandwidth_strip();	
    }
		reset_tokenizer();
  }
  else if (!cmd_in_field){ // only:0 handle characters between { and }
		//Serial.println("\n\n\n\n****Reseting the tokenizer\n");
		reset_tokenizer();
	  return;
	}
  else if (cmd_in_label){
    //label is delimited by space
    if (c != ' ' && strlen(cmd_label) < sizeof(cmd_label)-1){
      int i = strlen(cmd_label);
      cmd_label[i++] = c;
      cmd_label[i]= 0;
    }
    else 
      cmd_in_label = false;
  }
  else if (!cmd_in_label && strlen(cmd_value) < sizeof(cmd_value) -1 ){
    int i = strlen(cmd_value);
    cmd_value[i++] = c;
    cmd_value[i] = 0;
  }
	else
		reset_tokenizer();
}

// I2c routines
// we separate out the updates with \n character
void send_text(char *text){

	if (!client.connected())
		return;

	int len = strlen(text);
	int written = client.print(text);
	if (written != len){
		Serial.println("short write, dropping tcp");
		client.stop();
	}
}

void send_updates(){
  char c;
	char buff[500];
	int update_count;
	static unsigned int next_adc_update = 0;

//	Serial.println("@");
	send_text("?\n");
 
	if (message_buffer[0]){
		Serial.println(message_buffer);
		send_text(message_buffer);
		message_buffer[0] = 0;
	}

 	update_count = 0;
	//check if any button has been pressed
  for (struct field *f = field_list; f->type != -1; f++){
    if (f->update_to_radio && f->type == FIELD_BUTTON){
			f->update_to_radio = false;
      sprintf(buff, "%s %s\n", f->label, f->value);
			send_text(buff);
			update_count++;
    }
	}
	if (update_count)
		return;
	//then the rest
  for (struct field *f = field_list; f->type != -1; f++){
    if (f->update_to_radio){
			f->update_to_radio = false;
      sprintf(buff, "%s %s\n", f->label, f->value);
			Debug.println(buff);
			send_text(buff);
			update_count++;
    }
	}

	if (update_count)
		return;

	unsigned int now = millis();
	if (next_adc_update <= now){
		sprintf(buff, "vbatt %d\npower %d\nvswr %d\n", vbatt, (vfwd * vfwd)/10, vswr);
  	send_text(buff);
		next_adc_update = now + 200;
	}
}

#define AVG_N 10 

void measure_voltages(){
  char buff[30];
  int f, r, b;

	static unsigned long next_reading_update = 0;
	unsigned long now = millis();

	if (now < next_reading_update)
		return;

	int af, ar, ab;
	af = analogRead(A0);
	ar = analogRead(A1);
	ab = analogRead(A2);
  f = (56 * af)/460;
  r = (56 * ar)/460;
  b = (500 * ab)/278;

	vbatt = b;

	if (f > vfwd)
		vfwd = f;
	else
  	vfwd = ((vfwd * AVG_N) + f)/(AVG_N + 1);

	if (r > vref)
		vref = r;
	else
  	vref = ((vref * AVG_N) + r)/(AVG_N + 1);

	vswr = (10*(vfwd + vref))/(vfwd-vref);
	
	//if (vfwd > 20)
	vfwd = 3 + ((vfwd)/2);

	// update only once in a while
	next_reading_update = now + 50;
}

void set_state(int new_state){
  if (new_state != stream_state){
    //Debug.printf("new state = %d\n", new_state);
//    if (new_state == STREAM_PLAYING){
//      mp3.flush();
//      mp3.begin();
//   }
//   else
//      mp3.end();
  }
  stream_state = new_state;
}

void wifi_init(){
	temp_ssid[0] = 0;
	temp_key[0] = 0;
}

// stores a successfully paired wifi ssid/key pair
static void wifi_save(char *new_ssid, char *new_key){
	Debug.println("block before:\n");
	block_dump();
	//first check if the already stored, if so, we just update it
	for (int i = 0; i < MAX_APS; i++)
		if (!strcmp(block.ap_list[i].ssid, new_ssid)){
			if (strcmp(block.ap_list[i].key, new_key)){
				strcpy(block.ap_list[i].key, new_key);
				Debug.printf("updated the previous key %s with %s\n", new_ssid, new_key);
				block_write();
			}
      else
        Debug.printf("ssid key is not updated.\n");
      return;
		}
	//now, we have to shift out one ssid and add this
	for (int i = MAX_APS -1; 0 < i; i--){
		Debug.printf("shifting ap %d to %d\n", i, i-1);
		strcpy(block.ap_list[i].ssid, block.ap_list[i-1].ssid);
		strcpy(block.ap_list[i].key, block.ap_list[i-1].key);
	}
	Debug.printf("inserted new ap %s\n", new_ssid)	;
	strcpy(block.ap_list[0].ssid, new_ssid);
	strcpy(block.ap_list[0].key, new_key);
	block_write();
	block_dump();
}


struct field *ui_slice(){
  uint16_t x, y;
	struct field *f_touched = NULL;

	if (now > last_blink + BLINK_RATE){
		field_blink(-1);
		last_blink = now;
	}

  // check the encoder state
	if (digitalRead(ENC_S) == HIGH && encoder_switch == true){
			encoder_switch = false;		
	}
	if (digitalRead(ENC_S) == LOW && encoder_switch == false){
		encoder_switch = true;
		field_input(ZBITX_KEY_ENTER);
	}

	int step_size = 3;
  if (f_selected && !strcmp(f_selected->label, "FREQ"))
    step_size = 1;

	if (wheel_move > step_size){
    field_input(ZBITX_KEY_UP);
		wheel_move = 0;
		last_wheel_moved = now;
  }
  else if (wheel_move < -step_size){
    field_input(ZBITX_KEY_DOWN);
		wheel_move = 0;
		last_wheel_moved = now;
  }
  //redraw everything
  field_draw_all(false);

  if (!screen_read(&x, &y)){
      mouse_down = false;
    return NULL;
  }

  //check for user inputt
  struct field *f = field_at(x, y);
  if (!f)
    return NULL;
  //do selection only if the touch has started
  if (!mouse_down){
    field_select(f->label);
		if (f->type == FIELD_FT8)
			field_tapped(f, x, y);
		next_repeat_time = millis() + 1500;
		f_touched = f;
	}
	else if (next_repeat_time < millis() && f->type == FIELD_KEY){
    field_select(f->label);
		next_repeat_time = millis() + 300;
	}
     
  mouse_down = true;
	return f_touched; //if any ...
}


// the setup function runs once when you press reset or power the board
void setup1() {
	block_read();
  screen_init();
  field_init();
  field_clear_all();
  command_init();
  field_set("MODE","CW", false);

	strcpy(temp_ssid, "zbitx");
	strcpy(temp_key, "zbitx12345");

	pinMode(ENC_S, INPUT_PULLUP);
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  encoder_state = digitalRead(ENC_A) + (2 * digitalRead(ENC_B));

	attachInterrupt(ENC_A, on_enc, CHANGE);
	attachInterrupt(ENC_B, on_enc, CHANGE);

	field_set("9", "zBitx firmware v4.00 2026-04-27\nWaiting for the zbitx wifi...\n", false);

	if (digitalRead(ENC_S) == LOW)
		reset_usb_boot(0,0); //invokes reset into bootloader mode
}

void loop1(void) {
	static uint32_t next_update = 0;

	now = millis();
  core1_time = millis();
	ui_slice();
  measure_voltages();
  delay(100);
}

/*
	Communications loop
*/

void wifi_poll(){
	static bool wifi_connected = false;
	static bool begin_issued = false;
	static unsigned long begin_started = 0;

	if (WiFi.status() == WL_CONNECTED){
		if (!wifi_connected){
			field_set("9", "WiFi is connected to the radio\n", false);
			Debug.println("Online");
			set_state(STREAM_WIFI_ONLINE);
			wifi_connected = true;
			begin_issued = false;
		}
		else if (stream_state == STREAM_WIFI_OFFLINE)
			set_state(STREAM_WIFI_ONLINE);
		return;
	}

	if (wifi_connected){
		field_set("9", "WiFi is disconnected\n", false);
		set_state(STREAM_WIFI_OFFLINE);
		wifi_connected = false;
		begin_issued = false;
	}

	if (!begin_issued){
		Serial.println("wifi begin");
		set_state(STREAM_WIFI_CONNECTING);
		WiFi.mode(WIFI_STA);
		WiFi.begin("zbitx", "zbitx12345");
		begin_started = millis();
		begin_issued = true;
		return;
	}

	// association in progress — give it time, don't tear down
	if (millis() - begin_started > WIFI_CONNECT_TIMEOUT_MS){
		Serial.println("wifi connect timed out, retrying");
		WiFi.disconnect();
		begin_issued = false;
	}
}


void setup(){
	message_buffer[0] = 0;
	Serial1.setTX(16);
  Serial1.setRX(17);
  Debug.begin(115200);

  while (!Debug && millis() < 3000)
		NULL;
	Debug.println("booting zbitx front panel 4.01 2026/04/17");
	wifi_init();
	block_dump();
}

void loop() {
  size_t mp3available, netavailable, bytes_to_read;
	static uint32_t next_update = 0;

  wifi_poll();
  core1_check();
	delay(50);

	//if the client is connected
	if (WiFi.status() != WL_CONNECTED){
	//	Debug.println(__LINE__);
		return;
	}

	if (!client.connected()){
		Serial.println("trying connect to tcp");
		if (!client.connect(host, port)){
			delay(1000);
			return;
		}
		Debug.println("Connected to the remote\n");
		field_set("9", "Connected to the remote!\n", false);
		client.setTimeout(10000);
	}

	netavailable = client.available();
	bytes_to_read = sizeof(buff);

	if (netavailable > 0){
		if (bytes_to_read > netavailable)
			bytes_to_read = netavailable;
		int start = millis();
		size_t actually_read = client.readBytes(buff, bytes_to_read);
		buff[actually_read] = 0;
		//Serial.printf("tokenizing<<<<<\n%s\n>>>>>>>\n", buff);
		for (int i = 0; i < actually_read; i++)
			command_tokenize(buff[i]);
		//Serial.printf("%d in %d\n", actually_read, millis() - start);
	}

	unsigned int now = millis();
  core1_time = millis();

	if (next_update < now){
		//these can be the result of moues or encoder inputs
		send_updates();
		next_update = now + 200;
	}
}

