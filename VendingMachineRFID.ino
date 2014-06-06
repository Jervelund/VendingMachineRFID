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

#define EEPROM_SIZE 1024

//#include <SoftwareSerial.h>
//SoftwareSerial Serial2(2, 3); // RX, TX

#define TRANSMISSION_ATTEMPTS 10
#define TRANSMISSION_REPEATS 5
#define TRANSMISSION_SPACE 95
#define TRANSMISSION_ATOM_SIZE 3

uint8_t recieve_error;

MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance.
uint32_t lastCardScan; // Debounce RFID reads
const uint8_t timeBetweenScans = 200;
uint16_t credits_in_machine;//Serial.parseInt();

const uint8_t speakerOutPin = A1;

void setup() {
  //Serial2.begin(19200);

  // Set this to 1 to set the entire EEPROM to 0 (default is 0xFF)
#if 0
  for (uint16_t i = 0; i < EEPROM_SIZE; i++)
    EEPROM_writeAnything(i, 0);
  Serial2.println(F("EEPROM cleared"));
  while (1);
#endif

  //Serial2.println(F("Started"));

  lastCardScan = 0;

  Serial.begin(57600); // Initialize serial communications with master vending Arduino
  Serial.setTimeout(200);

  SPI.begin(); // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522 card
  pinMode(speakerOutPin, OUTPUT); // speaker out
  BEEP(1);
}

uint16_t rfid_raw_read() {
  char parseBuffer[TRANSMISSION_REPEATS * TRANSMISSION_ATOM_SIZE];
  uint16_t number = 0;
  recieve_error = 1; // set default to error
  if (Serial.readBytes(parseBuffer, sizeof(parseBuffer)) == sizeof(parseBuffer)) {
    // Received correct amount of bytes - check message integrity
    if (memcmp(parseBuffer, parseBuffer + TRANSMISSION_ATOM_SIZE, sizeof(parseBuffer) - TRANSMISSION_ATOM_SIZE) == 0) {
      if (parseBuffer[2] == TRANSMISSION_SPACE) {
        // OK
        recieve_error = 0;
        memcpy(&number, parseBuffer, sizeof(number));
        // Remember to notify other party that we do not need a retransmission
      }
    }
  }
  return number;
}

uint16_t rfid_recieve(unsigned char command) {
  char parseBuffer[TRANSMISSION_REPEATS * TRANSMISSION_ATOM_SIZE];
  uint8_t recieveAttemptsRemaining = TRANSMISSION_ATTEMPTS;
  uint16_t number = 0;
  while (recieveAttemptsRemaining-- > 0) {
    // Flush incoming buffer
    while (Serial.available()) {
      Serial.read();
      delay(5);
    }
    // Request and read data from other party
    Serial.write(command);
    while (!Serial.available()); // Wait till other side transmits a C
    if (Serial.read() != 'C')
      return;
    number = rfid_raw_read();
    if (recieve_error == 0) { // Received correctly, return value
      return number;
    }
    delay(5);
    // Received incorrect amount of bytes, or an error occurred
  }
  recieve_error = 1; // timed out
  return number;
}

char rfid_raw_transmit(uint16_t number) {
  for (int i = 0; i < TRANSMISSION_REPEATS * TRANSMISSION_ATOM_SIZE; i = i + TRANSMISSION_ATOM_SIZE) {
    Serial.write(number & 0xFF);
    Serial.write(number >> 8);
    Serial.write(TRANSMISSION_SPACE);
  }
}

char rfid_transmit(unsigned char command, uint16_t number) {
  char ack_buffer[3];
  uint8_t transmitAttemptsRemaining = TRANSMISSION_ATTEMPTS;
  while (transmitAttemptsRemaining-- > 0) {
    // Flush incoming buffer
    while (Serial.available()) {
      Serial.read();
    }
    // Transmit data
    Serial.write(command);
    rfid_raw_transmit(number);
    // Verify command was transmitted correctly
    if (Serial.readBytes(ack_buffer, 3) == 3) { // If we receive all three bytes
      if (ack_buffer[0] == command && memcmp(ack_buffer + 1, &number, sizeof(number)) == 0) //  check if they match with message
        return 0;
    }
  }
  return 1;
}

