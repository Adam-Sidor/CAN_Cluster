#include <CAN.h>

#define lo8(x) (uint8_t)((x) & 0xff)
#define hi8(x) (uint8_t)(((x) >> 8) & 0xff)
#define CS_PIN 10
#define IRQ_PIN 2

uint32_t timestamp100ms = 0;
uint32_t timestamp200ms = 0;
uint32_t timetoblinkers = 0;

bool stringComplete = false;
String inputString = "",inputValue="0";

int speed = 0;

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
        if(inputString.indexOf("SPEED")!=-1){
            inputValue=inputString.substring(5,inputString.length());
        }
        speed=inputValue.toInt();
        Serial.println(speed);
        Serial.println(inputString);
        inputString = "";
        stringComplete = false;
    }
}

// functions

void serialEvent()
{
    while (Serial.available())
    {
        char inChar = (char)Serial.read();
        if (inChar == '\n')
        {
            stringComplete = true;
        }else
            inputString += inChar;
    }
}