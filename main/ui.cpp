#include <Arduino.h>
#include <SPI.h>
#include <U8g2lib.h>
#include "include/ui.h"
#include "include/pins.h"
#include "include/sensors.h"
#include "include/eeprom.h"
#include "include/core.h"
#include "include/config.h"
#include <SoftTimer.h>

/*#ifdef MEGA
//U8GLIB_SSD1306_128X64 u8g(13, 11, 7, 6, 8); 
U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, 13, 11, 10, 6, 5);
#endif*/

//#ifdef TEENSY
//U8GLIB_SSD1306_128X64 u8g(9, 11, 10, 6, 5); // DSPLOUT1-5
// 9->13 would allow hardware SPI
U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI u8g2(U8G2_R0, 13, 11, 10, 9, 5);
//U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, 13, 11, 10, 9, 5);
//U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI u8g2(U8G2_R0, 9, 11, 10, 13, 5);
//a_SSD1306_128X64_NONAME_1_4W_HW_SPI(U8G2_R0, 10, 6, 5);
//#endif
/*
yellow(res), yellow-brown(dc), black(clock), redyellowfat (cs), brownwhitepink (data)
5 6 9 10 11
clock, data, cs, dc [, reset]
9       11    10  6    5
res,dc,clock,cs,data


whitegreen<->black
green<->brownwhitepink
orangewhite<->redyellow
brown<->yellowbrown
blue<->yellow
*/

// UI STAGE
// Control for what user sees and how gearbox is used with
//
typedef u8g2_uint_t u8g_uint_t;
//u8g2.begin();

