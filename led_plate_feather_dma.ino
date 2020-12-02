#include <SPI.h>
#include <Adafruit_ZeroDMA.h>
#include "utility/dma.h"
#include <FlashStorage.h>

#define MASK_R 0x1  //red
#define MASK_G 0x2  //green
#define MASK_B 0x4  //blue
#define MASK_A 0x8  //all

#define N_LEDS 24

#define C_MASK_0  (1 << 0)
#define C_MASK_1  (1 << 1)
#define C_MASK_2  (1 << 2)
#define C_MASK_3  (1 << 3)
#define C_MASK_4  (1 << 4)
#define C_MASK_5  (1 << 5)

#define SPI_SPEED 8000000

typedef struct {
  union {
    struct {
      union {
        uint8_t i;
        uint8_t intensity;
      };
      union {
        uint8_t b;
        uint8_t blue;
      };
      union {
        uint8_t g;
        uint8_t green;
      };
      union {
        uint8_t r;
        uint8_t red;
      };
    };
    uint8_t raw[4];
  };
} LED;

typedef struct {
  uint8_t start[4]; // start frame, 4x 0x00
  LED leds[N_LEDS];
  uint8_t end[N_LEDS / 16]; // end frame all 0's again
} LED_BUFFER;

// holds the last switching time and color mask for a config
typedef struct {
  union {
    struct {
      unsigned long t_R,
                    t_G,
                    t_B,
                    t_I;
    };
    unsigned long t_X[4];
  };
  uint8_t mask;
} CONFIG_STATE;

typedef struct {
  uint8_t   brightness,
            red,
            green,
            blue;
  uint16_t  tOn_R,
            tOff_R,
            tOn_G,
            tOff_G,
            tOn_B,
            tOff_B,
            tOn_I,
            tOff_I;
} CONFIG;

void config_setDuration(uint16_t tOn, uint16_t tOff, enum color{red = 0, green, blue, all})
{
  for(int n = 0; n < 6; n++)
  {
    if(configMask & (1 << n)) // true if config n is settable
    {
      switch(color)
      {
        case red:
          configs[n].tOn_R = tOn;
          configs[n].tOff_R = tOff;
        break;

        case green:
          configs[n].tOn_G = tOn;
          configs[n].tOff_G = tOff;
        break;

        case blue:
          configs[n].tOn_B = tOn;
          configs[n].tOff_B = tOff;
        break;

        case all:
          configs[n].tOn_R = tOn;
          configs[n].tOff_R = tOff;
          configs[n].tOn_G = tOn;
          configs[n].tOff_G = tOff;
          configs[n].tOn_B = tOn;
          configs[n].tOff_B = tOff;
        break;
      }
    }
  }
}

CONFIG configs[6];
CONFIG_STATE configsState[6];

/*
CONFIG config = {
  .brightness = 31,
  .red     = 0,
  .green   = 0,
  .blue    = 0,
  .tOn_R   = 1,
  .tOff_R  = 0,
  .tOn_G   = 0,
  .tOff_G  = 0,
  .tOn_B   = 0,
  .tOff_B  = 0,
  .tOn_I   = 1,
  .tOff_I  = 0
};
*/

const byte numChars = 32;
char receivedChars[numChars];
char tmpChars[numChars];
boolean newData = false;
boolean loadOnBoot = true;

// bitwise mask to select configs for setters
int configMask = C_MASK_0 | C_MASK_1 | C_MASK_2 | C_MASK_3 | C_MASK_4 | C_MASK_5; // default: select all

LED_BUFFER frameBuffer[2];  // double buffering - write one while transmitting the other
uint8_t buffer_free = 0;  // the one currently not in use by the DMA peripheral

Adafruit_ZeroDMA dma;
ZeroDMAstatus dmaStat; // status code, mostly ignored
DmacDescriptor *dmaDesc; // saves the address, so that it can be changed
volatile bool dmaDone = false;
bool frameDone = false;
#define DMA_LENGTH 4 + (N_LEDS * 4) + (N_LEDS / 16)

