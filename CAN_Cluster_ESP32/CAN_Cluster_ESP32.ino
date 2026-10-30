// ESP32 version of CAN_Cluster: data from SimHub over Bluetooth Classic (SPP),
// BMW E90 cluster driven by MCP2515 module over SPI.
// Requires the original ESP32 (S2/S3/C3/C6 have no Bluetooth Classic).
#include <BluetoothSerial.h>
#include "MCP2515.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED) || !defined(CONFIG_BT_SPP_ENABLED)
#error Bluetooth Classic (SPP) is not available - use the original ESP32 board.
#endif

#define lo8(x) (uint8_t)((x)&0xff)
#define hi8(x) (uint8_t)(((x) >> 8) & 0xff)
#define CS_PIN 5
#define IRQ_PIN 4  // INT is only needed for receiving, can stay unconnected
#define BT_NAME "CAN_Cluster"
#define ECHO_INPUT true  // send every received line back (visible in SimHub log)
#define DEBUG_BT_TO_USB true  // print every line received over Bluetooth on USB Serial
#define DEBUG_CAN_RX true  // print frames sent by the cluster on USB Serial (e.g. check control 0x338)
#define BLINKER_INTERVAL 650
#define CAN_RX_PRINT_INTERVAL 2000  // the same CAN ID is printed at most once per this time
#define CAN_RX_WATCH_ID 0x338  // this ID is also printed on every data change (check control messages)
#define CAN_INJECT_INTERVAL 100  // interval between repeats of a frame sent with "can..." command
#define LINE_BUFFER_SIZE 128
#define BC_ENTER_PIN 25  // MOSFET switching 1k between cluster pin 16 and GND (BC button)
#define BC_UP_PIN 26  // 2k (up)
#define BC_DOWN_PIN 27  // 3k (down)
#define BC_PRESS_TIME 200  // "bc<button>" command
#define BC_HOLD_TIME 2000  // "bcHold<button>" command (long press)
#define CC_SLOTS 16  // max active check control messages at a time
#define CC_REFRESH_INTERVAL 1000  // active messages are repeated, otherwise the cluster drops them
#define SUPPRESS_SERVICE_LIGHT true  // hide "!" service reminder (CC-ID 281) shown after every ignition on
#define SERVICE_LIGHT_ID 281

BluetoothSerial SerialBT;

// timers
uint32_t timestamp100ms = 0;
uint32_t timestamp200ms = 0;
uint32_t timeToBlinkers = 0;
uint32_t timeToInjectedFrame = 0;
uint32_t timestamp500ms = 0;
uint32_t timeToCheckControl = 0;

// Serial
struct LineReader {
  char buffer[LINE_BUFFER_SIZE];
  size_t length;
  bool overflow;
  uint32_t bytesReceived;
};
LineReader btReader = {};
LineReader usbReader = {};
bool btConnected = false;

// values
int speed = 0, handBrake = 0, rpm = 0, fuelLevel = 100;
short blinkers = 0;
bool firstBlink = true;
uint8_t hour = 0, minute = 0;
bool lightMode[5] = { false, true, false, false, false };
bool cruiseActive = false;
int cruiseSpeed = 0;  // km/h
uint32_t cruiseRefreshInterval = 1000;
int fuelRate = 0;  // µl/s, drives the consumption gauge and on-board computer
const uint8_t bcPins[3] = { BC_ENTER_PIN, BC_UP_PIN, BC_DOWN_PIN };
int8_t bcActivePin = -1;
uint32_t bcPressStart = 0;
uint32_t bcPressLength = 0;

// CAN variables
uint8_t abs_frame = 0xB3;
uint8_t tempCounter = 0x63;
struct InjectedFrame {
  long id;
  uint8_t data[8];
  uint8_t length;
  uint16_t remaining;
};
InjectedFrame injectedFrame = {};
uint16_t ccActive[CC_SLOTS];
uint8_t ccCount = 0;