void loop() {
  // Look for new cards
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  // Select one of the cards
  if (!mfrc522.PICC_ReadCardSerial()) return;
  // Now a card is selected. The UID and SAK is in mfrc522.uid.

  // Dump UID
  if (millis() - lastCardScan > timeBetweenScans) {
    lastCardScan = millis();
    // Some user feedback
    BEEP(1);
    // Init card
    card_t card;
    // Copy card uid
    memcpy(&card.uid, &mfrc522.uid.uidByte, sizeof(card_uid_t));
    uint16_t addr = findCardOffset(card);
    uint16_t credit_addr = addr + sizeof(card_uid_t);
    // Flush incoming buffer
    //while(Serial.available()){Serial.read();}
    // Fetch current credit count for the vending machine, in order to know whether to withdraw or deposit
    credits_in_machine = rfid_recieve('C');
    //Serial.println(addr);
    // Attempt to receive response from vending machine
    // If no response from has been received within a second, assume it is not powered/booted yet, stop processing
    if (recieve_error == 1) {
      BEEP(4);
      return;
    }

    if (credits_in_machine == 0) { // Attempt withdrawal               ## WITHDRAW
      if (addr == 0xFFFF) { // New card!
        // ERR Beep + display "NO CREDITS"?
        //Serial.println("NO CREDITS - UNKNOWN CARD");
        Serial.write('N');
        BEEP(1);
        // Halt PICC
        mfrc522.PICC_HaltA();
        // Return
        return;
      } else { // Known card!
        // Card found!
        // Withdraw from card
        EEPROM_readAnything(addr + sizeof(card_uid_t), card.credits);
        if (card.credits == 0) {
          // ERR Beep + display "NO CREDITS"?
          //Serial.println("NO CREDITS - KNOWN CARD");
          Serial.write('N');
          BEEP(1);
          // Halt PICC
          mfrc522.PICC_HaltA();
          // Return
          return;
        }
        uint16_t current_credits = card.credits;
        card.credits = 0;
        // Attempt to save new card info
        if (updateAndVerify(addr, card)) {
          // Check if card has been changed
          // OK Beep + Set credits on vending machine
          char error = rfid_transmit('S', current_credits);
          if (error != 0) {
            // Could not update machine credits - attempt to save credits to card again!
            card.credits = current_credits;
            updateAndVerify(addr, card);
            BEEP(4);
          }// else {BEEP(1);}
        } else {
          // ERR Beep + display "ERR  EEPROM BAD" ?
          //Serial.println("ERR  EEPROM BAD");
          Serial.write('B');
          BEEP(4);
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
      // Zero credits in machine - done now to avoid race-conditions of people buying when saving credits
      Serial.write("ZZZZZ"); // Make sure the machine is zeroed, do it five times to be sure
      if (addr == 0xFFFF) { // New card!
        // Attempt to find free spot
        //Serial.println("Unknown card - search for free spot");
        addr = findFreeCardSpot();
        if (addr == 0xFFFF) {
          // Reset credits on vending machine
          rfid_transmit('S', credits_in_machine);
          // ERR Beep + display "ERR  OUT OF MEMORY" ?
          //Serial.println("ERR  OUT OF MEMORY");
          Serial.write('O');
          BEEP(4);
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
        EEPROM_readAnything(addr + sizeof(card_uid_t), card.credits);
      }
      card.credits += credits_in_machine;
      // Card found! (Known card OR found free spot for new card)
      // Attempt to add credits to card
      if (updateAndVerify(addr, card)) {
        // OK Beep
        //BEEP(1);
      } else {
        // Reset credits on vending machine
        rfid_transmit('S', credits_in_machine);
        // ERR Beep + display "ERR  EEPROM BAD" ?
        //Serial.println("ERR  EEPROM BAD");
        Serial.write('B');
        BEEP(4);
        // TODO: Move card + mark sector as bad.
        // Attempt to move card?
        // Sector could be marked as dead by setting all bytes to 0xFE
      }
    }
  }
  // Halt PICC
  mfrc522.PICC_HaltA();
}

bool updateAndVerify(uint16_t addr, card_t card) {
  EEPROM_updateAnything(addr, card);
  if (EEPROM_compareAnything(addr, card)) {
    //Serial.print("Update OK - new credits:");
    //Serial.println(card.credits);
    return true;
  }
  //Serial.print("Update verify error at address: ");
  //Serial.println(addr);
  return false;
}

/* Returns address of card - returns 0xFFFF if card is not found */
int16_t findCardOffset(const card_t c) {
  uint16_t addr;
  uint16_t last_addr = EEPROM_SIZE - sizeof(card_t); // EEPROM size minus element size
  for (addr = 0; addr <= last_addr; addr += sizeof(card_t)) {
    if (EEPROM_compareAnything(addr, c.uid)) { // If bytes at address match, ok!
      //Serial.println(addr);
      return addr;
    }
  }
  return 0xFFFF; // Error value
}

int16_t findFreeCardSpot() {
  card_credits_t credits = 0;
  uint16_t addr = sizeof(card_uid_t); // Offset pointer, so we are looking at the card.credits
  uint16_t last_addr = EEPROM_SIZE - sizeof(card_credits_t); // EEPROM size minus size of one element
  for (addr; addr <= last_addr; addr += sizeof(card_t)) {
    if (EEPROM_compareAnything(addr, credits)) { // If bytes at address match, ok!
      //Serial.println(addr-sizeof(card_uid_t));
      return addr - sizeof(card_uid_t); // Offset pointer again, so it points to beginning of card_t
    }
  }
  return 0xFFFF; // Error value
}

int tone_ = 0;

void BEEP(uint8_t n) {
  unsigned char delayms = 100;
  beepp(delayms);
  for (uint8_t i = 1; i < n; i++) {
    delay(delayms);
    beepp(delayms);
  }
}

void beepp(unsigned char delayms) {
  digitalWrite(speakerOutPin, HIGH);     // Almost any value can be used except 0 and 255
  // experiment to get the best tone
  delay(delayms);          // wait for a delay ms
  digitalWrite(speakerOutPin, LOW);       // 0 turns it off
}
