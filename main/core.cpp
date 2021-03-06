#include <Arduino.h>
#include "include/pins.h"
#include "include/calc.h"
#include "include/maps.h"
#include "include/sensors.h"
#include "include/input.h"
#include "include/core.h"
#include "include/config.h"
#include <SoftTimer.h>

// CORE
// input:pollstick -> core:decideGear -> core:gearChange[Up|Down] -> core:switchGearStart -> core:boostControl
// input:polltrans -> core:switchGearStop
// poll -> evaluateGear

// Obvious internals
byte gear = 2; // Start on gear 2
byte newGear = 2;
byte pendingGear = 2;
float ratio;

// Shift pressure defaults
int spcPercentVal = 100;
int mpcPercentVal = 100;

// for timers
unsigned long int shiftStartTime, shiftStopTime, delaySinceLast = 0;
unsigned long int shiftDuration = 0;

// Solenoid used
int cSolenoidEnabled = 0;
int cSolenoid = 0; // Change solenoid pin to be controlled.
int lastMapVal;
int shiftLoad = 0;
int shiftAtfTemp = 0;
boolean preShift, postShift, preShiftDone, shiftDone, postShiftDone = false;

// Gear shift logic
// Beginning of gear change phase
// Send PWM signal to defined solenoid in transmission conductor plate.
void switchGearStart(int cSolenoid, int spcVal, int mpcVal)
{
  struct ConfigParam config = readConfig();
  delaySinceLast = millis() - shiftStopTime;

  if (debugEnabled)
  {
    Serial.print(F("[switchGearStart->switchGearStart] Begin of gear change current-solenoid: "));
    Serial.print(gear);
    Serial.print(F("-"));
    Serial.println(newGear);
  }

  if (trans && (delaySinceLast > config.nextShiftDelay))
  {
    shiftBlocker = true;   // Blocking any other shift operations during the shift
    preShift = true;       // Poke start of preShift mode (input.cpp polltrans checks these).
    postShiftDone = false; // Reset previous states
    preShiftDone = false;
    shiftDone = false;

    spcPercentVal = spcVal;
    mpcPercentVal = mpcVal;
    cSolenoidEnabled = cSolenoid;
  }
  else
  {
    if (debugEnabled)
    {
      Serial.print(F("[switchGearStart->switchGearStart] blocking change or transmission disabled delaySinceLast/nextShiftDelay: "));
      Serial.print(delaySinceLast);
      Serial.print(F("/"));
      Serial.println(config.nextShiftDelay);
    }
  }
}

void doPreShift()
{
  struct ConfigParam config = readConfig();
  struct SensorVals sensor = readSensors();

  // Things to do before actual shift, example is for waiting boost to settle to the target. (boostControl should let it drop in this scenario).
  if ((boostLimit && ((sensor.curBoostLim > 0) && (sensor.curBoost <= sensor.curBoostLim - config.boostDrop)) || sensor.curBoostLim == 0) || !boostLimit)
  {
    preShift = false;
    preShiftDone = true;

    if (debugEnabled)
    {
      Serial.print(F("[switchGearStart->preShift] curBoost/curBoostLim: "));
      Serial.print(sensor.curBoost);
      Serial.print(F("/"));
      Serial.println(sensor.curBoostLim);
    }
  }
  else
  {
    Serial.print(F("[switchGearStart->preShift] blocking curBoost/curBoostLim: "));
    Serial.print(sensor.curBoost);
    Serial.print(F("/"));
    Serial.println(sensor.curBoostLim);
  }
}