void setup() {
  Serial.begin(115200);
  SerialBT.begin(BT_NAME);
  for (uint8_t pin : bcPins) {
    digitalWrite(pin, LOW);
    pinMode(pin, OUTPUT);
  }
  CAN.setPins(CS_PIN, IRQ_PIN);
  CAN.setSPIFrequency(1E6);
  while (!CAN.begin(100E3)) {
    Serial.println("Connecting with CAN failed!");
    delay(100);
  }
  Serial.println("Connecting with CAN succesful!");
  Serial.println("Bluetooth name: " BT_NAME);
  timestamp100ms = millis();
  timestamp200ms = millis();
  timeToBlinkers = millis();
}

void loop() {
  if (readLine(SerialBT, btReader)) {
    processLine(btReader.buffer);
    if (ECHO_INPUT) SerialBT.println(btReader.buffer);
    if (DEBUG_BT_TO_USB) {
      Serial.print("BT> ");
      Serial.println(btReader.buffer);
    }
  }
  if (readLine(Serial, usbReader)) {
    processLine(usbReader.buffer);
    if (ECHO_INPUT) Serial.println(usbReader.buffer);
  }
  checkBluetoothConnection();
  updateBcButton();
  if (DEBUG_BT_TO_USB)
    reportBluetoothTraffic();
  if (DEBUG_CAN_RX)
    reportCanFrames();

  if (millis() - timestamp100ms > 99) {
    sendIgnitionKeyOn();
    sendSpeed(speed, true);
    sendRPM(rpm);
    sendEngineData();
    sendDmeStatus();
    sendVehicleDynamics(speed);
    timestamp100ms = millis();
  }
  if (millis() - timestamp200ms > 199) {
    sendLights();
    sendIgnitionStatus();
    sendAirbagSeatbeltCounter();
    sendABSBrakeCounter1();
    sendABSBrakeCounter2();
    sendHandbrake(handBrake);
    sendTime();
    sendFuelLevel(fuelLevel);
    sendCruiseControl();
    timestamp200ms = millis();
  }
  // blinker frame is sent at a fixed interval also when blinkers are off (previously it flooded the bus)
  if (millis() - timeToBlinkers >= BLINKER_INTERVAL) {
    switch (blinkers) {
      case 0:
        sendTurnOffBlinkers();
        break;
      case 1:
        sendLeftBlinker();
        break;
      case 2:
        sendRightBlinker();
        break;
      case 3:
        sendHazzard();
        break;
    }
    timeToBlinkers = millis();
  }
  if (millis() - timestamp500ms > 499) {
    if (SUPPRESS_SERVICE_LIGHT && !isCheckControlActive(SERVICE_LIGHT_ID))
      sendCheckControl(SERVICE_LIGHT_ID, false);
    timestamp500ms = millis();
  }
  if (ccCount > 0 && millis() - timeToCheckControl >= CC_REFRESH_INTERVAL) {
    for (uint8_t i = 0; i < ccCount; i++)
      sendCheckControl(ccActive[i], true);
    timeToCheckControl = millis();
  }
  if (injectedFrame.remaining > 0 && millis() - timeToInjectedFrame >= CAN_INJECT_INTERVAL) {
    sendInjectedFrame();
    timeToInjectedFrame = millis();
  }
}

// functions
// CAN functions
void sendHazzard() {
  CAN.beginPacket(0x1F6);
  CAN.write(0xB1);
  if (firstBlink) {
    CAN.write(0xF2);
    firstBlink = false;
  }
  else
    CAN.write(0xF1);
  CAN.endPacket();
}

void sendTurnOffBlinkers() {
  CAN.beginPacket(0x1F6);
  CAN.write(0x80);
  if (firstBlink) {
    CAN.write(0xF0);
    firstBlink = false;
  }
  CAN.endPacket();
}

void sendLeftBlinker() {
  CAN.beginPacket(0x1F6);
  CAN.write(0x91);
  if (firstBlink) {
    CAN.write(0xF2);
    firstBlink = false;
  }
  else
    CAN.write(0xF1);
  CAN.endPacket();
}

void sendRightBlinker() {
  CAN.beginPacket(0x1F6);
  CAN.write(0xA1);
  if (firstBlink) {
    CAN.write(0xF2);
    firstBlink = false;
  }
  else
    CAN.write(0xF1);
  CAN.endPacket();
}

void sendHandbrake(int isActive) {
  CAN.beginPacket(0x34F);
  isActive ? CAN.write(0xFE) : CAN.write(0xFD);
  CAN.write(0xFF);
  CAN.endPacket();
}