void draw(int wantedGear)
{
  struct SensorVals sensor = readSensors();
  struct ConfigParam config = readConfig();
  static int maxSpeed, maxBoost, maxOilTemp, maxAtfTemp, maxRPM;
  static int infoDisplay = 1;
  static double infoDisplayTime;
  static boolean infoDisplayShown, infoBoost, infoSpeed = false;

  if (sensor.curOilTemp > maxOilTemp)
  {
    maxOilTemp = sensor.curOilTemp;
  }
  if (sensor.curSpeed > maxSpeed)
  {
    maxSpeed = sensor.curSpeed;
  }
  if (sensor.curBoost > maxBoost)
  {
    maxBoost = sensor.curBoost;
  }
  if (sensor.curAtfTemp > maxAtfTemp)
  {
    maxAtfTemp = sensor.curAtfTemp;
  }
  if (sensor.curRPM > maxRPM)
  {
    maxRPM = sensor.curRPM;
  }

  if (infoDisplay > 0)
  {
    if (!infoDisplayShown)
    {
      infoDisplay = millis();
      infoDisplayShown = true;
    }

    if (infoDisplay == 1)
    {
      u8g2.setFont(u8g2_font_fub14_tf);
      u8g2.setCursor(10, 40);
      u8g2.print(F("LAMP"));
      u8g2.setCursor(10, 60);
      u8g2.print(F("DEFECTIVE"));
    }
    else if (infoDisplay == 2)
    {
      u8g2.setFont(u8g2_font_fub14_tf);
      u8g2.setCursor(10, 40);
      u8g2.print(F("boostLimit"));
      u8g2.setCursor(10, 60);
      u8g2.print(sensor.curBoostLim);
    }
    else if (infoDisplay == 3)
    {
      u8g2.setFont(u8g2_font_fub14_tf);
      u8g2.setCursor(10, 40);
      u8g2.print(F("Speed"));
      u8g2.setCursor(10, 60);
      u8g2.print(F("fault"));
    }
  }

  if (page == 1 && infoDisplay == 0)
  {
    u8g2.setFont(u8g2_font_logisoso16_tr);
    u8g2.setCursor(50, 20);
    if (wantedGear == 6)
    {
      u8g2.print(F("N"));
    }
    if (wantedGear == 7)
    {
      u8g2.print(F("R"));
    }
    if (wantedGear == 8)
    {
      u8g2.print(F("P"));
    }
    if ((wantedGear < 5 || (!fullAuto && wantedGear == 5)) && !shiftPending)
    {
      u8g2.print(gear);
    }
    else if ((wantedGear < 5 || (!fullAuto && wantedGear == 5)) && shiftPending && !preShift && !postShift)
    {
      u8g2.setCursor(40, 20);
      u8g2.print(F("SHIFT"));
    }
    else if ((wantedGear < 5 || (!fullAuto && wantedGear == 5)) && shiftPending && preShift)
    {
      u8g2.setCursor(40, 20);
      u8g2.print(F("PRESHIFT"));
    }
    else if ((wantedGear < 5 || (!fullAuto && wantedGear == 5)) && shiftPending && postShift)
    {
      u8g2.setCursor(40, 20);
      u8g2.print(F("POSTSHIFT"));
    }
    if (fullAuto && wantedGear < 6)
    {

      u8g2.setCursor(60, 20);
      u8g2.print(F("("));
      u8g2.print(gear);
      u8g2.print(F(")"));
    }
    u8g2.setFont(u8g2_font_fub14_tf);
    u8g2.setCursor(60, 40);
    u8g2.print(sensor.curSpeed);
    u8g2.setCursor(45, 60);
    u8g2.print(F("km/h"));
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.setCursor(0, 10);
    u8g2.print("atfTemp:");
    u8g2.setCursor(0, 20);
    u8g2.print(sensor.curAtfTemp);
    u8g2.setCursor(25, 20);
    u8g2.print(maxAtfTemp);
    u8g2.setCursor(0, 30);
    u8g2.print(F("oilTemp:"));
    u8g2.setCursor(0, 40);
    u8g2.print(sensor.curOilTemp);
    u8g2.setCursor(25, 40);
    u8g2.print(maxOilTemp);
    u8g2.setCursor(0, 50);
    u8g2.print(F("Boost:"));
    u8g2.setCursor(0, 60);
    u8g2.print(sensor.curBoost);
    u8g2.setCursor(25, 60);
    u8g2.print(maxBoost);
    u8g2.setCursor(100, 10);
    u8g2.print(F("RPM:"));
    u8g2.setCursor(100, 20);
    u8g2.print(sensor.curRPM);
    u8g2.setCursor(100, 30);
    u8g2.print(F("TPS:"));
    u8g2.setCursor(100, 40);
    u8g2.print(sensor.curTps);
    u8g2.setCursor(100, 50);
    u8g2.print(F("Load:"));
    u8g2.setCursor(100, 60);
    u8g2.print(sensor.curLoad);
  }
  else if (page == 2 && infoDisplay == 0)
  {
    float boostBar;
    u8g2.drawFrame(5, 8, 115, 24);
    if (sensor.curBoostLim > 0)
    {
      boostBar = sensor.curBoost / sensor.curBoostLim;
    }
    else
    {
      boostBar = sensor.curBoost / config.boostSpring;
    }
    float boostBox = boostBar * 115;
    u8g2.drawBox(5, 8, boostBox, 24);
    u8g2.setFont(u8g2_font_fub14_tf);
    u8g2.setCursor(40, 28);
    u8g2.print(n2Speed);
    u8g2.setCursor(60, 28);
    u8g2.print(F(" / "));
    u8g2.setCursor(80, 28);
    u8g2.print(n3Speed);
    if (sensor.curBoostLim < 1)
    {
      u8g2.setCursor(10, 56);
      u8g2.print(F("LOW TEMP"));
    }
  }
  else if (page == 3 && infoDisplay == 0)
  {
    u8g2.setFont(u8g2_font_logisoso16_tr);
    u8g2.setCursor(50, 20);
    if (wantedGear == 6)
    {
      u8g2.print(F("N"));
    }
    if (wantedGear == 7)
    {
      u8g2.print(F("R"));
    }
    if (wantedGear == 8)
    {
      u8g2.print(F("P"));
    }
    if ((wantedGear < 5 || (!fullAuto && wantedGear == 5)) && !shiftPending)
    {
      u8g2.print(gear);
    }
    else if ((wantedGear < 5 || (!fullAuto && wantedGear == 5)) && shiftPending)
    {
      u8g2.setCursor(40, 20);
      u8g2.print(F("SHIFT"));
    }
    if (fullAuto && wantedGear < 6)
    {

      u8g2.setCursor(60, 20);
      u8g2.print(F("("));
      u8g2.print(gear);
      u8g2.print(F(")"));
    }
    u8g2.setFont(u8g2_font_fub14_tf);
    u8g2.setCursor(60, 40);
    u8g2.print(sensor.curSlip);
    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.setCursor(0, 10);
    u8g2.print("shiftTemp:");
    u8g2.setCursor(0, 20);
    u8g2.print(shiftAtfTemp);
    u8g2.setCursor(0, 30);
    u8g2.print(F("shiftLoad:"));
    u8g2.setCursor(0, 40);
    u8g2.print(shiftLoad);
    u8g2.setCursor(0, 50);
    u8g2.print(F("SPC:"));
    u8g2.setCursor(0, 60);
    u8g2.print(spcPercentVal);
    u8g2.setCursor(100, 10);
    u8g2.print(F("MPC:"));
    u8g2.setCursor(100, 20);
    u8g2.print(mpcPercentVal);
    u8g2.setCursor(100, 30);
    u8g2.print(F("RGear:"));
    u8g2.setCursor(100, 40);
    u8g2.print(evalGearVal);
    u8g2.setCursor(100, 50);
    u8g2.print(F("Ratio;"));
    u8g2.setCursor(100, 60);
    u8g2.print(sensor.curRatio);
  }
  if ((millis() - infoDisplayTime > 5000) && infoDisplayShown)
  {
    infoDisplay = 0;
    infoDisplayShown = false;
  }
  else if (millis() < 2000)
  {
    infoDisplay = 1;
    infoBoost = true;
  }
  else if ((sensor.curBoostLim > 0) && !infoBoost)
  {
    infoDisplay = 2;
    infoBoost = true;
  }
  else if (speedFault && !infoSpeed && wantedGear < 6)
  {
    infoDisplay = 3;
    infoSpeed = true;
  }
}

