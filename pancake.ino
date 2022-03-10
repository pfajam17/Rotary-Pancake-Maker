#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27,20,4);
#include "A4988.h"

#define pin_sizeUp  18
#define pin_sizeDown  19
#define pin_queuePancake  21
#define pin_heatingCoil 11
#define pin_knob1 31
#define pin_knob2 33
#define pin_knob3 35
#define pin_knob4 37
#define pin_knob5 39
#define pin_stepperStep 5
#define pin_stepperDir  6
#define pin_temperatureSensor A0
#define pin_dosingCylinderIn  42
#define pin_dosingCylinderOut 44
#define pin_dosingCylinderEndSwitch 46

#define temperatureSetting1 150.0
#define temperatureSetting2 160.0
#define temperatureSetting3 170.0
#define temperatureSetting4 180.0
#define temperatureSetting5 190.0
#define temperatureTolerance  5.0

#define stepsPerRotation  200
#define transmissionFactor  51 
#define zones  6
#define stepsPerZone  stepsPerRotation * transmissionFactor / zones
#define stepperRPMs 200

#define bakingTimeSeconds 600
//as per the experiment

#define cylinderDiameter  16.5 //mm
#define cylinderArea  cylinderDiameter * cylinderDiameter * 3.1415 / 4    //mm^2
#define extensionSpeed  0.065 //mm per millisecond
#define extensionVolume1  20000   // mm^3 (20k equals 20ml)
#define extensionVolume2  30000
#define extensionVolume3  40000
#define extensionVolume4  50000
#define extensionVolume5  60000



volatile uint8_t startupDoodle = 1;
volatile uint8_t sizeDisplayLevel = 3;
volatile uint8_t sizeQ1 = 0;
volatile uint8_t sizeQ2 = 0;
volatile uint8_t sizeQ3 = 0;
int bakingSize = 0;
volatile uint8_t timer1Seconds = 0;
volatile uint8_t timer2Seconds = 0;
volatile uint8_t timer3Seconds = 0;

int occupiedZones = 0;
//number of heating zones with pancakes on them (in no particular order)

volatile float innerTemperature = 0.0;
volatile float selectedTemperature = 0.0;
float temperatureBuffer[5];
int temperatureBufferCounter = 0;

volatile int i;

bool startupPhase = true;
bool makingAPancakeRightNow = false;
bool emergencyFlag = false;

bool screenRefreshState = false;
bool screenRefreshStatePreviously = false;

int no_queuedPancakes = 0;

A4988 stepper(stepsPerRotation, pin_stepperDir, pin_stepperStep);



