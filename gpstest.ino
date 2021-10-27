
/* Pinout
USB->Serial (PA9/PA10)
Display I2C (PB6/PB7)
GPS (PB10/PB11)
*/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306_STM32.h>
#include <TinyGPS.h>


#define OLED_RESET 4

#define SPEED_THR  2

#define SPEED_ALR_THR_ON  78
#define SPEED_ALR_THR_OFF  76
#define SPEED_FAST_STEP    2

#define SPEAKER_TONE_ALARM 880
#define SPEAKER_TONE_ALARM_INC 80

#define NLIMITS 3

#define SPEAKER_PIN PB13

Adafruit_SSD1306 display(OLED_RESET);

TinyGPS gps;

#if (SSD1306_LCDHEIGHT != 32)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif
#define DISPLAY_I2C_ADDR 0x3C // for 128x32

//#if (SSD1306_LCDHEIGHT != 64)
//#error("Height incorrect, please fix Adafruit_SSD1306.h!");
//#endif
//#define DISPLAY_I2C_ADDR 0x3C // for 128x32  3D?

bool isFixing = true;
bool isShowMessage = false;
bool signalOn = false;

uint32_t startTime;
uint32_t procTime;

unsigned int uPrevSpeed;

struct SpeedLimit {
  unsigned int speed;
  unsigned int alr_on;
  unsigned int alr_off;
  unsigned int alr_override;
  unsigned int tone;
};

const struct SpeedLimit SpeedLimits[NLIMITS] = {
  { 60, 60, 58, 62, SPEAKER_TONE_ALARM},
  { 80, 78, 76, 85, SPEAKER_TONE_ALARM},
  { 110, 108, 106, 115, SPEAKER_TONE_ALARM}
};

void setup() {
  
  pinMode(PC13, OUTPUT);
  digitalWrite(PC13,LOW);   // turn the LED on (HIGH is the voltage level)
  
  display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C_ADDR);  // initialize with the I2C addr 0x3D (for the 128x64) ???
  display.setTextColor(WHITE);
  demo();
  digitalWrite(PC13, HIGH);    // turn the LED off by making the voltage LOW
  Serial.begin(115200);
  Serial.println("GPS start....");

  Serial3.begin(9600); //PB10/PB11 (RXTX3) GPS port

  procTime = startTime = millis();
  
}

bool newData = false;

void loop() {
  // read GPS
  //bool newData = false;
  if(Serial3.available()) {
    digitalWrite(PC13,LOW);
    while(Serial3.available() && !newData) {
      char ch = Serial3.read();
      Serial.print(ch);
      if (gps.encode(ch)) { // Did a new valid sentence come in?
        if(isFixing) {
          tone(SPEAKER_PIN, 800, 200);
          isFixing = false;
        }
        newData = true;
      }
    }
    digitalWrite(PC13, HIGH);
  } 

  // Processing cycle
  if(millis() - procTime >= 1000L) { // once per sec
    procTime = millis();
    if(isFixing) { // still fixing
      unsigned long chars;
      unsigned short sentences, failed;
      gps.stats(&chars, &sentences, &failed);
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0,0);
      if(chars==0 && millis() - startTime > 15000L) {
        // no device
        display.print("No GPS connection");
        display.display();
        Serial.println("No device");
        delay(10000);
        nvic_sys_reset();
        return;
      }
      
      if(isShowMessage) {
        display.println("Fixing...");
        isShowMessage=false;
      } else {
        display.println();
        isShowMessage=true; 
      }

      display.print(chars); display.print(" ");
      display.print(sentences); display.print(" ");
      display.print(failed);
      
      display.display();
    } 
    else { // do logic
      if (newData) {
        processData();
        newData = false;
      }
    }
  } 
}


