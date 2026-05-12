#include <Arduino.h>
#include <WiFi.h>
#include <EEPROM.h>
#include "zbitx.h"

struct saved block;
boolean block_dirty = false;

bool block_read(){
  uint8_t buff[512];
  int index = 0, i;

  EEPROM.begin(512);

//  Debug.println("reading block");
  byte *p = (byte *)buff;
  for (int i = 0; i < sizeof(block); i++){
    byte b = EEPROM.read(i);
    *p++ = b;  
  }
 
  struct saved *q = (struct saved *)buff;
  if (q->magic != 0x00C0FFEE){
    Serial.println("block was uninitialized");
    memset((void *)&block, 0, sizeof(block));
    return false;
  }
  else {
    memcpy((void *)&block, buff, sizeof(block));
  }

  //check if zbitx is an ap 
  bool zbitx_found = false;
  for (int i = 0; i < MAX_APS; i++){
    if (!strcmp(block.ap_list[i].ssid, "zbitx"))
      zbitx_found = true;
  }
  if (zbitx_found == false){
    strcpy(block.ap_list[MAX_APS-1].ssid, "zbitx");
    strcpy(block.ap_list[MAX_APS-1].key, "zbitx12345");
  }
  block_dump();
  return true;
}

//don't do this too often
void block_write(){
  uint8_t buff[512];
  int index = 0, i;

  //read the last saved
  for (i = 0; i < sizeof(buff); i++)
    buff[i] = EEPROM.read(i++);

//  Debug.println("block checking");
  //if it is the same, don't do anything
  struct saved *p = (struct saved *)buff;
  if (p->magic == 0x00C0FFEE && !memcmp(&block, buff, sizeof(block))){
//    Debug.println("Block is unchanged and not updated");
    return;
  }

//  Debug.println("block writing");

  //write the block
  block.magic = 0x00C0FFEE;
  byte *q = (byte *)&block;
  for (int i = 0; i < sizeof(block); i++){
    byte b = *q++;
    EEPROM.write(i, b);
  }
//  Debug.printf("%d bytes of block written", i);
  EEPROM.commit();
//  Debug.println("block commited");
//  block_dump();
}

#define Serial1 Serial

void block_dump(){
  Serial1.printf("block magic id %x\n", block.magic);
  Serial1.printf("userid : %u\n", block.my_id);
  for (int i = 0; i < 6; i++)
    Serial1.printf("%d ", block.calibration_data[i]);
  Serial1.print("APs:\n");
  for (int i = 0; i < MAX_APS; i++)
    Serial1.printf("%d. [%s]->[%s]\n", i+1, block.ap_list[i].ssid, block.ap_list[i].key);
}