unsigned long t, t_R, t_G, t_B, t_I;

// EEPROM emulation for Atmel ARM based controllers
FlashStorage(flash, CONFIG);

void dma_callback(Adafruit_ZeroDMA *dma) {
  dmaDone = true;
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
    config.red = (uint8_t) atoi(strtokIndx);
    strtokIndx = strtok(NULL, ",:;");

    Serial.println(strtokIndx);
    config.green = (uint8_t) atoi(strtokIndx);
    strtokIndx = strtok(NULL, ",:;");

    Serial.println(strtokIndx);
    config.blue = (uint8_t) atoi(strtokIndx);
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
      config.tOff_R = (uint16_t) atoi(strtokIndx);
    }else if(strtokIndx[1] == 'g') {
      //green duration
      Serial.println("Got enduring green data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOn_G = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOff_G = (uint16_t) atoi(strtokIndx);
    }else if(strtokIndx[1] == 'b') {
      //blue duration
      Serial.println("Got enduring blue data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOn_B = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      config.tOff_B = (uint16_t) atoi(strtokIndx);
    }else{
      //duration
      Serial.println("Got enduring data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      uint16_t duration = (uint16_t) atoi(strtokIndx);
      config.tOn_R = duration;
      config.tOn_G = duration;
      config.tOn_B = duration;
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      duration = (uint16_t) atoi(strtokIndx);
      config.tOff_R = duration;
      config.tOff_G = duration;
      config.tOff_B = duration;
    }
  }
  else if (strtokIndx[0] == 's')
  {
  Serial.println("Saved current configuration to EEPROM!");
    flash.write(configs);
  }
  else if (strtokIndx[0] == 'l')
  {
  Serial.println("Loaded previous configuration from EEPROM!");
    configs = flash.read();
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

void initFrame(LED_BUFFER *buffer) {
  for(int i = 0; i < 4; i++)
  {
    buffer->start[i] = 0;
  }
  for(int i = 0; i < N_LEDS; i++)
  {
    buffer->leds[i].intensity = 0xE0;
    buffer->leds[i].red = 0;
    buffer->leds[i].green = 0;
    buffer->leds[i].blue = 0;
  }
  for(int i = 0; i < N_LEDS / 16; i++)
  {
    buffer->end[i] = 0;
  }
  
}

void writeFrame(LED_BUFFER *buffer, CONFIG *config, uint8_t mask) {
  uint8_t red, green, blue, brightness;

  brightness = config->brightness;

  if(mask & MASK_R)
    red = 0;
  else
    red = config->red;

  if(mask & (MASK_G | MASK_A))
    green = 0;
  else
    green = config->green;
  
  if(mask & (MASK_B | MASK_A))
    blue = 0;
  else
    blue = config->blue;
  
  for(int n = 0; n < N_LEDS; n++)
  {
    buffer->leds[n].intensity = 0xE0 + config->brightness;
    buffer->leds[n].red = red;
    buffer->leds[n].green = green;
    buffer->leds[n].blue = blue;
  }
}

void writeCol(LED_BUFFER *buffer, CONFIG *config, uint8_t config_index)
{
  // clamping to avoid memory corruption
  if(config_index > 5)
    config_index = 5;
  
  uint8_t red, green, blue, brightness, mask;

  brightness = config->brightness;
  mask = config->mask;

  if(mask & MASK_R)
    red = 0;
  else
    red = config->red;

  if(mask & (MASK_G | MASK_A))
    green = 0;
  else
    green = config->green;
  
  if(mask & (MASK_B | MASK_A))
    blue = 0;
  else
    blue = config->blue;
  
  buffer->leds[ 0 + config_index].raw = {0xE0 + brightness, blue, green, red};
  buffer->leds[11 - config_index].raw = {0xE0 + brightness, blue, green, red};
  buffer->leds[12 + config_index].raw = {0xE0 + brightness, blue, green, red};
  buffer->leds[23 - config_index].raw = {0xE0 + brightness, blue, green, red};
}

void checkConfig(CONFIG *config, CONFIG_STATE *configState)
{
  unsigned long t = millis();
  if((t - configState->t_R >= config->tOn_R) && (config->tOff_R > 0))
  {
    config->mask |= MASK_R;
  }
  if((t - configState->t_R >= config->tOn_R + config->tOff_R) && (config->tOn_R > 0))
  {
    config->mask &= ~MASK_R;
    configState->t_R = t
  }
  
  if((t - configState->t_G >= config->tOn_G) && (config->tOff_G > 0))
  {
    config->mask |= MASK_G;
  }
  if((t - t_G >= config->tOn_G + config->tOff_G) && (config->tOn_G > 0))
  {
    config->mask &= ~MASK_G;
    configState->t_G = t
  }
  
  if((t - configState->t_B >= config->tOn_B) && (config->tOff_B > 0))
  {
    config->mask |= MASK_B;
  }
  if((t - t_B >= config->tOn_B + config->tOff_B) && (config->tOn_B > 0))
  {
    config->mask &= ~MASK_B;
    configState->t_B = t
  }

  if(t - configState->t_I >= config->tOn_I)
  {
    config->mask |= MASK_A;
  }
  if(t - t_I >= config->tOn_I + config->tOff_I)
  {
    config->mask &= ~MASK_A;
    configState->t_I = t
  }
}

void setup() {
    Serial.begin(9600);
    SPI.begin();

    // Feather M4's native SPI uses SERCOM1
    dma.setTrigger(SERCOM1_DMAC_ID_TX);
    dma.setAction(DMA_TRIGGER_ACTON_BEAT);
    dmaStat = dma.allocate();
    dmaDesc = dma.addDescriptor(
      &frameBuffer[buffer_free],  // source
      (void*)(&SERCOM1->SPI.DATA.reg),  // destination
      DMA_LENGTH, // how many total
      DMA_BEAT_SIZE_BYTE, // bytes/hwords/words
      true, // inc source addr?
      false // inc dest addr?
    );
    dma.setCallback(dma_callback);

    initFrame(&frameBuffer[0]);
    initFrame(&frameBuffer[1]);
    frameDone = true;
    pinMode(LED_BUILTIN, OUTPUT);

    // send zero'ed buffer once to quickly shut off all LEDS
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    dmaStat = dma.startJob(); // go!
    while(!dmaDone);  // just wait

    if(loadOnBoot)
    {
      configs = flash.read();
    } else {
      for(int n = 0; n < 6; n++)
      {
        configs[n] = {
          .brightness = 31,
          .red     = 0,
          .green   = 0,
          .blue    = 0,
          .tOn_R   = 1,
          .tOff_R  = 0,
          .tOn_G   = 0,
          .tOff_G  = 0,
          .tOn_B   = 0,
          .tOff_B  = 0,
          .tOn_I   = 1,
          .tOff_I  = 0
        };
      }
    }
    t = millis();
    for(int n = 0; n < 6; n++)
    {
      for(int m = 0; m < 4; m++)
        configsState[n].t_X[m] = t;
    }
}

void loop() {
  uint8_t mask, mask_old;
  
  recvData();
  if(newData)
    parseData();
  
  for(int c = 0; c < 6; c++)
  {
   checkConfig(&configs[c], &configsState[c]);
  }
  
  frameDone = (mask == mask_old) ? true : false;
  mask_old = mask;
  //if(!frameDone)
  //{
    //writeFrame(&frameBuffer[buffer_free], &config, mask);
    for(int c = 0; c < 6; c++)
    {
   writeCol(&frameBuffer[buffer_free], &configs[c], c);
    }
    frameDone = true;
  //}

  if(dmaDone){
    SPI.endTransaction();
    dma.changeDescriptor(
      dmaDesc,
      &frameBuffer[buffer_free]
    );
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    dmaStat = dma.startJob(); // go!
    dmaDone = false;
    // swap buffers
    buffer_free = 1 - buffer_free;
  }
  digitalWrite(LED_BUILTIN, buffer_free);
  
}