void demo() {
  displaySpeed(0, 1, 5, 0, 0, 60);
  tone(SPEAKER_PIN, 20);
  delay(400);
  displaySpeed(9, 2, 4, 90, -1, 60);
  tone(SPEAKER_PIN, 400);
  delay(400);
  displaySpeed(99, 2, 3, 180, 1, 110);
  //display.drawRect(0, 0, 92, 32, WHITE);
  tone(SPEAKER_PIN, 800);
  delay(400);
  displaySpeed(130, 2, 2, 270, 1, 0);
  //display.drawRect(0, 0, 92, 32, WHITE);
  display.drawFastHLine(1, 1, 92, WHITE);
  tone(SPEAKER_PIN, 1600);
  delay(400);
  noTone(SPEAKER_PIN);
}

void processData() {
    float flat, flon, fspeed, fcourse;
    unsigned long age, hdop;
    unsigned int sats;
    sats = gps.satellites();
    fspeed = gps.f_speed_kmph();
    hdop = gps.hdop();
    fcourse = gps.f_course();
    //sats = sats == TinyGPS::GPS_INVALID_SATELLITES ? 0 : sats;
    //hdop = hdop == TinyGPS::GPS_INVALID_HDOP ? 0 : hdop;
    fspeed = fspeed == TinyGPS::GPS_INVALID_F_SPEED ? 0 : fspeed;
    fcourse = fcourse == TinyGPS::GPS_INVALID_F_ANGLE ? 0 : fcourse;
//    gps.f_get_position(&flat, &flon, &age);
//    flat = flat == TinyGPS::GPS_INVALID_F_ANGLE ? 0.0 : flat;
//    flon = flon == TinyGPS::GPS_INVALID_F_ANGLE ? 0.0 : flon;
//    Serial.println();
//    Serial.print("LAT=");
//    Serial.print(flat, 6);
//    Serial.print(" LON=");
//    Serial.print(flon, 6);
//    Serial.print(" SAT=");
//    Serial.print(sats);
//    Serial.print(" PREC=");
//    Serial.print(hdop);
//    Serial.print(" SPEED=");
//    Serial.print(speed, 2);
    //unsigned int uspeed = (int)fspeed;
    //if(fspeed-uspeed >= 0.5) uspeed++;
    unsigned int uspeed = round(fspeed);
    if(uspeed < SPEED_THR) uspeed = 0;
    int limit_idx = getLimit(uspeed);
    unsigned int limit_speed = limit_idx < 0 ? 0 : SpeedLimits[limit_idx].speed;
    displaySpeed(uspeed, sats, round(hdop/100.0f), round(fcourse), uspeed-uPrevSpeed, limit_speed);
    
    if(!signalOn) {
      if(uspeed > SPEED_ALR_THR_ON) {
        signalOn = true;
        tone(SPEAKER_PIN, SPEAKER_TONE_ALARM+(uspeed-SPEED_ALR_THR_ON)*SPEAKER_TONE_ALARM_INC);
      }
    } else { // signal on
       if(uspeed < SPEED_ALR_THR_OFF) {
        signalOn = false;
        noTone(SPEAKER_PIN);
      }    
    }

    uPrevSpeed = uspeed;
}

void displaySpeed(unsigned int uspeed, unsigned int sats, unsigned int hdop, unsigned int course, int diff, unsigned int limit_speed) {
  display.clearDisplay();
  display.setTextSize(4);
  display.setCursor(0,0);
  char sspeed[4]="   ";
  int pos = 2;
  do {
    sspeed[pos] = (uspeed % 10)+'0';
    uspeed/=10;
    pos--;
  } while(uspeed && pos>=0);
  display.print(sspeed);
  display.setTextSize(1); // 2
  display.setCursor(80,24);
  display.print("km/h");
  /*
  display.setTextSize(1);
  display.setCursor(100,0);
  display.print(sats);
  display.print("/");
  display.print(hdop);
  display.setCursor(92,8);
  display.print(course);
  display.print(" ");
  display.print(diff ==0 ? ' ' : diff>0 ? 'U' : 'D');
  */
  if(limit_speed > 0) {
    display.setTextSize(2);
    display.setCursor(92,0);
    display.print(limit_speed);
  }
  display.display();
}

int getLimit(unsigned int uspeed) {
  for(int i=0; i<NLIMITS; i++) {
    if(uspeed < SpeedLimits[i].alr_override) {
      return i;
    }
  }
  return -1;
}