void rpmMeterUpdate()
{
  struct SensorVals sensor = readSensors();
  struct ConfigParam config = readConfig();

  int rpmPWM = map(sensor.curRPM, 0, config.maxRPM, 0, 255);
  analogWrite(rpmMeter, rpmPWM);
}

void updateSpeedo()
{
  struct SensorVals sensor = readSensors();
  //int speedPWM = map(sensor.curSpeed, 0, 255, 0, 255);
  analogWrite(speedoCtrl, sensor.curSpeed);
}

// Display update
void updateDisplay(Task *me)
{

  u8g2.clearBuffer();
  draw(wantedGear);
  u8g2.sendBuffer();

  if (w124rpm)
  {
    rpmMeterUpdate();
  }
  if (w124speedo)
  {
    updateSpeedo();
  }
}

void datalog(Task *me)
{
  if (datalogger)
  {
    struct SensorVals sensor = readSensors();

    if (debugEnabled)
    {
      debugEnabled = false;
    }
    Serial.print(sensor.curSpeed);
    Serial.print(F(";"));
    Serial.print(sensor.curRPM);
    Serial.print(F(";"));
    Serial.print(sensor.curTps);
    Serial.print(F(";"));
    Serial.print(gear);
    Serial.print(F(";"));
    Serial.print(sensor.curOilTemp);
    Serial.print(F(";"));
    Serial.print(sensor.curAtfTemp);
    Serial.print(F(";"));
    Serial.print(sensor.curLoad);
    Serial.print(F(";"));
    Serial.print(sensor.curBoost);
    Serial.print(F(";"));
    Serial.print(sensor.curBoostLim);
    Serial.print(F(";"));
    Serial.print(n2Speed);
    Serial.print(F(";"));
    Serial.print(n3Speed);
    Serial.print(F(";"));
    Serial.print(ratio);
    Serial.print(F(";"));
    Serial.println(gearSlip);
  }
}