void sendSpeed(uint16_t speed, bool kmph) {
  static uint32_t lastTimeSent = 0;
  static uint16_t lastReading = 0;
  static uint16_t count = 0xF000;
  uint16_t speedValToSend;
  if (kmph) {
    speedValToSend = ((millis() - lastTimeSent) / 50) * speed / 2 * 0.65;
  } else
    speedValToSend = ((millis() - lastTimeSent) / 50) * speed / 2 * 1.055;
  speedValToSend += lastReading;

  lastReading = speedValToSend;
  lastTimeSent = millis();

  CAN.beginPacket(0x1A6);

  CAN.write(lo8(speedValToSend));
  CAN.write(hi8(speedValToSend));
  CAN.write(lo8(speedValToSend));
  CAN.write(hi8(speedValToSend));
  CAN.write(lo8(speedValToSend));
  CAN.write(hi8(speedValToSend));
  CAN.write(lo8(count));
  CAN.write(hi8(count));

  CAN.endPacket();
  count += 200;
}

void sendRPM(uint16_t rpm) {
  uint16_t tempRpm = rpm * 4.15;
  CAN.beginPacket(0x0AA);

  CAN.write(0x5F);
  CAN.write(0x59);
  CAN.write(0xFF);
  CAN.write(0x00);
  CAN.write(lo8(tempRpm));
  CAN.write(hi8(tempRpm));
  CAN.write(0x80);
  CAN.write(0x99);

  CAN.endPacket();
}
void sendFuelLevel(int percent) {
  percent = map(constrain(percent, 0, 100), 0, 100, 0, 50);
  uint16_t sensor = percent * 160;

  CAN.beginPacket(0x349);

  CAN.write(lo8(sensor));
  CAN.write(hi8(sensor));
  CAN.write(lo8(sensor));
  CAN.write(hi8(sensor));
  CAN.write(0x00);

  CAN.endPacket();
}
void sendTime() {
  CAN.beginPacket(0x39E);
  CAN.write(hour);
  CAN.write(minute);
  CAN.write(0x00);
  CAN.write(0x18);
  CAN.write(0x7F);
  CAN.write(0xE7);
  CAN.write(0x07);
  CAN.write(0xF2);
  CAN.endPacket();
}

void sendLights() {
  CAN.beginPacket(0x21A);
  uint8_t lights = 0;
  if (lightMode[0])
    lights = 0;
  if (lightMode[1])
    lights += 5;
  if (lightMode[2])
    lights += 2;
  if (lightMode[3])
    lights += 32;
  if (lightMode[4])
    lights += 64;
  CAN.write(lights);
  // 5-low 7-high 25-fog front 45-fog back
  CAN.write(0x12);
  CAN.write(0xF7);

  CAN.endPacket();
}

void sendIgnitionKeyOn() {
  CAN.beginPacket(0x130);
  CAN.write(0x45);
  CAN.write(0x40);
  CAN.write(0x69);
  CAN.write(0x8F);
  CAN.write(0xE2);
  CAN.endPacket();
}
void sendIgnitionStatus() {
  CAN.beginPacket(0x26E);

  CAN.write(0x40);
  CAN.write(0x40);
  CAN.write(0x7F);
  CAN.write(0x50);
  CAN.write(0xFF);
  CAN.write(0xFF);
  CAN.write(0xFF);
  CAN.write(0xFF);

  CAN.endPacket();
}
void sendAirbagSeatbeltCounter() {
  static uint8_t count = 0x00;

  CAN.beginPacket(0x0D7);

  CAN.write(count);
  CAN.write(0xFF);

  CAN.endPacket();

  count++;
}

void sendABSBrakeCounter2() {
  abs_frame = ((((abs_frame >> 4) + 3) << 4) & 0xF0) | 0x03;
  CAN.beginPacket(0x19E);

  CAN.write(0x00);
  CAN.write(0x00);
  CAN.write(abs_frame);
  CAN.write(0x00);
  CAN.write(0x00);
  CAN.write(0x00);
  CAN.write(0x00);
  CAN.write(0x00);

  CAN.endPacket();
}

