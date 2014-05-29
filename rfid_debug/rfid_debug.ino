/*
 * This sketch is used to debug the RFID module for an arduino based vending machine by simulation
 * RFID module for ArduinoVendingMachine - https://github.com/Lauszus/ArduinoVendingMachine
 * Developed by: Sigurd Jervelund Hansen - https://github.com/Jervelund/VendingMachineRFID
 * MIT license
 */

uint16_t counter; // Counter for the credit currently in the machine and the total value of coins that have been put into the machine
int led = 13;

void setup() {
  Serial.begin(19200); // Initialize serial communications with master vending arduino
  Serial.setTimeout(400);
  pinMode(led, OUTPUT);
  counter = 30000;
}

#define transmission_attempts 10
#define transmission_repeats 5
uint8_t recieve_error;
uint16_t rfid_recieve(){
  char parseBuffer[transmission_repeats*2];
  uint8_t recieveAttemptsRemaining = transmission_attempts;
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
  uint8_t transmitAttemptsRemaining = transmission_attempts;
  while(waiting_for_ok && transmitAttemptsRemaining-- > 0){
    // Flush incoming buffer
    while(Serial.available()){Serial.read();}
    for (int i = 0; i < transmission_repeats*2; i = i+2) {
      Serial.write(sencond_half);
      Serial.write(first_half);
    }
    if(Serial.readBytes(&waiting_for_ok,1) == 0) // No bytes recieved
      waiting_for_ok = 1; // Overwrite waiting_for_ok - necessary?
  }
  return waiting_for_ok;
}

void loop() {
  if (Serial.available()) {
    int input = Serial.read();
    if(input == 'C'){ // Fetch current credits
      rfid_transmit(counter);
    }
    else if(input == 'S'){ // Set current credits
      uint16_t temp_counter = rfid_recieve();
      if(recieve_error == 0) // If credits recieved correctly, update counter
        counter = temp_counter;
    }
    else if(input == 'Z'){ // Zero current credits
      counter = 0;
    }
  }
  if(counter == 0)
    digitalWrite(led, LOW);
  else
   digitalWrite(led, HIGH);
}

