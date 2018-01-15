#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


// SPI display setup
#define OLED_MOSI  11   //D1
#define OLED_CLK   13   //D0
#define OLED_DC    9
#define OLED_CS    10
#define OLED_RESET 8

// Solenoid output pins
const int y3 = 2;
const int y4 = 3;
const int y5 = 4;
const int mpc = 5;
const int spc = 6;
const int tcc = 7;

// INPUT PINS
// Stick input 
const int whitepin = 31;
const int bluepin = 29;
const int greenpin = 27;
const int yellowpin = 33;

// Switches
const int tempSwitch = 22;
const int gdownSwitch = 23;
const int gupSwitch = 24;

// Car sensor input pins
const int tpspin = A0;
// map & rpm and load input coming here also.
// END INPUT PINS

// Internals, states
int newGear;
int gear = 2; // Start on gear 2
int prevgear = 1;
int const *pin;
Adafruit_SSD1306 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
static unsigned long thisMicros = 0;
static unsigned long lastMicros = 0;
int tccState = 0;
int prevtccState = 0;
int prevgdownState = 0;
int prevgupState = 0;
int gupState = 0;
int gdownState = 0;
// End of internals

// Environment configuration
// Shift delay
int shiftDelay = 500;
int shiftStartTime = 0;
int shiftDuration = 0;

// Do we want torque lock?
boolean tccEnabled = false; // no

// Are we in a real car?
boolean incar = false; // no.

// Do we use stick control?
boolean stick = true; // yes.

// Manual control?
boolean manual = false;

// Actual transmission there?
boolean trans = true;

// Default for blocking gear switches (do not change.)
boolean switchBlocker = false;
// Default for health (do not change.)
boolean health = false;
// Output to serial console
boolean debugEnabled = true;

// End of environment conf

void setup() {
  
   // TCCR2B = TCCR2B & 0b11111000 | 0x07;
  // MPC and SPC should have frequency of 1000hz
  // TCC should have frequency of 100hz
  // Lower the duty cycle, higher the pressures.
  Serial.begin(9600);

  display.begin(SSD1306_SWITCHCAPVCC);
  display.display();
  delay(1000);
  display.clearDisplay();
  display.setTextSize(5);
  display.setTextColor(WHITE);
 
  // Solenoid outputs
  pinMode(y3, OUTPUT); // 1-2/4-5 solenoid
  pinMode(y4,OUTPUT); // 2-3
  pinMode(y5,OUTPUT); // 3-4
  pinMode(spc,OUTPUT); // shift pressure
  pinMode(mpc,OUTPUT); // modulation pressure
  pinMode(tcc,OUTPUT); // lock

  //For manual control
  pinMode(gupSwitch,INPUT); // gear up
  pinMode(gdownSwitch,INPUT); // gear down
  
  //For stick control
  pinMode(whitepin,INPUT);
  pinMode(bluepin,INPUT);
  pinMode(greenpin,INPUT);
  pinMode(yellowpin,INPUT);

  // Drive solenoids are all off.
  analogWrite(y3,0);
  analogWrite(y4,0);
  analogWrite(y5,0);
  analogWrite(spc,0); // No pressure here by default.
  analogWrite(mpc,255); // We want constant pressure here.
  analogWrite(tcc,0); // No pressure here by default.

  //Internals
  pinMode(tpspin,INPUT); // throttle position sensor
  
  Serial.println("Started.");
  updateDisplay();
}

// UI STAGE
// Control for what user sees and how gearbox is used with
// 

// Display update
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(3,0);
  if ( prevgear <= 5 ) { display.print(prevgear); };
  if ( prevgear == 6 ) { display.print("N"); };
  if ( prevgear == 7 ) { display.print("R"); };
  if ( prevgear == 8 ) { display.print("P"); };
  display.print("->");
  if ( gear <= 5 ) { display.print(gear); };
  if ( gear == 6 ) { display.print("N"); };
  if ( gear == 7 ) { display.print("R"); };
  if ( gear == 8 ) { display.print("P"); }; 
  display.display();
}

// INPUT
// Polling for stick control
void pollstick() {
  // Read the stick.
  int whiteState = digitalRead(whitepin);
  int blueState = digitalRead(bluepin);
  int greenState = digitalRead(greenpin);
  int yellowState = digitalRead(yellowpin);
  int wantedGear = gear;

  // Determine position
  if (whiteState == HIGH && blueState == HIGH && greenState == HIGH && yellowState == LOW ) { wantedGear = 8; } // P
  if (whiteState == LOW && blueState == HIGH && greenState == HIGH && yellowState == HIGH ) { wantedGear = 7; } // R
  if (whiteState == HIGH && blueState == LOW && greenState == HIGH && yellowState == HIGH ) { wantedGear = 6; } // N
  if (whiteState == LOW && blueState == LOW && greenState == HIGH && yellowState == LOW ) { wantedGear = 5; }
  if (whiteState == LOW && blueState == LOW && greenState == LOW && yellowState == HIGH ) { wantedGear = 4; }
  if (whiteState == LOW && blueState == HIGH && greenState == LOW && yellowState == LOW ) { wantedGear = 3; }
  if (whiteState == HIGH && blueState == LOW && greenState == LOW && yellowState == LOW ) { wantedGear = 2; }
  if (whiteState == HIGH && blueState == HIGH && greenState == LOW && yellowState == HIGH ) { wantedGear = 1; }
  
  for ( int newGear = gear; wantedGear >= gear++; newGear++ ); { gearchange(); }
  for ( int newGear = gear; wantedGear <= gear--; newGear-- ); { gearchange(); }
  
  if ( debugEnabled ) {
    Serial.println("pollstick: Stick says");
    Serial.println(whiteState);
    Serial.println(blueState);
    Serial.println(greenState);
    Serial.println(yellowState);
    Serial.println("pollstick: Requested gear");
    Serial.println(wantedGear);
    Serial.println(gear);
  }
}