void doShift()
{
  // Actual shift
  int spcSetVal = (100 - spcPercentVal) * 2.55;
  int mpcSetVal = (100 - mpcPercentVal) * 2.55;

  if (adaptive)
  {
    int spcModVal = adaptSPC(lastMapVal, lastXval, lastYval);
    if (spcModVal < 10)
    {
      spcModVal = 10;
    };
    if (spcModVal > 190)
    {
      spcModVal = 200;
    };
    spcPercentVal = spcModVal / 100 * spcSetVal;
  }

  if (spcPercentVal > 100)
  {
    spcPercentVal = 100; // to make sure we're on the bounds.
    if (debugEnabled)
    {
      Serial.println(F("[switchGearStart->switchGearStart] SPC high limit hit."));
    }
  }

  if (spcPercentVal < 10)
  {
    spcPercentVal = 10; // to be on safe side.
    if (debugEnabled)
    {
      Serial.println(F("[switchGearStart->switchGearStart] SPC low limit hit."));
    }
  }
  spcSetVal = (100 - spcPercentVal) * 2.55; // these are calculated twice to make sure if there is changes they are noted.
  mpcSetVal = (100 - mpcPercentVal) * 2.55;

  shiftStartTime = millis(); // Beginning to count shiftStartTime

  analogWrite(tcc, 0);
  analogWrite(spc, spcSetVal);
  analogWrite(mpc, mpcSetVal);
  analogWrite(cSolenoidEnabled, 255); // Beginning of gear change

  if (debugEnabled)
  {
    Serial.print(F("[switchGearStart->doShift] spcPercentVal/mpcPercentVal "));
    Serial.print(spcPercentVal);
    Serial.print(F("/"));
    Serial.println(mpcPercentVal);
  }
  preShiftDone = false;
  shiftDone = true;
}

void doPostShift()
{
  // You can do post shift stuff here.
  if (debugEnabled)
  {
    Serial.println(F("[switchGearStart->postShift] completed. "));
  }

  postShift = false;
  postShiftDone = true;
  shiftPending = false;
  shiftBlocker = false;
}

// End of gear change phase
void switchGearStop()
{
  analogWrite(cSolenoidEnabled, 0); // turn shift solenoid off
  analogWrite(spc, 0);              // spc off
  analogWrite(mpc, 0);              // mpc off
  if (evalGear)
  {
    gear = evaluateGear();
  }
  else
  {
    gear = pendingGear; // we can happily say we're on new gear
  }
  shiftStopTime = millis();

  if (debugEnabled)
  {
    Serial.print(F("[switchGearStop->switchGearStop] End of gear change current-solenoid: "));
    Serial.print(gear);
    Serial.print(F("-"));
    Serial.print(newGear);
    Serial.print(F("-"));
    Serial.println(cSolenoid);
  }
  shiftStartTime = 0;
  postShift = true;
}

// upshift parameter logic gathering