void sendABSBrakeCounter1() {
  static uint8_t count = 0xF0;

  CAN.beginPacket(0x0C0);

  CAN.write(count);
  CAN.write(0xFF);

  CAN.endPacket();

  count++;
  if (count == 0x00) {
    count = 0xF0;
  }
}

// Engine data from DME (format from veikkos/e90-can-cluster): byte 2 alive counter + run state in bits 4-5
// (2 = running), bytes 4-5 total injected fuel in µl. The cluster computes instant consumption (mpg gauge),
// average consumption and range from it.
void sendEngineData() {
  static uint32_t lastTime = 0;
  static uint32_t fuelRemainder = 0;  // µl * 1000
  static uint16_t fuelTotal = 0;
  uint32_t now = millis();
  fuelRemainder += (uint32_t)constrain(fuelRate, 0, 100000) * (now - lastTime);
  fuelTotal += fuelRemainder / 1000;
  fuelRemainder %= 1000;
  lastTime = now;

  CAN.beginPacket(0x1D0);
  CAN.write(0x8B);  // coolant temperature + 48 (91 °C)
  CAN.write(0x8A);  // oil temperature + 48 (90 °C)
  CAN.write(0x20 | (tempCounter & 0x0F));
  CAN.write(0xCD);
  CAN.write(lo8(fuelTotal));
  CAN.write(hi8(fuelTotal));
  CAN.write(0xCD);
  CAN.write(0xA8);

  CAN.endPacket();
  tempCounter++;
}

// DME status (fixed frame from veikkos/e90-can-cluster).
void sendDmeStatus() {
  CAN.beginPacket(0x12F);
  CAN.write(0x3F);
  for (int i = 0; i < 4; i++)
    CAN.write(0x00);
  CAN.write(0x40);
  CAN.write(0x01);
  CAN.write(0x00);
  CAN.endPacket();
}

// Vehicle speed from DSC in 0.1 km/h (format from veikkos/e90-can-cluster), accelerations and yaw rate 0.
// Byte 1 bits 4-6: driving direction forward, byte 6 high nibble alive counter, byte 7 checksum.
void sendVehicleDynamics(uint16_t kmph) {
  uint16_t raw = constrain(kmph * 10, 0, 0xFFF);
  uint8_t frame[8] = {};
  frame[0] = lo8(raw);
  frame[1] = 0x80 | (kmph >= 1 ? 0x10 : 0x00) | ((raw >> 8) & 0x0F);
  frame[6] = (((millis() % 300) * 16 / 300) & 0x0F) << 4;
  uint16_t sum = 0x00A0;
  for (int i = 0; i < 7; i++)
    sum += frame[i];
  frame[7] = sum + (sum >> 8) + 0x01;

  CAN.beginPacket(0x1A0);
  CAN.write(frame, 8);
  CAN.endPacket();
}

// Cruise control status from DME (flags from commaai/opendbc bmw_e9x_e8x.dbc, speed scale tested on the cluster).
// Without this frame the cluster shows CC-ID 26 "cruise control failure" after power-up.
// byte 0 + low nibble of byte 1: set speed in 1/16 mph, byte 1 bit 4 inactive / bit 5 active.
// km/h are sent 1:1 as mph, so the cluster shows the same number as the game (only the unit label differs).
// The cluster shows the set speed only for a moment after it changes, so while cruise is active
// the lowest 1/16 bit is toggled every cruiseRefreshInterval ms - the displayed number stays the same,
// but the message is shown again. 0 disables it ("cruiseRefresh<ms>" over serial).
void sendCruiseControl() {
  static uint32_t lastToggle = 0;
  static uint8_t fraction = 0;
  if (cruiseActive && cruiseRefreshInterval > 0 && millis() - lastToggle >= cruiseRefreshInterval) {
    fraction ^= 1;
    lastToggle = millis();
  }
  uint16_t setSpeed = cruiseActive ? constrain(cruiseSpeed, 0, 255) * 16 + fraction : 0;

  CAN.beginPacket(0x200);
  CAN.write(lo8(setSpeed));
  CAN.write((cruiseActive ? 0x20 : 0x10) | (hi8(setSpeed) & 0x0F));
  for (int i = 0; i < 6; i++)
    CAN.write(0x00);
  CAN.endPacket();
}

