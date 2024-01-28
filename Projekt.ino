enum { READY,
       SCANNED,
       WAITING_FOR_AUTH,
       ALC_PREPARE,
       TEMP_PREPARE,
       MEASURING_ALC,
       MEASURING_TEMP,
       EVALUATION,
       SENDING_RFID,
       SCANNING_RFID } state = READY;
// Define LCD screen
#include <LiquidCrystal.h>
LiquidCrystal lcd(2, 3, 4, 5, 6, 7);

// Define  RFID sensor
#include <SPI.h>
#include <MFRC522.h>
#define RST_PIN 9
#define SS_PIN 10
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Define pin of alcohol sensor
#define MQ3pin 0

// Define nRF
#include "printf.h"
#include "RF24.h"
#define CE_PIN A0
#define CSN_PIN 8
RF24 radio(CE_PIN, CSN_PIN);

uint8_t address[][6] = { "1Node", "2Node" };
bool radioNumber = 1;

bool role = true;

// Variable init
byte nuidPICC[4];
double R0 = 0.0828960723;
float maxBAC = 0;
float maxTemp = 0;
byte name[32];

struct attendanceData {
  float BAC;
  float TEMP;
} attendance;

char* add_user = "#ADD_USER";

long startTime;

void setup() {
  pinMode(A4, OUTPUT);
  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  while (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    delay(1000);
    // hold in infinite loop
  }

  radio.setPALevel(RF24_PA_LOW);  // RF24_PA_MAX is default.


  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(address[radioNumber]);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(1, address[!radioNumber]);  // using pipe 1
  // radio.setChannel(150);
  // additional setup specific to the node's role

  radio.startListening();  // put radio in RX mode
  radio.setPayloadSize(strlen(add_user) + 1);

  lcd.begin(16, 2);
  SPI.begin();         // Init SPI bus
  mfrc522.PCD_Init();  // Init MFRC522
  delay(10);           // Optional delay. Some board do need more time after init to be ready, see Readme
  mfrc522.PCD_DumpVersionToSerial();
  lcd.clear();
  lcd.print("Skeniraj");
  lcd.setCursor(0, 1);
  lcd.print("karticu...");
}

