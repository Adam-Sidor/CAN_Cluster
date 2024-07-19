#include <CAN.h>
#define lo8(x) (uint8_t)((x)&0xff)
#define hi8(x) (uint8_t)(((x) >> 8) & 0xff)
#define CS_PIN  10
#define IRQ_PIN 2
uint32_t timestamp100ms = 0;
uint32_t timestamp200ms = 0;
uint32_t timetoblinkers = 0;
String inputString = "";

void setup()
{
  Serial.begin(115200);
  inputString.reserve(200);
  CAN.setPins(CS_PIN, IRQ_PIN);
  while (!CAN.begin(100E3))
  {
    Serial.println("Connecting with CAN failed!");
    delay(100);
  }
  Serial.println("Connecting with CAN succesful!");
  timestamp100ms = millis();
  timestamp200ms = millis();
  timetoblinkers = millis();
}

void loop(){

}