// Check control message / warning light. Cluster confirms a shown message in 0x338.
// IDs e.g. from veikkos/e90-can-cluster types.h: 31 check engine, 39 overheat, 212 oil, 213 battery,
// 275 fuel, 281 service, 308 brake (red), 643 AdBlue.
void sendCheckControl(uint16_t id, bool on) {
  CAN.beginPacket(0x592);
  CAN.write(0x40);
  CAN.write(lo8(id));
  CAN.write(hi8(id));
  CAN.write(on ? 0x31 : 0x30);
  for (int i = 0; i < 4; i++)
    CAN.write(0xFF);
  CAN.endPacket();
}

void sendInjectedFrame() {
  CAN.beginPacket(injectedFrame.id);
  CAN.write(injectedFrame.data, injectedFrame.length);
  CAN.endPacket();
  injectedFrame.remaining--;
}

// Prints frames sent by the cluster itself (e.g. check control / error messages).
// Each ID is printed when first seen and then at most once per CAN_RX_PRINT_INTERVAL,
// CAN_RX_WATCH_ID additionally on every data change.
void reportCanFrames() {
  struct SeenId {
    long id;
    uint32_t lastPrint;
    uint8_t data[8];
    uint8_t length;
  };
  static SeenId seen[40];
  static uint8_t seenCount = 0;

  for (int i = 0; i < 4; i++) {
    int size = CAN.parsePacket();
    if (size < 0 || (size == 0 && !CAN.packetRtr()))
      return;
    long id = CAN.packetId();
    uint8_t data[8];
    int length = 0;
    while (CAN.available() && length < 8)
      data[length++] = CAN.read();

    SeenId* entry = NULL;
    for (uint8_t n = 0; n < seenCount; n++)
      if (seen[n].id == id)
        entry = &seen[n];
    if (entry == NULL && seenCount < 40) {
      entry = &seen[seenCount++];
      entry->id = id;
      entry->lastPrint = millis() - CAN_RX_PRINT_INTERVAL;
      entry->length = 0xFF;  // never equal to received data
    }
    if (entry != NULL) {
      bool changed = entry->length != length || memcmp(entry->data, data, length) != 0;
      if (millis() - entry->lastPrint < CAN_RX_PRINT_INTERVAL && !(id == CAN_RX_WATCH_ID && changed))
        continue;
      entry->lastPrint = millis();
      memcpy(entry->data, data, length);
      entry->length = length;
    }

    Serial.printf("CAN< %03lX [%d]", id, length);
    for (int n = 0; n < length; n++)
      Serial.printf(" %02X", data[n]);
    Serial.println();
  }
}

// Serial functions
// Collects characters into a fixed buffer (no String, no heap fragmentation).
// Returns true when a complete line is in reader.buffer.
bool readLine(Stream& input, LineReader& reader) {
  while (input.available()) {
    char inChar = (char)input.read();
    reader.bytesReceived++;
    if (inChar == '\r')
      continue;
    if (inChar == '\n') {
      bool complete = !reader.overflow && reader.length > 0;
      reader.buffer[reader.length] = '\0';
      reader.length = 0;
      reader.overflow = false;
      if (complete)
        return true;
    } else if (reader.length < LINE_BUFFER_SIZE - 1) {
      reader.buffer[reader.length++] = inChar;
    } else
      reader.overflow = true;  // drop the rest of a too long line
  }
  return false;
}