void gearchangeUp(int newGear)
{
  struct SensorVals sensor = readSensors();
  if (shiftBlocker == false && shiftPending == true)
  {
    pendingGear = newGear;
    shiftLoad = sensor.curLoad;
    shiftAtfTemp = sensor.curAtfTemp;

    if (debugEnabled)
    {
      Serial.print(F("[gearChangeUp->gearChangeUp] performing change prev-new: "));
      Serial.print(gear);
      Serial.print(F("->"));
      Serial.println(newGear);
    }

    switch (newGear)
    {
    case 1:
      gear = 1;
      break;
    case 2:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeUp->switchGearStart] Solenoid y3 requested with spcMap12/mpcMap12, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y3, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 100;
        switchGearStart(y3, readMap(spcMap12, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap12, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 3:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeUp->switchGearStart] Solenoid y4 requested with spcMap23/mpcMap23, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y4, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 130;
        switchGearStart(y4, readMap(spcMap23, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap23, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 4:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeUp->switchGearStart] Solenoid y5 requested with spcMap34/mpcMap34, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y5, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 160;
        switchGearStart(y5, readMap(spcMap34, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap34, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 5:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeUp->switchGearStart] Solenoid y3 requested with spcMap45/mpcMap45, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y3, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 190;
        switchGearStart(y3, readMap(spcMap45, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap45, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    default:
      break;
    }
  }
  else if (debugEnabled)
  {
    Serial.println(F("[gearChangeUp->gearChangeUp] Blocking change"));
  }
}

// downshift parameter logic gathering
void gearchangeDown(int newGear)
{
  struct SensorVals sensor = readSensors();
  if (shiftBlocker == false && shiftPending == true)
  {
    pendingGear = newGear;
    shiftLoad = sensor.curLoad;
    shiftAtfTemp = sensor.curAtfTemp;
    if (debugEnabled)
    {
      Serial.print(F("[gearChangeDown->gearChangeDown] performing change prev-new: "));
      Serial.print(gear);
      Serial.print(F("->"));
      Serial.println(newGear);
    }

    switch (newGear)
    {
    case 1:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeDown->switchGearStart] Solenoid y3 requested with spcMap21/mpcMap21, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y3, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 210;
        switchGearStart(y3, readMap(spcMap21, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap21, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 2:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeDown->switchGearStart] Solenoid y4 requested with spcMap32/mpcMap32, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y4, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 240;
        switchGearStart(y4, readMap(spcMap32, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap32, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 3:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeDown->switchGearStart] Solenoid y5 requested with spcMap43/mpcMap43, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y5, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 270;
        switchGearStart(y5, readMap(spcMap43, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap43, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 4:
      if (debugEnabled)
      {
        Serial.print(F("[gearchangeDown->switchGearStart] Solenoid y3 requested with spcMap54/mpcMap54, load/atfTemp "));
        Serial.print(sensor.curLoad);
        Serial.print(F("-"));
        Serial.println(sensor.curAtfTemp);
      }
      if (!sensors)
      {
        switchGearStart(y3, 100, 100);
      }
      if (sensors)
      {
        lastMapVal = 300;
        switchGearStart(y3, readMap(spcMap54, sensor.curLoad, sensor.curAtfTemp), readMap(mpcMap54, sensor.curLoad, sensor.curAtfTemp));
      }
      break;
    case 5:
      gear = 5;
      break;
    default:
      break;
    }
  }
  else if (debugEnabled)
  {
    Serial.println(F("[gearChangeUp->gearChangeUp] Blocking change"));
  }
}

// Logic for automatic new gear, this makes possible auto up/downshifts.
void decideGear(Task *me)
{
  int moreGear = gear + 1;
  int lessGear = gear - 1;
  struct SensorVals sensor = readSensors();

  // Determine speed related downshift and upshift here.
  int autoGear = readMap(gearMap, sensor.curTps, sensor.curSpeed);

  if (!shiftBlocker && !shiftPending && !speedFault && wantedGear < 6 && stickCtrl)
  {
    if ((autoGear > gear && wantedGear > gear && fullAuto) || (!fullAuto && wantedGear > gear))
    {
      int newGear = moreGear;
      if (debugEnabled)
      {
        Serial.println(F(""));
        Serial.print(F("[decideGear->gearchangeUp] tpsPercent-vehicleSpeed: "));
        Serial.print(sensor.curTps);
        Serial.print(F("-"));
        Serial.println(sensor.curSpeed);
      }
      if (debugEnabled)
      {
        Serial.print(F("[decideGear->gearchangeUp] wantedGear-autoGear-newGear-gear: "));
        Serial.print(wantedGear);
        Serial.print(F("-"));
        Serial.print(autoGear);
        Serial.print(F("-"));
        Serial.print(newGear);
        Serial.print(F("-"));
        Serial.println(gear);
      }
      if (evalGear)
      {
        int evaluatedGear = evaluateGear();
        if (evaluatedGear == gear)
        {
          shiftPending = true;
          gearchangeUp(newGear);
        }
        else
        {
          if (debugEnabled)
          {
            Serial.println("Blocking shift, evaluatedGear != gear");
          }
        }
      }
      else
      {
        shiftPending = true;
        gearchangeUp(newGear);
      }
    }

    if ((autoGear < gear && fullAuto) || (wantedGear < gear && !fullAuto))
    {
      int newGear = lessGear;
      if (debugEnabled)
      {
        Serial.println(F(""));
        Serial.print(F("[decideGear->gearchangeDown] tpsPercent-vehicleSpeed: "));
        Serial.print(sensor.curTps);
        Serial.print(F("-"));
        Serial.println(sensor.curSpeed);
      }
      if (debugEnabled)
      {
        Serial.print(F("[decideGear->gearchangeDown] wantedGear-autoGear-newGear-gear: "));
        Serial.print(wantedGear);
        Serial.print(F("-"));
        Serial.print(autoGear);
        Serial.print(F("-"));
        Serial.print(newGear);
        Serial.print(F("-"));
        Serial.println(gear);
      }
      if (evalGear)
      {
        int evaluatedGear = evaluateGear();
        if (evaluatedGear == gear)
        {
          shiftPending = true;
          gearchangeDown(newGear);
        }
        else
        {
          if (debugEnabled)
          {
            Serial.println("Blocking shift, evaluatedGear != gear");
          }
        }
      }
      else
      {
        shiftPending = true;
        gearchangeDown(newGear);
      }
    }
  }
}

int evaluateGear()
{
  struct SensorVals sensor = readSensors();
  int incomingShaftSpeed = 0;
  int measuredGear = 0;

  if (n3Speed == 0)
  {
    incomingShaftSpeed = n2Speed * 1.64;
  }
  else
  {
    incomingShaftSpeed = n2Speed;
    //when gear is 2, 3 or 4, n3 speed is not zero, and then incoming shaft speed (=turbine speed) equals to n2 speed)
  }
  if (n3Speed == 0 && sensor.curSpeed < 20)
  {
    measuredGear = 1; // If we're near standstill and there is no N3 info, we can assume that we're on 1.
  }
  else
  {
    ratio = (float)incomingShaftSpeed / vehicleSpeedRevs;
    measuredGear = gearFromRatio(ratio);
  }

  return measuredGear;
}

float ratioFromGear(int inputGear)
{
  float gearRatio;
  switch (inputGear)
  {
  case 1:
    gearRatio = 3.59;
    return gearRatio;
    break;
  case 2:
    gearRatio = 2.19;
    return gearRatio;
    break;
  case 3:
    gearRatio = 1.41;
    return gearRatio;
    break;
  case 4:
    gearRatio = 1.00;
    return gearRatio;
    break;
  case 5:
    gearRatio = 0.83;
    return gearRatio;
    break;
  default:
    gearRatio = 0.00;
    return gearRatio;
    break;
  }
}

int gearFromRatio(float inputRatio)
{
  if (inputRatio < 3.82 && inputRatio > 3.46)
  {
    int returnGear = 1;
    return returnGear;
  }
  else if (inputRatio < 2.62 && inputRatio > 2.26)
  {
    int returnGear = 2;
    return returnGear;
  }
  else if (inputRatio < 1.60 && inputRatio > 1.35)
  {
    int returnGear = 3;
    return returnGear;
  }
  else if (inputRatio < 1.10 && inputRatio > 0.94)
  {
    int returnGear = 4;
    return returnGear;
  }
  else if (inputRatio < 0.90 && inputRatio > 0.60)
  {
    int returnGear = 5;
    return returnGear;
  }
  else
  {
    int returnGear = 6;
    return returnGear;
  }
}

float getGearSlip()
{
  static float maxRatio[5], minRatio[5];
  float slip;

  if (ratio > maxRatio[gear])
  {
    maxRatio[gear] = ratio;
  }
  else if (ratio < minRatio[gear] && ratio > 0.00)
  {
    minRatio[gear] = ratio;
  }
  slip = maxRatio[gear] - minRatio[gear];

  return slip;
}
// END OF CORE
