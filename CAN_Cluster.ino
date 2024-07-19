#include <CAN.h>

#define lo8(x) (uint8_t)((x) & 0xff)
#define hi8(x) (uint8_t)(((x) >> 8) & 0xff)
#define CS_PIN 10
#define IRQ_PIN 2

uint32_t timestamp100ms = 0;
uint32_t timestamp200ms = 0;
uint32_t timetoblinkers = 0;

bool stringComplete = false;
String inputString = "";

int speed = 0,handBrake=0;

void setup()
{
    Serial.begin(115200);
    inputString.reserve(200);
    CAN.setPins(CS_PIN, IRQ_PIN);
    CAN.setSPIFrequency(1E6);
    /*while (!CAN.begin(100E3))
    {
      Serial.println("Connecting with CAN failed!");
      delay(100);
    }
    Serial.println("Connecting with CAN succesful!");*/
    timestamp100ms = millis();
    timestamp200ms = millis();
    timetoblinkers = millis();
}

void loop()
{
    if (stringComplete)
    {
        if(catchValue("speed")>=0)
            speed=catchValue("speed");
        if(catchValue("handBrake")>=0){
            handBrake = catchValue("handBrake");
        }
        Serial.println(speed);
        Serial.println(handBrake);
        inputString = "";
        stringComplete = false;
    }
}

//functions
//CAN functions
void sendHandbrake(bool isActive)
{
  CAN.beginPacket(0x34F);
  isActive ? CAN.write(0xFE):CAN.write(0xFD);
  CAN.write(0xFF);
  CAN.endPacket();
}
//Serial functions
int catchValue(String valName)
{
    String inputValue = "-1";
    if (inputString.indexOf(valName) != -1)
    {
        inputValue = inputString.substring(valName.length(), inputString.length());
    }
    return inputValue.toInt();
}
void serialEvent()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        if (inChar == '\n')
        {
            stringComplete = true;
        }
        else
            inputString += inChar;
    }
}