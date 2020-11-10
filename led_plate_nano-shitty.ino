// SPI for avr - Version: Latest 
#include <SPI.h>

#define MASK_R 0x1  //red
#define MASK_G 0x2  //green
#define MASK_B 0x4  //blue
#define MASK_A 0x5  //all

typedef struct {
  union {
    struct {
      union {
        uint8_t r;
        uint8_t red;
      };
      union {
        uint8_t g;
        uint8_t green;
      };
      union {
        uint8_t b;
        uint8_t blue;
      };
    };
    uint8_t raw[3];
  };
} LED;

//LED led = {0x0A, 0x0A, 0x00};
#define N_LEDS 24
LED led[N_LEDS];

const byte numChars = 32;
char receivedChars[numChars];
char tmpChars[numChars];
boolean newData = false;

uint8_t brightness = 31;
uint16_t tOn_R = 1;
uint16_t tOff_R = 0;
uint16_t tOn_G = 10;
uint16_t tOff_G = 190;
uint16_t tOn_B = 0;
uint16_t tOff_B = 0;
uint16_t tOn_I = 1;
uint16_t tOff_I = 0;
unsigned long t, t_R, t_G, t_B, t_I;

//uint8_t red, green, blue;

void APA102(LED *ledarray, uint8_t leds, uint8_t brightness, uint8_t mask) {
  uint8_t *_ledarray = (uint8_t*) ledarray;
  
  SPI.beginTransaction(SPISettings(15000000, MSBFIRST, SPI_MODE0));
  
  //Start frame
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  SPI.transfer(0x00);
  
  for (int i = 0; i < (leds+leds+leds); i += 3) {
    SPI.transfer(0xE0 + brightness);
    
    if((mask & MASK_B) || (mask & MASK_A))
      SPI.transfer(0);
    else
      SPI.transfer(_ledarray[i +2]);
    
    if((mask & MASK_G) || (mask & MASK_A))
      SPI.transfer(0);
    else
      SPI.transfer(_ledarray[i +1]);
      
    if((mask & MASK_R) /*|| (mask & MASK_A)*/)  //TODO: Make the interval mask more universal
      SPI.transfer(0);
    else
      SPI.transfer(_ledarray[i +0]);
  }
  
  //End frame
  for (int i = 0; i < leds; i += 16); {
    SPI.transfer(0x00);
  }
  SPI.endTransaction();
}

void recvData() {
  static uint8_t ndx = 0;
  char endMarker = '\n';
  char rc;
  
  while(Serial.available() > 0 && newData == false) {
    rc = Serial.read();
    if( rc != endMarker) {
      receivedChars[ndx] = rc;
      ndx ++;
      if (ndx >= numChars) {
        ndx = numChars -1;
      }
    }
    else {
      receivedChars[ndx] = '\0'; // string termination
      ndx = 0;
      strcpy(tmpChars, receivedChars);
      Serial.println(tmpChars);
      newData = true;
    }
  }
}

void parseData() {
  char * strtokIndx;
  
  strtokIndx = strtok(tmpChars, ",:;");
  if(strtokIndx[0] == 'b') {
    //brightness
    Serial.println("Got bright data!");
    strtokIndx = strtok(NULL, ",:;");
    brightness = (uint8_t) atoi(strtokIndx);
    if (brightness > 31)
      brightness = 31;
    Serial.println(brightness);
  }
  else if(strtokIndx[0] == 'c') {
    //color
    Serial.println("Got colorful data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    for(int i = 0; i < N_LEDS; i++)
    {
      led[i].red = (uint8_t) atoi(strtokIndx);
    }
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    for(int i = 0; i < N_LEDS; i++)
    {
      led[i].green = (uint8_t) atoi(strtokIndx);
    }
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    for(int i = 0; i < N_LEDS; i++)
    {
      led[i].blue = (uint8_t) atoi(strtokIndx);
    }
  }
  else if(strtokIndx[0] == 'i') {
    //interval
    Serial.println("Got intermittent data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    tOn_I = (uint16_t) atoi(strtokIndx) * 1000;
    strtokIndx = strtok(NULL, ",:;");
    tOff_I = (uint16_t) atoi(strtokIndx) * 1000;
  }
  else if(strtokIndx[0] == 'd') {
    if(strtokIndx[1] == 'r') {
      //red duration
      Serial.println("Got enduring red data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn_R = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff_R = atoi(strtokIndx);
    }else if(strtokIndx[1] == 'g') {
      //green duration
      Serial.println("Got enduring green data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn_G = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff_G = atoi(strtokIndx);
    }else if(strtokIndx[1] == 'b') {
      //blue duration
      Serial.println("Got enduring blue data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn_B = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff_B = atoi(strtokIndx);
    }else{
      //duration
      Serial.println("Got enduring data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn_R, tOn_G, tOn_B = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff_R, tOff_G, tOff_B = (uint16_t) atoi(strtokIndx);
    }
  }
  else if(strtokIndx[0] == 'h') {
    Serial.print("Accepted Formats:\n\tbrightness: b:xx - 0 to 31\n\tcolor: c:xxx:xxx:xxx - 0 to 255\n\tduration: d:xx:xx - tOn, tOff in ms\n\tcolor duration dr, dg, db:xx:xx -tOn, tOff in ms\n\tinterval: i:xx:xx - tOn, tOff in sec\n\thelp: h\n");
  }
  newData = false;
}

void setup() {
    Serial.begin(9600);
    SPI.begin();
    t_R, t_G, t_B = millis();
}

void loop() {
  uint8_t mask;
  
  recvData();
  if(newData)
    parseData();
  
  t = millis();
  if(t - t_R >= tOn_R)
  {
    mask |= MASK_R;
  }
  if(t - t_R >= tOn_R + tOff_R)
  {
    mask &= ~MASK_R;
    t_R = millis();
  }
  
  if(t - t_G >= tOn_G)
  {
    mask |= MASK_G;
  }
  if(t - t_G >= tOn_G + tOff_G)
  {
    mask &= ~MASK_G;
    t_G = millis();
  }
  
  if(t - t_B >= tOn_B)
  {
    mask |= MASK_B;
  }
  if(t - t_B >= tOn_B + tOff_B)
  {
    mask &= ~MASK_B;
    t_B = millis();
  }

  if(t - t_I >= tOn_I)
  {
    mask |= MASK_A;
  }
  if(t - t_I >= tOn_I + tOff_I)
  {
    mask &= ~MASK_A;
    t_I = millis();
  }
  
  APA102(led, 24, brightness, mask);
  
}
