/*
 * RFID module for ArduinoVendingMachine - https://github.com/Lauszus/ArduinoVendingMachine
 * Developed by: Sigurd Jervelund Hansen - https://github.com/Jervelund/VendingMachineRFID
 * MIT license
 */
#define SS_PIN 10
#define RST_PIN A0

#include <SPI.h>
#include <MFRC522.h> // MFRC522 library by Miguel Balboa (circuitito.com, Jan, 2012) & Søren Thing Andersen (access.thing.dk, fall of 2013)
#include "EEPROMAnything.h"
#include "VendingMachineRFID.h"

/* Hack to fix sizeof(card_uid_t) sometimes becoming 5 (?) */
// #define CARD_UID_SIZE 4
// Caused by Stream.parseInt();

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance.
uint32_t lastCardScan; // Debounce RFID reads
uint32_t timeBetweenScans;
uint16_t credits_in_machine;//Serial.parseInt();

void setup() {
  lastCardScan = 0;
  timeBetweenScans = 800; // TODO: Set to 1500 when in production
  Serial.begin(19200); // Initialize serial communications with master vending arduino
  Serial.setTimeout(400);
  SPI.begin(); // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522 card
}

uint8_t recieve_error;
uint16_t rfid_recieve(){
  char parseBuffer[10];
  uint8_t recieveAttemptsRemaining = 11;
  uint16_t number = 0;
  while(recieveAttemptsRemaining-- > 0){
    if(Serial.readBytes(parseBuffer, sizeof(parseBuffer)) == sizeof(parseBuffer)){
      // Recieved correct ammount of bytes - check message integrity
      if(memcmp(parseBuffer,parseBuffer+2,sizeof(parseBuffer)-2) == 0){
        // OK
        recieve_error = 0;
        // Let other party know we do not need a retransmission
        Serial.write(0);
        memcpy(&number,parseBuffer,sizeof(number));
        return number;
      }
    }
    // Recieved incorrect ammount of bytes - Retry only if we are going to run function again
    if(recieveAttemptsRemaining > 0)
      Serial.write(recieveAttemptsRemaining+48);
  }
  recieve_error = 1; // timed out
//number = 0;
  // Let other party know we do not need a retransmission
  Serial.write(0);
  return number;
}

char rfid_transmit(uint16_t number){
//uint16_t number;                      // 0001 0110 0100 0111
  uint16_t mask   = B11111111;          // 0000 0000 1111 1111
  uint8_t first_half   = number >> 8;   // >>>> >>>> 0001 0110
  uint8_t sencond_half = number & mask; // ____ ____ 0100 0111
  char waiting_for_ok = 1;
  uint8_t transmitAttemptsRemaining = 10;
  while(waiting_for_ok && transmitAttemptsRemaining-- > 0){
    // Flush incoming buffer
    while(Serial.available()){Serial.read();}
    for (int i = 0; i < 10; i = i+2) {
      Serial.write(sencond_half);
      Serial.write(first_half);
    }
    Serial.readBytes(&waiting_for_ok,1);
  }
  return waiting_for_ok;
}