int main(){
  pinMode(pin_sizeUp, INPUT);
  pinMode(pin_sizeDown, INPUT);
  pinMode(pin_queuePancake, INPUT);
  pinMode(pin_knob1, INPUT);
  pinMode(pin_knob2, INPUT);
  pinMode(pin_knob3, INPUT);
  pinMode(pin_knob4, INPUT);
  pinMode(pin_knob5, INPUT);
  pinMode(pin_temperatureSensor, INPUT);
  pinMode(pin_heatingCoil, OUTPUT);
  pinMode(pin_stepperStep, OUTPUT);
  pinMode(pin_stepperDir, OUTPUT);
  pinMode(pin_dosingCylinderIn, OUTPUT);
  pinMode(pin_dosingCylinderOut, OUTPUT);
  pinMode(pin_dosingCylinderEndSwitch, INPUT);

  
  attachInterrupt(digitalPinToInterrupt(pin_sizeUp), ISRsizeUp, RISING);       //button interrupt to increase size parameter
  attachInterrupt(digitalPinToInterrupt(pin_sizeDown), ISRsizeDown, RISING);     //button interrupt to decrease size parameter
  attachInterrupt(digitalPinToInterrupt(pin_queuePancake), ISRqueuePancake, RISING);      //button interrupt to queue a new pancake

  stepper.begin(stepperRPMs, 1); //stepper speed 200 rpm (before transmission), full steps

  //timer config for the main timer (1s)
  TCCR1A |= (1 << COM1A1);    //enables compare output channel A on output pin 11 (OC1A)
  TCCR1B |= (1<< CS12) | (1 << CS10) | (1 << WGM12);    //highest prescaler at 1024    
  OCR1AH = (0x01);
  OCR1AL = (0x312D);
  //same compare value in A and B compare registers as the routine is to be called upon every second either way
  TIMSK1 |= (1 << ICIE1) | (1 << OCIE1A);


  //title and credit screen
  lcd.init();
  lcd.setCursor(3, 0);
  lcd.print("Rotary  Pancake  Maker");
  lcd.setCursor(5, 1);
  lcd.print("Stefan Haring");
  lcd.setCursor(5, 2);
  lcd.print("Jan Pfanner");
  lcd.setCursor(5, 3);
  lcd.print("Georg Schnabel");
  delay(4000);

  while(1){    
    
    if(startupPhase){     
      //preheat
      //when the temperature is reached, set startup to false and reconfigure screen refresh timer channel      
      digitalWrite(pin_heatingCoil, HIGH);  //bring the heat babey      
      digitalWrite(pin_dosingCylinderOut, HIGH); //extend the dosing cylinder if it isn't already
      
      if(innerTemperature < selectedTemperature){
        ;
      }else {
        startupPhase = false;        
      }      
    }    
    
    if(startupPhase && !emergencyFlag && (screenRefreshState != screenRefreshStatePreviously)){ //only execute during startup IF there's no emergency and IF the 1s-screen-refresh-flag has been set
    //startup screen refresh 
       startupSequenceScreenRefresh();
       screenRefreshStatePreviously = screenRefreshState;
    }else if( !startupPhase && !emergencyFlag && (screenRefreshState != screenRefreshStatePreviously)){
    //normal operation screen refresh
       regularScreenRefresh(); 
       screenRefreshStatePreviously = screenRefreshState;
    }

    if(sizeQ1 > 0 && !startupPhase && occupiedZones <3 && !emergencyFlag){
      makingAPancakeRightNow = true;
      bakingSize = sizeQ1;
      sizeQ1 = sizeQ2;
      sizeQ2 = sizeQ3;
      sizeQ3 = 0;
      //each queued pancake moves up one slot
      //the first queued value is moved into a different variable to free up a queue slot
    }

    //pour dough, move into heating zone and start timer
    if(makingAPancakeRightNow && !startupPhase && !emergencyFlag){
      ejectDough(bakingSize); //the step is included in the pouring function
      occupiedZones++;
      switch(occupiedZones){
        case 0: timer1Seconds = 0; break;
        case 1: timer2Seconds = 0; break;
        case 2: timer3Seconds = 0; break;
        case 3: break; //shouldn't happen
        default: break;
      }
      makingAPancakeRightNow = false;
    }

    //a pancake is done and needs ejection
    if((timer1Seconds >= bakingTimeSeconds || timer2Seconds >= bakingTimeSeconds || timer3Seconds >= bakingTimeSeconds) && !emergencyFlag){     //it doesn't matter which timer finishes first, they'll be mixed up after a few queues but the one that's done will always be the first in line
      for(i = 3; i > occupiedZones; i--){
        stepOneZone();    //ensure the pancake that's done gets to the final zone, no matter how many are being baked
      }
      stepOneZone();      //aaand push it out
      occupiedZones --;   //Houston, we've lost one
    }

    if(digitalRead(pin_knob1) == HIGH){
      selectedTemperature = temperatureSetting1;
    }else if(digitalRead(pin_knob2) == HIGH){
      selectedTemperature = temperatureSetting2;
    }else if(digitalRead(pin_knob3) == HIGH){
      selectedTemperature = temperatureSetting3;
    }else if(digitalRead(pin_knob4) == HIGH){
      selectedTemperature = temperatureSetting4;
    }else if(digitalRead(pin_knob5) == HIGH){
      selectedTemperature = temperatureSetting5;
    }else{
      emergencyMessage(2);
    }
    //check all 5 inputs from the knob to see which temperature setting is selected
    //if none works, there's an error and an error message is displayed accordingly

      
    //temperature controller with upper and lower limit +/- tolerance hysteresis 
    if((innerTemperature > selectedTemperature + temperatureTolerance) && !emergencyFlag){
      digitalWrite(pin_heatingCoil, LOW);
    } else if(innerTemperature < selectedTemperature - temperatureTolerance){
      digitalWrite(pin_heatingCoil, HIGH);
    }
    
  }
  
  //return 0; 
  //don't even need this one haha
}

void ISRsizeUp(){
  sizeDisplayLevel++;
  if(sizeDisplayLevel > 5){
      sizeDisplayLevel = 5;
  }
}