// Polling for manual switch keys
void pollkeys() {
  gupState == digitalRead(gupSwitch); // Gear up
  gdownState == digitalRead(gdownSwitch); // Gear down


  if (gdownState != prevgdownState || gupState != prevgupState ) {
    if (gdownState == LOW && gupState == HIGH) {
      prevgupState = gupState;
      if ( debugEnabled ) { Serial.println("pollkeys: Gear up button"); }
      gearup();
    } else if (gupState == LOW && gdownState == HIGH) {
      prevgdownState = gdownState;
      if ( debugEnabled ) { Serial.println("pollkeys: Gear down button"); }
      geardown();
    }
  }
}

// For manual control, gear up
void gearup() {
  if ( ! gear > 5 ) {  // Do nothing if we're on N/R/P
    prevgear = gear;
    gear++;
    if (gear > 4) { gear = 5; } // Make sure not to switch more than 5.
    if ( ! prevgear == gear) { 
      if ( debugEnabled ) { Serial.println("gearup: Gear up requested"); }
      gearchange(); 
    }
  }
}

// For manual control, gear down
void geardown() {
  if ( ! gear > 5 ) {  // Do nothing if we're on N/R/P
    prevgear = gear;
    gear--;
    if (gear < 2) { gear = 1; } // Make sure not to switch less than 1.
    if ( ! prevgear == gear) {
      if ( debugEnabled ) { Serial.println("geardown: Gear down requested"); }
      gearchange(); }
  }
}


// END OF UI STAGE / INPUT

// CORE
// no pressure alteration happening yet
//  
// gearSwitch logic
void switchGearStart() {
   shiftStartTime = millis(); 
   switchBlocker = true;
   Serial.print(switchBlocker);
   if ( debugEnabled ) { Serial.println("switchGearStart: Begin of gear change:"); Serial.println(*pin); }
   analogWrite(spc,255); // We could change shift pressure here 
   analogWrite(*pin,255); // Beginning of gear change
}

void switchGearStop() {
   analogWrite(*pin,0); // End of gear change
   analogWrite(spc,0); // let go of shift pressure
   switchBlocker = false;
   if ( debugEnabled ) { Serial.println("switchGearStop: End of gear change:"); Serial.println(*pin); }
   prevgear = gear; // Make sure previous gear is known
}

void polltrans() {
   shiftDuration = millis() - shiftStartTime;
   if ( shiftDuration > shiftDelay) { switchGearStop(); };
 
   //Raw value for pwm control (0-255) for SPC solenoid, see page 9: http://www.all-trans.by/assets/site/files/mercedes/722.6.1.pdf
   // "Pulsed constantly while idling in Park or Neutral at approximately 40% Duty cycle" <- 102/255 = 0.4
   if ( gear > 5 ) {
     analogWrite(spc, 102); 
   }
}

void gearchange() {
 
    if ( switchBlocker == false ) { 
      switch (newGear) {
      case 1: 
        if ( prevgear == 2 ) { pin = &y3; switchGearStart(); gear = 1; };
        break;
      case 2:
        if ( prevgear == 1 ) { pin = &y3; switchGearStart(); gear = 2; };
        if ( prevgear == 3 ) { pin = &y5; switchGearStart(); gear = 2; };
        break;
      case 3:
        if ( prevgear == 2 ) { pin = &y5; switchGearStart(); gear = 3; };
        if ( prevgear == 4 ) { pin = &y4; switchGearStart(); gear = 3; };
        break;
      case 4:
        if ( prevgear == 3 ) { pin = &y4; switchGearStart(); gear = 4; };
        if ( prevgear == 5 ) { pin = &y3; switchGearStart(); gear = 4; };
        break;
      case 5:
        if ( prevgear == 4 ) { pin = &y3; switchGearStart(); gear = 5; };
        break;
      case 6:
        gear = 6; // mechanical "N"
        break;
      case 7:
        gear = 7; // mechanical "R" 
        break;
      case 8:
        gear = 8; // mechanical "P" 
        break;
      default:
      break;
    }
    if ( debugEnabled ) { 
      Serial.println("gearChange: requested change from:"); 
      Serial.println(prevgear);
      Serial.println("to");
      Serial.println(gear);
    }
  }
  updateDisplay();
  
}

// END OF CORE

void checkHealth() {
  // Get temperature
  int tempState = digitalRead(tempSwitch);
  int prevtempState = 0;
 // if ( tempState ==  ) { health == true; };
}

void loop() {
  checkHealth();
  // If we have a temperature, we can assume P/N switch has moved to R/N. (Lever switch and temp sensor are in series)
  if (( incar && health ) || ( ! incar )) {
    if ( stick ) { pollstick(); } // using stick
    if ( manual ) { pollkeys(); } // using manual control
    if ( trans ) { polltrans(); } // using transmission
  }
}
