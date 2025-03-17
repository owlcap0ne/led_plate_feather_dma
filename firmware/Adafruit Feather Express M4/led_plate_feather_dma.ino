#include <SPI.h>
#include <Adafruit_ZeroDMA.h>
#include "utility/dma.h"
#include <FlashStorage.h>

#define MASK_R (1 << 0)  //red
#define MASK_G (1 << 1)  //green
#define MASK_B (1 << 2)  //blue
#define MASK_A (1 << 3)  //all

#define N_LEDS 24
#define N_CONFIGS 6

#define C_MASK_0  (1 << 0)
#define C_MASK_1  (1 << 1)
#define C_MASK_2  (1 << 2)
#define C_MASK_3  (1 << 3)
#define C_MASK_4  (1 << 4)
#define C_MASK_5  (1 << 5)

#define SPI_SPEED 8000000

#define F_UPDATE 2000 // update frequency for LED array, min. 1000 for 1ms resolution
#define T_UPDATE 1000000 / F_UPDATE // interval between updates in us

boolean loadOnBoot = true;

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

// ugly hack for flash storage...
typedef struct {
  CONFIG configs[N_CONFIGS];
} CONFIG_STORE;

CONFIG configs[N_CONFIGS];
CONFIG_STATE configsState[N_CONFIGS];

const byte numChars = 32;
char receivedChars[numChars];
char tmpChars[numChars];
boolean newData = false;

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

// EEPROM emulation for Atmel ARM based controllers
FlashStorage(flash, CONFIG_STORE);

void dma_callback(Adafruit_ZeroDMA *dma) {
  dmaDone = true;
}

void config_setDuration(uint16_t tOn, uint16_t tOff, uint8_t mask)
{
  for(int n = 0; n < N_CONFIGS; n++)
  {
    if(configMask & (1 << n)) // true if config n is settable
    {
      if(mask & MASK_G)
      {
          configs[n].tOn_G = tOn;
          configs[n].tOff_G = tOff;
      }
      else if(mask & MASK_R)
        {
          configs[n].tOn_R = tOn;
          configs[n].tOff_R = tOff;
        }
      else if(mask & MASK_B)
      {
          configs[n].tOn_B = tOn;
          configs[n].tOff_B = tOff;
      }
      else if(mask & MASK_A)
      {
          configs[n].tOn_R = tOn;
          configs[n].tOff_R = tOff;
          configs[n].tOn_G = tOn;
          configs[n].tOff_G = tOff;
          configs[n].tOn_B = tOn;
          configs[n].tOff_B = tOff;
      }
    }
  }
}

void config_setBrightness(uint8_t brightness)
{
  for(int n = 0; n < N_CONFIGS; n++)
  {
    if(configMask & (1 << n)) // true if config n is settable
      configs[n].brightness = brightness;
  }
}

void config_setInterval(uint16_t tOn, uint16_t tOff)
{
  for(int n = 0; n < N_CONFIGS; n++)
  {
    if(configMask & (1 << n)) // true if config n is settable
    {
      configs[n].tOn_I = tOn;
      configs[n].tOff_I = tOff;
    }
  }
}

void config_setColor(uint8_t red, uint8_t green, uint8_t blue)
{
  for(int n = 0; n < N_CONFIGS; n++)
  {
    if(configMask & (1 << n))
    {
      configs[n].red = red;
      configs[n].green = green;
      configs[n].blue = blue;
    }
  }
}