void ISRsizeDown(){
  sizeDisplayLevel--;
  if(sizeDisplayLevel < 1){
    sizeDisplayLevel = 1;
  }
}

void ISRqueuePancake(){
  if(sizeQ1 <= 0){
    sizeQ1 = sizeDisplayLevel;
    //queue in slot 1
  } else if (sizeQ2 <= 0){
    sizeQ2 = sizeDisplayLevel;
    //queue in slot 2
  } else if(sizeQ3 <= 0){
    sizeQ3 = sizeDisplayLevel;
    //queue in slot 3
  } else {
    exit;
    //don't queue
  }
}

ISR(TIMER1_COMPA_vect){ //just increment the timers, they're reset elsewhere
  timer1Seconds++;
  timer2Seconds++;
  timer3Seconds++;
  screenRefreshState != screenRefreshState;
}

void ejectDough(int bakingSize){ //needs a step at the end
  //push cylinder in accordance with a size 1 pancake's volume, gotta have 5 constants for the different cylinder distances
  uint16_t volume = 0;
  float extensionTime;
  switch (bakingSize){
    case 1: volume = extensionVolume1; break;
    case 2: volume = extensionVolume2; break;
    case 3: volume = extensionVolume3; break;
    case 4: volume = extensionVolume4; break;
    case 5: volume = extensionVolume5; break;
  }
  extensionTime = cylinderArea * extensionSpeed / volume;


  digitalWrite(pin_dosingCylinderIn, HIGH);
  delay(extensionTime);
  digitalWrite(pin_dosingCylinderIn, LOW);
  
  while(digitalRead(pin_dosingCylinderEndSwitch) == LOW){
    digitalWrite(pin_dosingCylinderOut, HIGH);
  } else {
    digitalWrite(pin_dosingCylinderOut, LOW);
  }
  stepOneZone();  
}

void stepOneZone(){
  stepper.move(stepsPerZone);
}

void readInnerTemperature(){   
  //2,983V -> 250°C (read as a value of 611)
  //2,016V -> 0°C  (read as a value of 412)
  //linear in between
  float rv;
  int tempReading = analogRead(pin_temperatureSensor);
  float temp = ((float)tempReading - 412) * 1.256;
  temperatureBuffer[temperatureBufferCounter] = temp;
  if(temp < 400 && temp > -40){ //only write and save the value by moving the buffer counter if it's within a realistic spectrum (unless you use the thing in Siberia or inside a bigger oven)
    temperatureBufferCounter++;
  }  
  if(temperatureBufferCounter > 4){ //when the buffer is full, temperature is averaged from 5 values and buffer is 'cleared' by allowing the values to be overwritten
    temperatureBufferCounter = 0;    
    for(i = 0; i < 5; i++){
      rv += temperatureBuffer[i];
    }
      
    innerTemperature = rv/5;
  }
}

void startupSequenceScreenRefresh(){
  if(startupDoodle >=5){
        startupDoodle = 1;
        lcd.clear();
        lcd.setCursor(6,0);
        lcd.print("Vorheizen:");
      }
  lcd.setCursor(3, 1);
  lcd.print("@@ ");
  startupDoodle++; 
}

void regularScreenRefresh(){
  lcd.clear();
  lcd.setCursor(6,0);
  lcd.print("Größe:");
  lcd.setCursor(3, 1);
  for(i = 0; i < sizeDisplayLevel; i++){
    lcd.print("@@");
  }
  lcd.setCursor(6, 3);
  lcd.print("Temperatur");
  lcd.setCursor(4,4);
  lcd.print(innerTemperature);
}

void emergencyMessage(int errorCode){
  //2 => knob reading error, 3 => zemperature reading error, 4=> actuator error
  emergencyFlag = true;
  switch (errorCode){
    case 0: exit; //in case of mistaken function call without an actual error present
    case 2: {
      lcd.clear();
      lcd.setCursor(6,0);
      lcd.print("Fehler im System: Temperatureinsteller überprüfen");
      break;
    }
    case 3:{
      lcd.clear();
      lcd.setCursor(6,0);
      lcd.print("Fehler im System: Temperatursensor defekt");
      break;
    }
    case 4: {
       lcd.clear();
        lcd.setCursor(6,0);
        lcd.print("Fehler im System: Motor blockiert");
        break;
    }
    default: exit; //for a call with a wrong error Code
    
  }
}
