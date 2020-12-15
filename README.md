# led_plate_feather_dma
## Installation
To successfuly compile the code a few additional components are needed:

### Adafruit SAMD boards
add
`https://adafruit.github.io/arduino-board-index/package_adafruit_index.json`
under Preferences -> Additional Board Manager URLs

Install the new Boards from the Board Manager
### Adafruit & Arduino SAMD Libraries
Install both the Arduino and the Adafruit SAMD Core Libraries from the Library Manager
### DMA and FlashStorage
Install the Adafruit ZeroDMA Library and the FlashStorage Library

## Usage
Connect the _Clk_-line to the _SCK_ Pin on the Feather

Connect the _Dat_-line to the _MO_ Pin on the Feather

Sending _h_ will return a help string with all accepted commands and formats
