#include <ArduinoWebsockets.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include "printf.h"
#include "RF24.h"
#include <Servo.h>

String IP_ADRESS = "192.168.8.148";
enum { READY,
       AUTHENTICATING,
       EVALUATING,
       WAITING_FOR_RFID,
       ADDING_RFID,
       WAITING_FOR_NAME } state = READY;

using namespace websockets;

WebsocketsClient attendanceSocket;
WebsocketsClient rfidSocket;

const char* ssid = "Brzzi";  // The SSID (name) of the Wi-Fi network you want to connect to
const char* password = "131000041103";

#define CE_PIN 4
#define CSN_PIN 5
RF24 radio(CE_PIN, CSN_PIN);
Servo servo;

uint8_t address[][6] = { "1Node", "2Node" };

bool radioNumber = 0;

bool role = false;

byte uuid[10];
byte name[32] = { NULL };
char* message = "#ADD_USER";


struct attendanceData {
  float BAC;
  float TEMP;
} attendance;

void setup() {
  pinMode(15, OUTPUT);
  servo.attach(15);
  pinMode(0, OUTPUT);
  pinMode(16, OUTPUT);
  pinMode(2, OUTPUT);
  digitalWrite(0, LOW);
  digitalWrite(16, LOW);
  digitalWrite(2, LOW);
  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {}  // hold in infinite loop
  }
  // Set the PA Level low to try preventing power supply related problems
  // because these examples are likely run with nodes in close proximity to
  // each other.
  radio.setPALevel(RF24_PA_LOW);  // RF24_PA_MAX is default.

  // save on transmission time by setting the radio to only transmit the
  // number of bytes we need to transmit a float
  radio.setPayloadSize(sizeof(uuid));  // float datatype occupies 4 bytes

  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(address[radioNumber]);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(1, address[!radioNumber]);  // using pipe 1
  // radio.setChannel(150);

  // additional setup specific to the node's role
  if (role) {
    radio.stopListening();  // put radio in TX mode
  } else {
    radio.startListening();  // put radio in RX mode
  }

  WiFi.begin(ssid, password);  // Connect to the network
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.println(" ...");

  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
    delay(1000);
    Serial.print(++i);
    Serial.print(' ');
  }

  Serial.println('\n');
  Serial.println("Connection established!");
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());

  bool connected = false;
  while (!connected) {
    Serial.println("WS connecting");
    String connection = "ws://" + IP_ADRESS + ":8888/evidence_ws";
    connected = attendanceSocket.connect(connection);
    delay(500);
  }
  Serial.println("Attendance socket connected");

  attendanceSocket.onMessage([&](WebsocketsMessage recievedMessage) {
    const char* receivedData = recievedMessage.c_str();

    strncpy((char*)name, receivedData, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    radio.stopListening();
    radio.setPayloadSize(sizeof(name));
    state = AUTHENTICATING;
  });


  connected = false;
  while (!connected) {
    Serial.println("WS connecting");
    String connection = "ws://" + IP_ADRESS + ":8888/rfid_ws";
    connected = rfidSocket.connect(connection);
    delay(500);
  }
  Serial.println("Rfid socket connected");
  rfidSocket.onMessage([&](WebsocketsMessage recievedMessage) {
    if (strcmp(recievedMessage.c_str(), "#ADD_USER") == 0) {
      radio.stopListening();
      radio.setPayloadSize(strlen(message) + 1);
      state = ADDING_RFID;
    }
  });
}

void loop() {

  if (state == READY) {
    if (rfidSocket.available()) {
      rfidSocket.poll();
    }
    if (radio.available()) {

      uint8_t bytes = radio.getPayloadSize();
      radio.read(&uuid, bytes);
      digitalWrite(0, HIGH);
      radio.stopListening();
      radio.setPayloadSize(sizeof(name));
      String uuidStr = "";
      for (int i = 0; i < bytes; i++) {
        // Convert each byte to a hexadecimal string and append
        if (uuid[i] < 0x10) uuidStr += "0";  // Leading zero for single digit hex
        uuidStr += String(uuid[i], HEX);
      }
      attendanceSocket.send(uuidStr);
      state = WAITING_FOR_NAME;
    }
    delay(200);
  }

  if (state == WAITING_FOR_NAME) {
    if (attendanceSocket.available()) {
      attendanceSocket.poll();
    }
  }

  if (state == ADDING_RFID) {
    bool report = radio.write(message, strlen(message) + 1);
    if (report) {
      radio.startListening();
      radio.setPayloadSize(sizeof(uuid));
      state = WAITING_FOR_RFID;
    } else {
      Serial.println(F("Transmission failed or timed out"));  // payload was not delivered
      delay(300);
    }
  }

  if (state == WAITING_FOR_RFID) {
    if (radio.available()) {
      // is there a payload? get the pipe number that recieved it
      uint8_t bytes = radio.getPayloadSize();  // get the size of the payload
      radio.read(&uuid, bytes);
      String uuidStr = "";
      for (int i = 0; i < bytes; i++) {
        // Convert each byte to a hexadecimal string and append
        if (uuid[i] < 0x10) uuidStr += "0";  // Leading zero for single digit hex
        uuidStr += String(uuid[i], HEX);
      }

      // Send the string via WebSocket
      rfidSocket.send(uuidStr.c_str());
      radio.startListening();
      state = READY;
    }
  }

  if (state == AUTHENTICATING) {
    bool report = radio.write(&name, sizeof(name));
    if (report) {
      Serial.print(F("Transmission successful! "));  // payload was delivered
      if (strcmp((char*)name, "Ulaz zabranjen") == 0) {
        digitalWrite(0, LOW);
        digitalWrite(16, HIGH);
        delay(3000);
        digitalWrite(16, LOW);
        radio.setPayloadSize(sizeof(uuid));
        state = READY;
      } else if (strcmp((char*)name, "Izlaz odobren") == 0) {
        digitalWrite(0, LOW);
        digitalWrite(2, HIGH);
        servo.write(180);
        delay(3000);
        digitalWrite(2, LOW);
        servo.write(-180);
        radio.setPayloadSize(sizeof(uuid));
        state = READY;
      } else {
        radio.setPayloadSize(sizeof(attendance));
        state = EVALUATING;
      }
      radio.startListening();
      memset(name, NULL, 32);
      memset(uuid, NULL, 10);
    } else {
      Serial.println(F("Transmission failed or timed out"));  // payload was not delivered
      delay(300);
    }
  }

  if (state == EVALUATING) {
    if (radio.available()) {
      // is there a payload? get the pipe number that recieved it
      uint8_t bytes = radio.getPayloadSize();  // get the size of the payload
      radio.read(&attendance, bytes);
      digitalWrite(0, LOW);
      Serial.println(attendance.TEMP);
      if (attendance.BAC < 0.5 && attendance.TEMP < 37) {
        digitalWrite(2, HIGH);
        servo.write(180);
        delay(3000);
        digitalWrite(2, LOW);
        servo.write(-180);
        String message = "#DATA: { BAC:" + String(attendance.BAC) + ", TEMP:" + String(attendance.TEMP) + "}";
        attendanceSocket.send(message);
      } else {
        digitalWrite(16, HIGH);
        delay(3000);
        digitalWrite(16, LOW);
      }
      radio.setPayloadSize(sizeof(uuid));
      state = READY;
    }
  }
}