void processLine(const char* line) {
  if (catchRawFrame(line))
    return;  // test command, hex data could look like other values (e.g. "bc1")
  int value;
  if (catchValue(line, "speed", value))
    speed = value;
  if (catchValue(line, "rpm", value))
    rpm = value;
  if (catchValue(line, "handBrake", value))
    handBrake = value;
  if (catchValue(line, "blinkers", value) && value != blinkers) {
    blinkers = value;
    firstBlink = true;
    timeToBlinkers = millis() - BLINKER_INTERVAL;  // send new state immediately
  }
  if (catchValue(line, "fuel", value))
    fuelLevel = value;
  if (catchValue(line, "fuelRate", value))
    fuelRate = value;
  if (catchValue(line, "cruise", value))
    cruiseActive = value;
  if (catchValue(line, "cruiseSpeed", value))
    cruiseSpeed = value;
  if (catchValue(line, "cruiseRefresh", value))
    cruiseRefreshInterval = value;
  if (catchValue(line, "bc", value))
    pressBcButton(value, BC_PRESS_TIME);
  if (catchValue(line, "bcHold", value))
    pressBcButton(value, BC_HOLD_TIME);
  catchCheckControl(line);
  if (catchValue(line, "lights", value)) {
    switch (value) {
      case 0:
        lightMode[0] = true;
        lightMode[1] = false;
        lightMode[3] = false;
        break;
      case 1:
        lightMode[1] = true;
        lightMode[0] = false;
        lightMode[3] = false;
        break;
      case 2:
        lightMode[3] = true;
        lightMode[1] = true;
        break;
      case 3:
        lightMode[2] = true;
        break;
      case 4:
        lightMode[2] = false;
        break;
      case 5:
        lightMode[4] = true;
        break;
      case 6:
        lightMode[4] = false;
        break;
    }
  }
  catchTime(line);
}

// Finds "<valName><number>" in line, e.g. "rpm2300". Name without a number is ignored.
// SimHub can glue several updates of one value into a line ("speed100speed0"), so the last one wins.
bool catchValue(const char* line, const char* valName, int& value) {
  bool found = false;
  size_t nameLength = strlen(valName);
  for (const char* where = strstr(line, valName); where != NULL; where = strstr(where + nameLength, valName)) {
    if (isdigit((unsigned char)where[nameLength])) {
      value = atoi(where + nameLength);
      found = true;
    }
  }
  return found;
}

// Accepts "time15:42:07" as well as "time03:42:07 PM" / "time3:42:07 PM".
void catchTime(const char* line) {
  const char* where = strstr(line, "time");
  if (where == NULL)
    return;
  where += 4;
  int inputHour, inputMinute;
  if (sscanf(where, "%d:%d", &inputHour, &inputMinute) != 2)
    return;
  const char* pm = strstr(where, "PM");
  const char* am = strstr(where, "AM");
  if (pm != NULL && pm - where <= 12 && inputHour < 12)
    inputHour += 12;  // 1 PM -> 13, 12 PM stays 12
  else if (am != NULL && am - where <= 12 && inputHour == 12)
    inputHour = 0;  // 12 AM -> 0
  if (inputHour < 0 || inputHour > 23 || inputMinute < 0 || inputMinute > 59)
    return;
  hour = inputHour;
  minute = inputMinute;
}

// Test command for finding unknown frames: "can<ID>:<data>[x<count>]", ID and data in hex, count in decimal.
// E.g. "can1EE:40FF" sends the frame once, "can1EE:40FFx20" 20 times every CAN_INJECT_INTERVAL ms
// (some modules need a frame held for a while), "x0" stops the frame being repeated.
// Returns true when the line was a "can" command (also a wrong one).
bool catchRawFrame(const char* line) {
  const char* where = strstr(line, "can");
  if (where == NULL || !isxdigit((unsigned char)where[3]))
    return false;
  char* end;
  long id = strtol(where + 3, &end, 16);
  InjectedFrame frame = {};
  frame.id = id;
  frame.remaining = 1;
  const char* p = end + 1;
  while (*end == ':' && isxdigit((unsigned char)p[0]) && isxdigit((unsigned char)p[1]) && frame.length < 8) {
    char byteText[3] = { p[0], p[1], '\0' };
    frame.data[frame.length++] = strtol(byteText, NULL, 16);
    p += 2;
  }
  if (*end == ':' && *p == 'x' && isdigit((unsigned char)p[1])) {
    frame.remaining = constrain(atoi(p + 1), 0, 1000);
    p++;
    while (isdigit((unsigned char)*p))
      p++;
  }
  if (*end != ':' || id > 0x7FF || (*p != '\0' && !isspace((unsigned char)*p))) {
    Serial.println("CAN> wrong command, use can<ID>:<data>[x<count>], e.g. can1EE:40FFx20");
    return true;
  }

  Serial.printf("CAN> %03lX [%d]", frame.id, frame.length);
  for (int n = 0; n < frame.length; n++)
    Serial.printf(" %02X", frame.data[n]);
  Serial.printf(" x%u\n", frame.remaining);
  injectedFrame = frame;
  timeToInjectedFrame = millis() - CAN_INJECT_INTERVAL;  // send immediately
  return true;
}