void config_resetTime(boolean interval)
{
  uint16_t t = millis();
  for(int n = 0; n < N_CONFIGS; n++)
  {
    if(configMask & (1 << n))
    {
      configsState[n].t_B = t;
      configsState[n].t_G = t;
      configsState[n].t_R = t;
      if(interval)
        configsState[n].t_I = t;
    }
  }
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

  uint16_t tOn, tOff;
  uint8_t red, green, blue, brightness;
  CONFIG_STORE tmp;
  
  strtokIndx = strtok(tmpChars, ",:;");
  if(strtokIndx[0] == 'b') {
    //brightness
    Serial.println("Got bright data!");
    strtokIndx = strtok(NULL, ",:;");
    brightness = (uint8_t) atoi(strtokIndx);
    if (brightness > 31)
      brightness = 31;
    config_setBrightness(brightness);
    Serial.println(brightness);
  }
  else if(strtokIndx[0] == 'c') {
    //color
    Serial.println("Got colorful data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    red = (uint8_t) atoi(strtokIndx);
    strtokIndx = strtok(NULL, ",:;");

    Serial.println(strtokIndx);
    green = (uint8_t) atoi(strtokIndx);
    strtokIndx = strtok(NULL, ",:;");

    Serial.println(strtokIndx);
    blue = (uint8_t) atoi(strtokIndx);

    config_setColor(red, green, blue);
  }
  else if(strtokIndx[0] == 'i') {
    //interval
    Serial.println("Got intermittent data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);
    tOn = (uint16_t) atoi(strtokIndx) * 1000;
    strtokIndx = strtok(NULL, ",:;");
    tOff = (uint16_t) atoi(strtokIndx) * 1000;
    config_setInterval(tOn, tOff);
    config_resetTime(true);
  }
  else if(strtokIndx[0] == 'z') {
    // zone selection
    Serial.println("Got zoned-out data!");
    strtokIndx = strtok(NULL, ",:;");
    Serial.println(strtokIndx);

    int c = 0;
    configMask = 0;
    while(strtokIndx[c] != '\0') {
      char tmp = (char) strtokIndx[c];

      if(tmp == '1')
        configMask |= C_MASK_0;
      else if(tmp == '2')
        configMask |= C_MASK_1;
      else if(tmp == '3')
        configMask |= C_MASK_2;
      else if(tmp == '4')
        configMask |= C_MASK_3;
      else if(tmp == '5')
        configMask |= C_MASK_4;
      else if(tmp == '6')
        configMask |= C_MASK_5;
      else if(tmp == '*')
        configMask = 0x3F;

      c++;
    }
    Serial.print("New zone mask: ");
    Serial.println(configMask);
  }
  else if(strtokIndx[0] == 'd') {
    if(strtokIndx[1] == 'r') {
      //red duration
      Serial.println("Got enduring red data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff = (uint16_t) atoi(strtokIndx);
      config_setDuration(tOn, tOff, MASK_R);
    }else if(strtokIndx[1] == 'g') {
      //green duration
      Serial.println("Got enduring green data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff = (uint16_t) atoi(strtokIndx);
      config_setDuration(tOn, tOff, MASK_G);
    }else if(strtokIndx[1] == 'b') {
      //blue duration
      Serial.println("Got enduring blue data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff = (uint16_t) atoi(strtokIndx);
      config_setDuration(tOn, tOff, MASK_B);
    }else{
      //duration
      Serial.println("Got enduring data!");
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOn = (uint16_t) atoi(strtokIndx);
      strtokIndx = strtok(NULL, ",:;");
      Serial.println(strtokIndx);
      tOff = (uint16_t) atoi(strtokIndx);
      config_setDuration(tOn, tOff, MASK_A);
    }
    // resync timing - not necessary but prettier
    config_resetTime(false);
  }
  else if (strtokIndx[0] == 's')
  {
    Serial.println("Saved current configuration to Flash!");
    for(int i = 0; i < N_CONFIGS; i++)
      tmp.configs[i] = configs[i];
    flash.write(tmp);
  }
  else if (strtokIndx[0] == 'l')
  {
    Serial.println("Loaded previous configuration from Flash!");
    tmp = flash.read();
    for(int i = 0; i < N_CONFIGS; i++)
      configs[i] = tmp.configs[i];
  }
  else if(strtokIndx[0] == 'h') {
    Serial.print(R"(Accepted Formats:
	brightness: b:xx - 0 to 31, ex: b:2
	color: c:xxx:xxx:xxx - 0 to 255, ex: c:255:0:50
	duration: d:xx:xx - tOn, tOff in ms, ex: d:100:100
	color duration dr, dg, db:xx:xx -tOn, tOff in ms, ex: dr:1:0 (const. red)
	interval: i:xx:xx - tOn, tOff in sec, ex: i:15:15
	save config to Flash: s
	load config from Flash: l
  select zones (columns): z:xxx - columns 1-6 or * for all, ex: z:024, z:*
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

void writeCol(LED_BUFFER *buffer, CONFIG *config, CONFIG_STATE *configState, uint8_t config_index)
{
  // clamping to avoid memory corruption
  if(config_index > 5)
    config_index = 5;
  
  uint8_t red, green, blue, brightness, mask;

  brightness = config->brightness;
  mask = configState->mask;

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
  
  buffer->leds[ 0 + config_index].intensity = 0xE0 + brightness;
  buffer->leds[ 0 + config_index].red = red;
  buffer->leds[ 0 + config_index].green = green;
  buffer->leds[ 0 + config_index].blue = blue;

  buffer->leds[11 - config_index].intensity = 0xE0 + brightness;
  buffer->leds[11 - config_index].red = red;
  buffer->leds[11 - config_index].green = green;
  buffer->leds[11 - config_index].blue = blue;

  buffer->leds[12 + config_index].intensity = 0xE0 + brightness;
  buffer->leds[12 + config_index].red = red;
  buffer->leds[12 + config_index].green = green;
  buffer->leds[12 + config_index].blue = blue;

  buffer->leds[23 - config_index].intensity = 0xE0 + brightness;
  buffer->leds[23 - config_index].red = red;
  buffer->leds[23 - config_index].green = green;
  buffer->leds[23 - config_index].blue = blue;
}

void checkConfig(CONFIG *config, CONFIG_STATE *configState)
{
  unsigned long t = millis();
  if((t - configState->t_R >= config->tOn_R) && (config->tOff_R > 0))
  {
    configState->mask |= MASK_R;
  }
  if((t - configState->t_R >= config->tOn_R + config->tOff_R) && (config->tOn_R > 0))
  {
    configState->mask &= ~MASK_R;
    configState->t_R = t;
  }
  
  if((t - configState->t_G >= config->tOn_G) && (config->tOff_G > 0))
  {
    configState->mask |= MASK_G;
  }
  if((t - configState->t_G >= config->tOn_G + config->tOff_G) && (config->tOn_G > 0))
  {
    configState->mask &= ~MASK_G;
    configState->t_G = t;
  }
  
  if((t - configState->t_B >= config->tOn_B) && (config->tOff_B > 0))
  {
    configState->mask |= MASK_B;
  }
  if((t - configState->t_B >= config->tOn_B + config->tOff_B) && (config->tOn_B > 0))
  {
    configState->mask &= ~MASK_B;
    configState->t_B = t;
  }

  if(t - configState->t_I >= config->tOn_I)
  {
    configState->mask |= MASK_A;
  }
  if(t - configState->t_I >= config->tOn_I + config->tOff_I)
  {
    configState->mask &= ~MASK_A;
    configState->t_I = t;
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

    // zero everything
    initFrame(&frameBuffer[0]);
    initFrame(&frameBuffer[1]);
    //frameDone = true;
    pinMode(LED_BUILTIN, OUTPUT);

    // send zero'ed buffer once to quickly shut off all LEDS
    SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
    dmaStat = dma.startJob(); // go!
    while(!dmaDone);  // just wait

    // load the last config from NVM
    // !!! CONTAINS TRASH AFTER PROGRAMMING THE CONTROLLER - KEEP AWAY FROM CELLS UNTIL CONFIGURED PROPERLY !!!
    if(loadOnBoot)
    {
      CONFIG_STORE tmp_configs = flash.read();
      for(int i = 0; i < N_CONFIGS; i++)
        configs[i] = tmp_configs.configs[i];
    } else {
      for(int n = 0; n < N_CONFIGS; n++)
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
    
    // reset all timings including interval
    config_resetTime(true);
    
}

unsigned long t_current = 0,
              t_previous;

void loop() {
  t_current = micros();
  
  // get new data from Serial and process it
  recvData();
  if(newData)
    parseData();
  
  // check the timings
  for(int c = 0; c < N_CONFIGS; c++)
  {
   checkConfig(&configs[c], &configsState[c]);
  }
  
  // write the frame buffer (without checking if that's necessary at the moment)
  // TODO: only write on changes?
  for(int c = 0; c < N_CONFIGS; c++)
  {
   writeCol(&frameBuffer[buffer_free], &configs[c], &configsState[c], c);
  }
  //frameDone = true;

  // as soon as the DMA engine is ready transmit the next buffer
  if(dmaDone && (t_current - t_previous >= T_UPDATE)){
    t_previous = t_current;

    //digitalWrite(LED_BUILTIN, 1);

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

    //digitalWrite(LED_BUILTIN, 0);
  }

  //digitalWrite(LED_BUILTIN, buffer_free);
  
}
