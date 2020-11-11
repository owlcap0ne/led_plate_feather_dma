// SPI for avr - Version: Latest 
#include <SPI.h>
#include <EEPROM.h>

#define MASK_R 0x1  //red
#define MASK_G 0x2  //green
#define MASK_B 0x4  //blue
#define MASK_A 0x8  //all

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

typedef struct {
  uint8_t   brightness;
  uint16_t  tOn_R,
            tOff_R,
            tOn_G,
            tOff_G,
            tOn_B,
            tOff_B,
            tOn_I,
            tOff_I;
} CONFIG;

//LED led = {0x0A, 0x0A, 0x00};
#define N_LEDS 24
LED led[N_LEDS];
CONFIG config = {
  .brightness = 31,
  .tOn_R   = 1,
  .tOff_R  = 0,
  .tOn_G   = 0,
  .tOff_G  = 0,
  .tOn_B   = 0,
  .tOff_B  = 0,
  .tOn_I   = 1,
  .tOff_I  = 0
};

const byte numChars = 32;
char receivedChars[numChars];
char tmpChars[numChars];
boolean newData = false;

/* 
uint8_t brightness = 31;
uint16_t tOn_R = 1;
uint16_t tOff_R = 0;
uint16_t tOn_G = 10;
uint16_t tOff_G = 190;
uint16_t tOn_B = 0;
uint16_t tOff_B = 0;
uint16_t tOn_I = 1;
uint16_t tOff_I = 0;
*/
unsigned long t, t_R, t_G, t_B, t_I;

uint8_t red, green, blue;

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
    config.brightness = (uint8_t) atoi(strtokIndx);
    if (config.brightness > 31)
      config.brightness = 31;
    Serial.println(config.brightness);
  }
  else if(strtokIndx[0] == 'c') {
    //color
    Serial.println("Got colorful data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    red = (uint8_t) atoi(strtokIndx);
    for(int i = 0; i < N_LEDS; i++)
    {
      led[i].red = red;
    }
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    green = (uint8_t) atoi(strtokIndx);
    for(int i = 0; i < N_LEDS; i++)
    {
      led[i].green = green;
    }
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    blue = (uint8_t) atoi(strtokIndx);
    for(int i = 0; i < N_LEDS; i++)
    {
      led[i].blue = blue;
    }
  }
  else if(strtokIndx[0] == 'i') {
    //interval
    Serial.println("Got intermittent data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    config.tOn_I = (uint16_t) atoi(strtokIndx) * 1000;
    strtokIndx = strtok(NULL, ",:;");
    config.tOff_I = (uint16_t) atoi(strtokIndx) * 1000;
  }
  else if(strtokIndx[0] == 'd') {
    if(strtokIndx[1] == 'r') {
      //red duration
      Serial.println("Got enduring red data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOn_R = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOff_R = atoi(strtokIndx);
    }else if(strtokIndx[1] == 'g') {
      //green duration
      Serial.println("Got enduring green data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOn_G = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOff_G = atoi(strtokIndx);
    }else if(strtokIndx[1] == 'b') {
      //blue duration
      Serial.println("Got enduring blue data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOn_B = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOff_B = atoi(strtokIndx);
    }else{
      //duration
      Serial.println("Got enduring data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOn_R, config.tOn_G, config.tOn_B = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOff_R, config.tOff_G, config.tOff_B = (uint16_t) atoi(strtokIndx);
    }
  }
  else if (strtokIndx[0] == 's')
  {
  Serial.println("Saved current configuration to EEPROM!");
  EEPROM.put(0, config);
  }
  else if (strtokIndx[0] == 'l')
  {
  Serial.println("Loaded previous configuration from EEPROM!");
  EEPROM.get(0, config);
  }
  else if(strtokIndx[0] == 'h') {
    Serial.print(R"(Accepted Formats:
	brightness: b:xx - 0 to 31
	color: c:xxx:xxx:xxx - 0 to 255
	duration: d:xx:xx - tOn, tOff in ms
	color duration dr, dg, db:xx:xx -tOn, tOff in ms
	interval: i:xx:xx - tOn, tOff in sec
	save config to EEPROM: s
	load config from EEPROM: l
	help: h
)");
  }
  newData = false;
}

void setup() {
    Serial.begin(9600);
    SPI.begin();
    t_R, t_G, t_B, t_I = millis();
}

void loop() {
  uint8_t mask;
  
  recvData();
  if(newData)
    parseData();
  
  t = millis();
  if(t - t_R >= config.tOn_R)
  {
    mask |= MASK_R;
  }
  if(t - t_R >= config.tOn_R + config.tOff_R)
  {
    mask &= ~MASK_R;
    t_R = millis();
  }
  
  if(t - t_G >= config.tOn_G)
  {
    mask |= MASK_G;
  }
  if(t - t_G >= config.tOn_G + config.tOff_G)
  {
    mask &= ~MASK_G;
    t_G = millis();
  }
  
  if(t - t_B >= config.tOn_B)
  {
    mask |= MASK_B;
  }
  if(t - t_B >= config.tOn_B + config.tOff_B)
  {
    mask &= ~MASK_B;
    t_B = millis();
  }

  if(t - t_I >= config.tOn_I)
  {
    mask |= MASK_A;
  }
  if(t - t_I >= config.tOn_I + config.tOff_I)
  {
    mask &= ~MASK_A;
    t_I = millis();
  }
  
  APA102(led, 24, config.brightness, mask);
  
}