// Buttons on the turn signal stalk (BC, up, down) are not on CAN - they connect cluster pin 16
// to GND through 1k, 2k or 3k. Each resistor is switched by a MOSFET driven from a GPIO.
// button: 1 BC, 2 up, 3 down (same as the resistor value in kOhm). Only one can be pressed at a time.
void pressBcButton(int button, uint32_t length) {
  releaseBcButton();
  if (button < 1 || button > 3) {
    Serial.println("BC> wrong button, use 1 (BC), 2 (up) or 3 (down)");
    return;
  }
  bcActivePin = bcPins[button - 1];
  digitalWrite(bcActivePin, HIGH);
  bcPressStart = millis();
  bcPressLength = length;
  Serial.printf("BC> button %d for %lu ms\n", button, (unsigned long)length);
}

void releaseBcButton() {
  if (bcActivePin < 0)
    return;
  digitalWrite(bcActivePin, LOW);
  bcActivePin = -1;
}

void updateBcButton() {
  if (bcActivePin >= 0 && millis() - bcPressStart >= bcPressLength)
    releaseBcButton();
}

// Handles every "ccOn<ID>" / "ccOff<ID>" in line (SimHub can glue several messages into one line).
void catchCheckControl(const char* line) {
  for (const char* where = strstr(line, "cc"); where != NULL; where = strstr(where + 2, "cc")) {
    if (strncmp(where, "ccOn", 4) == 0 && isdigit((unsigned char)where[4]))
      setCheckControl(atoi(where + 4), true);
    else if (strncmp(where, "ccOff", 5) == 0 && isdigit((unsigned char)where[5]))
      setCheckControl(atoi(where + 5), false);
  }
}

bool isCheckControlActive(uint16_t id) {
  for (uint8_t i = 0; i < ccCount; i++)
    if (ccActive[i] == id)
      return true;
  return false;
}

// Frames are sent only on change (SimHub repeats the state), active ones are refreshed in loop().
void setCheckControl(int id, bool on) {
  if (id <= 0 || id > 0xFFFF)
    return;
  for (uint8_t i = 0; i < ccCount; i++) {
    if (ccActive[i] != id)
      continue;
    if (!on) {
      ccActive[i] = ccActive[--ccCount];
      sendCheckControl(id, false);
    }
    return;
  }
  if (!on)
    return;
  if (ccCount >= CC_SLOTS) {
    Serial.println("CC> too many active messages");
    return;
  }
  ccActive[ccCount++] = id;
  sendCheckControl(id, true);
}

void clearCheckControl() {
  while (ccCount > 0)
    sendCheckControl(ccActive[--ccCount], false);
}

// Drops needles to zero when SimHub disconnects, otherwise the last values would stay on the cluster.
void checkBluetoothConnection() {
  bool connected = SerialBT.hasClient();
  if (connected != btConnected) {
    btConnected = connected;
    Serial.println(connected ? "Bluetooth connected" : "Bluetooth disconnected");
    if (!connected) {
      speed = 0;
      rpm = 0;
      fuelRate = 0;
      cruiseActive = false;
      clearCheckControl();
      if (blinkers != 0) {
        blinkers = 0;
        firstBlink = true;
      }
    }
  }
}

// Once per second prints how many bytes came over Bluetooth and an unfinished line, if any
// (helps to tell "nothing arrives" from "data arrives without a newline").
void reportBluetoothTraffic() {
  static uint32_t lastReport = 0;
  if (millis() - lastReport < 1000)
    return;
  lastReport = millis();
  if (btReader.bytesReceived == 0)
    return;
  Serial.printf("BT bytes in last second: %lu\n", (unsigned long)btReader.bytesReceived);
  if (btReader.length > 0) {
    Serial.print("BT waiting for newline: ");
    Serial.write((const uint8_t*)btReader.buffer, btReader.length);
    Serial.println();
  }
  btReader.bytesReceived = 0;
}
