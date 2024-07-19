#include <CAN.h>

#define lo8(x) (uint8_t)((x) & 0xff)
#define hi8(x) (uint8_t)(((x) >> 8) & 0xff)
#define CS_PIN 10
#define IRQ_PIN 2

// timers
uint32_t timestamp100ms = 0;
uint32_t timestamp200ms = 0;
uint32_t timetoblinkers = 0;

// Serial
bool stringComplete = false;
String inputString = "";

// values
int speed = 0, handBrake = 0, rpm = 0,fuelLevel=100;
uint8_t hour = 0, minute = 0;
bool lightMode[5] = {false, true, false, false, false};

// CAN variables
uint8_t abs_frame = 0xB3;
uint8_t tempCounter = 0x63;

void setup()
{
    Serial.begin(115200);
    inputString.reserve(200);
    CAN.setPins(CS_PIN, IRQ_PIN);
    CAN.setSPIFrequency(1E6);
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

void loop()
{
    if (stringComplete)
    {
        if (catchValue("speed") >= 0)
            speed = catchValue("speed");
        if (catchValue("rpm") >= 0)
            rpm = catchValue("rpm");
        if (catchValue("hour") >= 0)
            hour = catchValue("hour");
        if (catchValue("minute") >= 0)
            minute = catchValue("minute");
        if (catchValue("handBrake") >= 0)
            handBrake = catchValue("handBrake");
        if (catchValue("fuel") >= 0)
            fuelLevel = catchValue("fuel");
        if (catchValue("lights") >= 0)
        {
            switch (inputString[6])
            {
            case '0':
                lightMode[0] = true;
                lightMode[1] = false;
                lightMode[3] = false;
                break;
            case '1':
                lightMode[1] = true;
                lightMode[0] = false;
                lightMode[3] = false;
                break;
            case '2':
                lightMode[3] = true;
                lightMode[1] = true;
                break;
            case '3':
                lightMode[2] = true;
                break;
            case '4':
                lightMode[2] = false;
                break;
            case '5':
                lightMode[4] = true;
                break;
            case '6':
                lightMode[4] = false;
                break;
            }
        }
        inputString = "";
        stringComplete = false;
    }
    if (millis() - timestamp100ms > 99)
    {
        sendIgnitionKeyOn();
        sendSpeed(speed, true);
        sendRPM(rpm);
        timestamp100ms = millis();
    }
    if (millis() - timestamp200ms > 199)
    {
        sendLights();
        sendIgnitionStatus();
        sendAirbagSeatbeltCounter();
        sendABSBrakeCounter1();
        sendABSBrakeCounter2();
        sendHandbrake(handBrake);
        sendEngineTemp();
        sendTime();
        sendFuelLevel(fuelLevel);
        timestamp200ms = millis();
    }
}

// functions
// CAN functions
void sendHandbrake(int isActive)
{
    CAN.beginPacket(0x34F);
    isActive ? CAN.write(0xFE) : CAN.write(0xFD);
    CAN.write(0xFF);
    CAN.endPacket();
}

void sendSpeed(uint16_t speed, bool kmph)
{
    static uint32_t lastTimeSent = 0;
    static uint16_t lastReading = 0;
    static uint16_t count = 0xF000;
    uint16_t speedValToSend;
    if (kmph)
    {
        speedValToSend = ((millis() - lastTimeSent) / 50) * speed / 2 * 0.65;
    }
    else
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

void sendRPM(uint16_t rpm)
{
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
void sendFuelLevel(int percent)
{
  percent = map(percent, 0, 100, 0, 48);
  uint16_t sensor = percent * 160;

  CAN.beginPacket(0x349);

  CAN.write(lo8(sensor));
  CAN.write(hi8(sensor));
  CAN.write(lo8(sensor));
  CAN.write(hi8(sensor));
  CAN.write(0x00);

  CAN.endPacket();
}
void sendTime()
{
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

void sendLights()
{
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

void sendIgnitionKeyOn()
{
    CAN.beginPacket(0x130);
    CAN.write(0x45);
    CAN.write(0x40);
    CAN.write(0x69);
    CAN.write(0x8F);
    CAN.write(0xE2);
    CAN.endPacket();
}
void sendIgnitionStatus()
{
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
void sendAirbagSeatbeltCounter()
{
    static uint8_t count = 0x00;

    CAN.beginPacket(0x0D7);

    CAN.write(count);
    CAN.write(0xFF);

    CAN.endPacket();

    count++;
}

void sendABSBrakeCounter2()
{
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

void sendABSBrakeCounter1()
{
    static uint8_t count = 0xF0;

    CAN.beginPacket(0x0C0);

    CAN.write(count);
    CAN.write(0xFF);

    CAN.endPacket();

    count++;
    if (count == 0x00)
    {
        count = 0xF0;
    }
}

void sendEngineTemp()
{
    CAN.beginPacket(0x1D0);
    CAN.write(0x8B);
    CAN.write(0xFF);
    CAN.write(tempCounter);
    CAN.write(0xCC);
    CAN.write(0x5D);
    CAN.write(0x37);
    CAN.write(0xCD);
    CAN.write(0xFF);

    CAN.endPacket();
    tempCounter++;
}
// Serial functions
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