void loop() {
  // Look for new cards
  if(!mfrc522.PICC_IsNewCardPresent()) return;
  // Select one of the cards
  if(!mfrc522.PICC_ReadCardSerial()) return;
  // Now a card is selected. The UID and SAK is in mfrc522.uid.

  // Dump UID
    if((millis()-lastCardScan) > timeBetweenScans){
      lastCardScan = millis();
      //cardSwiped();
      // Init card
      card_t card;
      // Copy card uid
      memcpy(&card.uid, &mfrc522.uid.uidByte, sizeof(card_uid_t));
      uint16_t addr = findCardOffset(card);
      uint16_t credit_addr = addr+4;

      // Flush incoming buffer
      while(Serial.available()){Serial.read();}
      // Fetch current credit count for the vending machine, in order to know whether to withdraw or deposit
      Serial.print("C");
      credits_in_machine = rfid_recieve();
      //Serial.println(addr);
      // Attempt to recieve response from vending machine
      // If no response from has been recieved within a second, assume it is not powered/booted yet, stop processing
      if(recieve_error == 1) return;
      /*
      if(Serial.readBytes(parseBuffer, sizeof(parseBuffer)) != 2){
        //Serial.print("No response - aborting.");
        return;
      }
      //Serial.println(parseBuffer[0],DEC);
      //Serial.println(parseBuffer[1],DEC);
      memcpy(&credits_in_machine,parseBuffer,sizeof(parseBuffer));*/
      // Zero credits in machine - done now to avoid race-conditions of people buying when saving credits
      Serial.print("ZZZZZ"); // Zero five times
      //Serial.println(credits_in_machine);

      if(credits_in_machine == 0){ // Attempt withdrawal               ## WITHDRAW
        if(addr == 65535){ // New card!
          // ERR Beep + display "NO CREDITS"?
          //Serial.println("NO CREDITS - UNKNOWN CARD");
          Serial.print("N");
          // Halt PICC
          mfrc522.PICC_HaltA();
          // Return
          return;
        } else { // Known card!
          // Card found!
          // Withdraw from card
          EEPROM_readAnything(addr+sizeof(card_uid_t),card.credits);
          if(card.credits == 0){
            // ERR Beep + display "NO CREDITS"?
            //Serial.println("NO CREDITS - KNOWN CARD");
            Serial.print("N");
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Return
            return;
          }
          uint16_t current_credits = card.credits;
          card.credits = 0;
          // Attempt to save new card info
          if(updateAndVerify(addr,card)){
            // Check if card has been changed
            // OK Beep + Set credits on vending machine
            Serial.print("S");
            char error = rfid_transmit(current_credits);
            if(error != 0){
              // Could not update machine credits - attempt to save credits to card again!
              card.credits = current_credits;
              updateAndVerify(addr,card);
            }
          } else {
            // ERR Beep + display "ERR  EEPROM BAD" ?
            //Serial.println("ERR  EEPROM BAD");
            Serial.print("B");
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Return
            return;
            // TODO: Move card + mark sector as bad.
            // Attempt to move card?
            // Sector could be marked as dead by setting all bytes to 0xFE
          }
        }
      } else { // Attempt deposit                                      ## DEPOSIT
        if(addr == 65535){ // New card!
          // Attempt to find free spot
          //Serial.println("Unknown card - search for free spot");
          addr = findFreeCardSpot();
          if(addr == 65535){
            // Reset credits on vending machine
            Serial.print("S");
            rfid_transmit(credits_in_machine);
            // ERR Beep + display "ERR  OUT OF MEMORY" ?
            //Serial.println("ERR  OUT OF MEMORY");
            Serial.print("O");
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Return
            return;
          }
          // Valid free spot found!
          // !!!!!! IMPORTANT TO SET CREDITS TO ZERO !!!!!!
          // Else the value currently at card.credits will be added
          card.credits = 0;
        } else { // Known card
          // Fetch current credits
          EEPROM_readAnything(addr+sizeof(card_uid_t),card.credits);
        }
        card.credits += credits_in_machine;
        // Card found! (Known card OR found free spot for new card)
        // Attempt to add credits to card
        if(updateAndVerify(addr,card)){
          Serial.print("C");
          // OK Beep
        } else{
          // Reset credits on vending machine
          Serial.print("S");
          rfid_transmit(credits_in_machine);
          // ERR Beep + display "ERR  EEPROM BAD" ?
          //Serial.println("ERR  EEPROM BAD");
          Serial.print("B");
          // TODO: Move card + mark sector as bad.
          // Attempt to move card?
          // Sector could be marked as dead by setting all bytes to 0xFE
        }
      }
    } //else Serial.println("DEBOUNCE");

  // Halt PICC
  mfrc522.PICC_HaltA();
}

bool updateAndVerify(uint16_t addr, card_t card){
  EEPROM_updateAnything(addr,card);
  if(EEPROM_compareAnything(addr,card)){
    //Serial.print("Update OK - new credits:");
    //Serial.println(card.credits);
    return true;
  }
  //Serial.print("Update verify error at address: ");
  //Serial.println(addr);
  return false;
}

/* Returns address of card - returns 65535 if card is not found */
int16_t findCardOffset(const card_t c){
  uint16_t addr;
  uint16_t last_addr = 1024-sizeof(card_t); // EEPROM size minus element size
  for(addr = 0; addr <= last_addr; addr = addr + sizeof(card_t)){
    if(EEPROM_compareAnything(addr,c.uid)){ // If bytes at address match, ok!
      //Serial.println(addr);
      return addr;
    }
  }
  return 65535; // Error value
}

int16_t findFreeCardSpot(){
  uint16_t credits = 0;
  uint16_t addr = sizeof(card_uid_t); // Offset pointer, so we are looking at the card.credits
  uint16_t last_addr = 1024-sizeof(card_t); // EEPROM size minus size of one element
  for(addr; addr <= last_addr; addr = addr + sizeof(card_t)){
    if(EEPROM_compareAnything(addr,credits)){ // If bytes at address match, ok!
      //Serial.println(addr-sizeof(card_uid_t));
      return addr-sizeof(card_uid_t); // Offset pointer again, so it points to beginning of card_t
    }
  }
  return 65535; // Error value
}