void loop() {

  if (state == READY) {
    if (radio.available()) {
      uint8_t bytes = radio.getPayloadSize();  // get the size of the payload
      radio.read(add_user, bytes);

      lcd.clear();
      lcd.print("Dodavanje");
      lcd.setCursor(0, 1);
      lcd.print("Korisnika");
      state = SCANNING_RFID;

      radio.stopListening();
      radio.setPayloadSize(sizeof(mfrc522.uid.uidByte));
    } else {
      // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
      if (!mfrc522.PICC_IsNewCardPresent()) {
        return;
      }

      // Select one of the cards
      if (!mfrc522.PICC_ReadCardSerial()) {
        return;
      }
      state = SCANNED;
      radio.stopListening();
      radio.setPayloadSize(sizeof(mfrc522.uid.uidByte));
      tone(A4, 1500, 150);
      lcd.clear();
      lcd.print("Autentifikacija");
    }
  }

  if (state == SCANNING_RFID) {
    if (!mfrc522.PICC_IsNewCardPresent()) {
      return;
    }

    // Select one of the cards
    if (!mfrc522.PICC_ReadCardSerial()) {
      return;
    }
    tone(A4, 1500, 150);
    state = SENDING_RFID;
  }

  if (state == SENDING_RFID) {

    unsigned long start_timer = micros();
    bool report = radio.write(&mfrc522.uid.uidByte, sizeof(mfrc522.uid.uidByte));
    unsigned long end_timer = micros();

    if (report) {
      Serial.print(F("Transmission successful! "));
      Serial.print(F("Time to transmit = "));
      Serial.print(end_timer - start_timer);
      Serial.print(F(" us. Sent: "));
      lcd.clear();
      lcd.print("Skeniraj");
      lcd.setCursor(0, 1);
      lcd.print("karticu...");
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      state = READY;
      radio.startListening();
      radio.setPayloadSize(strlen(add_user) + 1);
    }
  }

  if (state == SCANNED) {

    unsigned long start_timer = micros();                                          // start the timer
    bool report = radio.write(&mfrc522.uid.uidByte, sizeof(mfrc522.uid.uidByte));  // transmit & save the report
    unsigned long end_timer = micros();                                            // end the timer

    if (report) {
      Serial.print(F("Transmission successful! "));  // payload was delivered
      Serial.print(F("Time to transmit = "));
      Serial.print(end_timer - start_timer);  // print the timer result
      Serial.print(F(" us. Sent: "));

      state = WAITING_FOR_AUTH;

      radio.startListening();
      radio.setPayloadSize(sizeof(name));
    } else {
      Serial.println(F("Transmission failed or timed out"));  // payload was not delivered
      delay(300);
    }
  }

  if (state == WAITING_FOR_AUTH) {

    if (radio.available()) {
      uint8_t bytes = radio.getPayloadSize();  // get the size of the payload
      radio.read(&name, bytes);
      lcd.clear();
      String name_string = (char*)name;
      int spaceIndex = name_string.indexOf(" ");
      if (spaceIndex != -1) {
        lcd.print(name_string.substring(0, spaceIndex));
        lcd.setCursor(0, 1);
      }
      lcd.print(name_string.substring(spaceIndex + 1, name_string.length()));
      if (name_string == "Ulaz zabranjen") {
        tone(A4, 1500, 1000);
        delay(3000);
        radio.setPayloadSize(strlen(add_user) + 1);
        radio.startListening();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        lcd.clear();
        lcd.print("Skeniraj");
        lcd.setCursor(0, 1);
        lcd.print("karticu...");
        state = READY;
        memset(name, NULL, 32);

      } else if (name_string == "Izlaz odobren") {
        tone(A4, 1500, 100);
        delay(200);
        tone(A4, 1500, 100);
        delay(2800);
        radio.setPayloadSize(strlen(add_user) + 1);
        radio.startListening();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        lcd.clear();
        lcd.print("Skeniraj");
        lcd.setCursor(0, 1);
        lcd.print("karticu...");
        state = READY;
        memset(name, NULL, 32);

      } else {
        delay(3000);

        state = ALC_PREPARE;
        lcd.clear();
        lcd.print("Mjerenje");
        lcd.setCursor(0, 1);
        lcd.print("Alc. za ");
        lcd.setCursor(14, 1);
        lcd.print("3s");
        startTime = millis();
      }
    }
  }

  if (state == ALC_PREPARE) {
    float time = millis();
    if (time - startTime < 3000) {
      lcd.setCursor(14, 1);
      lcd.print(3 - (int)((time - startTime) / 1000));
    } else {
      lcd.clear();
      lcd.print("Pusi u senzor");
      lcd.setCursor(14, 1);
      lcd.print("5s");
      state = MEASURING_ALC;
      startTime = millis();
    }
  }

  if (state == MEASURING_ALC) {
    if (millis() - startTime < 5000) {
      int remaining = (millis() - startTime) / 1000;
      lcd.setCursor(14, 1);
      lcd.print(5 - remaining);
      lcd.print("s");
      int sensorValue = analogRead(A5);
      float sensorVolt = (float)sensorValue / 1024 * 5.0;
      float RS = (5.0 - sensorVolt) / sensorVolt;
      float BAC = 0.4 * pow(RS/R0,-1.430676558);
      if (BAC > maxBAC) {
        maxBAC = BAC;
      }
      delay(100);
    } else {
      lcd.clear();
      lcd.print(maxBAC);
      lcd.print(" mg/L");
      state = TEMP_PREPARE;
      delay(3000);
      lcd.clear();
      lcd.print("Mjerenje");
      lcd.setCursor(0, 1);
      lcd.print("Temp. za ");
      lcd.setCursor(14, 1);
      lcd.print("3s");
      startTime = millis();
    }
  }
  if (state == TEMP_PREPARE) {
    float time = millis();
    if (time - startTime < 3000) {
      lcd.setCursor(14, 1);
      lcd.print(3 - (int)((time - startTime) / 1000));
    } else {
      lcd.clear();
      lcd.print("Mjerenje Temp.");
      lcd.setCursor(14, 1);
      lcd.print("5s");
      state = MEASURING_TEMP;
      startTime = millis();
    }
  }

  if (state == MEASURING_TEMP) {
    if (millis() - startTime < 5000) {
      int remaining = (millis() - startTime) / 1000;
      lcd.setCursor(14, 1);
      lcd.print(5 - remaining);
      lcd.print("s");
      int sensorValue = analogRead(A2);
      float sensorVoltage = (sensorValue/1204.0)*5;
      float temp = (sensorVoltage-.5)*100+16;
      if (temp > maxTemp) {
        maxTemp = temp;
      }
      delay(100);
    } else {
      lcd.clear();
      lcd.print(maxTemp);
      lcd.print(" C");
      state = EVALUATION;
      delay(3000);
      radio.stopListening();
      attendance.BAC = maxBAC;
      attendance.TEMP = maxTemp;
      radio.setPayloadSize(sizeof(attendance));
    }
  }

  if (state == EVALUATION) {

    unsigned long start_timer = micros();
    bool report = radio.write(&attendance, sizeof(attendance));
    unsigned long end_timer = micros();

    if (report) {
      Serial.print(F("Transmission successful! "));
      Serial.print(F("Time to transmit = "));
      Serial.print(end_timer - start_timer);
      Serial.print(F(" us. Sent: "));
      lcd.clear();
      Serial.println(maxTemp);

      if (maxBAC < 0.5 && maxTemp < 37) {
        lcd.print("Ulaz dozvoljen");
        tone(A4, 1500, 100);
        delay(200);
        tone(A4, 1500, 100);
        delay(2800);
      } else {
        lcd.print("Ulaz zabranjen");
        tone(A4, 1500, 1000);
        delay(3000);
      }

      maxTemp = 0;
      maxBAC = 0;
      radio.setPayloadSize(strlen(add_user) + 1);
      radio.startListening();
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      lcd.clear();
      lcd.print("Skeniraj");
      lcd.setCursor(0, 1);
      lcd.print("karticu...");
      state = READY;
      memset(name, NULL, 32);
    } else {
      Serial.println(F("Transmission failed or timed out"));
      delay(300);
    }
  }
}
