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
#include "h.h"

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance.
uint32_t lastCardScan; // Debounce RFID reads
uint32_t timeBetweenScans;

void setup() {
  uint32_t lastCardScan = 0;
  uint32_t timeBetweenScans = 1000; // TODO: Set to 1500 when in production
  Serial.begin(9600); // Initialize serial communications with master vending arduino
  SPI.begin(); // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522 card
  Serial.print("Sizeof(card): ");
  Serial.println(sizeof(card_t));
  Serial.print("Sizeof(card_uid_t): ");
  Serial.println(sizeof(card_uid_t));
  Serial.print("Sizeof(card_credits_t): ");
  Serial.println(sizeof(card_credits_t));
  Serial.println("Ready");
}

void loop() {
  // Look for new cards
  if(!mfrc522.PICC_IsNewCardPresent()) {return;}
  Serial.println("NEW CARD");
  // Select one of the cards
  if(!mfrc522.PICC_ReadCardSerial()) {return;}
  // Now a card is selected. The UID and SAK is in mfrc522.uid.

  // Dump UID
    if(millis()-lastCardScan > timeBetweenScans){
      lastCardScan = millis();
      cardSwiped();

      // Init card
      card_t card;
      // Copy card uid
      memcpy(&card.uid, &mfrc522.uid.uidByte, sizeof(card_uid_t));
      uint16_t addr = findCardOffset(card);

      // Fetch current credit count for the vending machine, in order to know whether to withdraw or deposit
      Serial.print("C ");
      Serial.println(addr);
      // Wait for print to compelete
      //Serial.flush();
      // Recieve response
      uint16_t credits_in_machine = Serial.parseInt();
      Serial.print("Recieved: ");
      Serial.println(credits_in_machine);

      if(credits_in_machine == 0){ // Attempt withdrawal                                          ## WITHDRAW
        if(addr == 65535){ // New card!
          // ERR Beep + display "NO CREDITS"?
          Serial.println("NO CREDITS - UNKNOWN CARD");
          // Halt PICC
          mfrc522.PICC_HaltA();
          // Return
          return;
        } else { // Known card!
          // Card found!
          // Withdraw from card + set credits to zero
          uint16_t credit_addr = addr+sizeof(card_uid_t);
          Serial.print("Z ");
          Serial.print(addr);
          Serial.print(" + ");
          Serial.print(sizeof(card_uid_t));
          Serial.print(" = ");
          Serial.println(credit_addr);
          EEPROM_readAnything(credit_addr,card.credits);
          if(card.credits == 0){
            // ERR Beep + display "NO CREDITS"?
            Serial.println("NO CREDITS - KNOWN CARD");
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
            Serial.println(current_credits);
          } else {
            // ERR Beep + display "ERR  EEPROM BAD" ?
            Serial.println("ERR  EEPROM BAD");
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Return
            return;
            // TODO: Move card + mark sector as bad.
            // Attempt to move card?
            // Sector could be marked as dead by setting all bytes to 0xFE
          }
        }
      } else { // Attempt deposit                                                                 ## DEPOSIT
        // Zero credits in machine - done now to avoid race-conditions of people buying when saving credits
        if(addr == 65535){ // New card!
          // Attempt to find free spot
          Serial.println("Search for free spot");
          addr = findFreeCardSpot();
          if(addr == 65535){
            // ERR Beep + display "ERR  OUT OF MEMORY" ?
            Serial.println("ERR  OUT OF MEMORY");
            // Restore current credits
            Serial.print("S");
            Serial.println(card.credits);
            // Halt PICC
            mfrc522.PICC_HaltA();
            // Return
            return;
          }
          // Valid free spot found!
          // !!!!!! IMPORTANT TO SET CREDITS TO ZERO !!!!!!
          //  Else the value at card.credits will be added
          card.credits = 0;
          Serial.print("Credits on card: ");
          Serial.println(card.credits);
          
          Serial.print("Credits on machine: ");
          Serial.println(credits_in_machine);

          card.credits += credits_in_machine;
          
          Serial.print("Credits total: ");
          Serial.println(card.credits);
        } else { // Known card
          // Fetch current credits
          uint16_t credit_addr = addr+sizeof(card_uid_t);
          //Serial.print("Z ");
          //Serial.print(addr);
          //Serial.print(" + ");
          //Serial.print(sizeof(card_uid_t));
          //Serial.print(" = ");
          //Serial.println(credit_addr);
          EEPROM_readAnything(credit_addr,card.credits);
          //Serial.print("Credits on card: ");
          //Serial.println(card.credits);
          
          //Serial.print("Credits on machine: ");
          //Serial.println(credits_in_machine);

          card.credits += credits_in_machine;
          
          //Serial.print("Credits total: ");
          //Serial.println(card.credits);
        }
        // Card found! (Known card OR found free spot for new card)
        // Add credits in machine

        if(updateAndVerify(addr,card)){
          // OK Beep + Set credits on vending machine
        } else{
          // ERR Beep + display "ERR  EEPROM BAD" ?
          Serial.println("ERR  EEPROM BAD");
          Serial.print("S");
          Serial.println(credits_in_machine);
          // Halt PICC
          mfrc522.PICC_HaltA();
          // Return
          return;
          // TODO: Move card + mark sector as bad.
          // Attempt to move card?
          // Sector could be marked as dead by setting all bytes to 0xFE
        }
      }
    } else Serial.println("DEBOUNCE");

  // Halt PICC
  mfrc522.PICC_HaltA();
}

bool updateAndVerify(uint16_t addr, card_t card){
  Serial.print("Setting card at :");
  Serial.print(addr);
  Serial.print("   to credits: ");
  Serial.println(card.credits);
  if(EEPROM_compareAnything(addr,card)){
    Serial.println("Update OK - No changes:");
    return true;
  }
  //EEPROM_updateAnything(addr,card);
  if(EEPROM_compareAnything(addr,card)){
    Serial.print("Update OK - new credits:");
    Serial.println(card.credits);
    return true;
  }
  Serial.print("Update verify error at address: ");
  Serial.println(addr);
  return false;
}

void cardSwiped(){
  if(mfrc522.uid.size >= 4){ // Check if card serial is long enough - 4 is MIFARE classic, 7 MIFARE ultralight (see MFRC533.cpp)
    printCard();
    printPICC();
  }
}

void printCard(){
  Serial.print("Card UID:");
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
    } 
    Serial.println();
}

void printPICC(){
      // Dump PICC type
    byte piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.print("PICC type: ");
    Serial.println(mfrc522.PICC_GetTypeName(piccType));
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI 
     && piccType != MFRC522::PICC_TYPE_MIFARE_1K
     && piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
      Serial.println("This sample only works with MIFARE Classic cards.");
      return;
    }
}

/* Returns address of card - returns 65535 if card is not found */
int16_t findCardOffset(const card_t c){
  uint16_t addr;
  uint16_t last_addr = 1024-sizeof(card_t); // EEPROM size minus element size
  for(addr = 0; addr <= last_addr; addr = addr + sizeof(card_t)){
    if(EEPROM_compareAnything(addr,c.uid)){ // If bytes at address match, ok!
      Serial.print("MATCH! known: ");
      Serial.println(addr);
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
      Serial.println("MATCH!  free: ");
      Serial.println(addr-sizeof(card_uid_t));
      return addr-sizeof(card_uid_t); // Offset pointer again, so it points to beginning of card_t
    }
  }
  return 65535; // Error value
}
