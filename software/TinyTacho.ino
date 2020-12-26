// tinyTacho - RPM-Meter using ATtiny13A and I²C OLED
//
// This code implements a simple tachometer (RPM counter). An IR photo
// diode is connected to the positive input of ATtiny's internal
// analog comparator, a variable resistor for calibration is connected to
// the negative input. An interrupt is triggered on every falling edge of
// the comparator output which saves the current value of timer/counter 0
// and restarts the timer. The 8-bit timer is expanded to a 16-bit one by
// using the timer overflow interrupt. The saved timer/counter value
// contains the timer counts per revolution. The RPM is calculated by
// utilizing the following equation:
//
// RPM = 60 * F_CPU / prescaler / counter
//     = 60 * 1200000 / 64 / counter
//     = 1125000 / counter
//
// The calculated RPM value is displayed on an I²C OLED display.
// The I²C protocol implementation is based on a crude bitbanging method.
// It was specifically designed for the limited resources of ATtiny10 and
// ATtiny13, but should work with some other AVRs as well.
// The functions for the OLED are adapted to the SSD1306 128x32 OLED module,
// but they can easily be modified to be used for other modules. In order to
// save resources, only the basic functionalities which are needed for this
// application are implemented.
// For a detailed information on the working principle of the I²C OLED
// implementation visit https://github.com/wagiminator/attiny13-tinyoleddemo
//
//
//    +-----------------------------+
// ---|SDA +--------------------+   |
// ---|SCL |    SSD1306 OLED    |   |
// ---|VCC |       128x36       |   |
// ---|GND +--------------------+   |
//    +-----------------------------+
//
//                            +-\/-+
//          --- A0 (D5) PB5  1|°   |8  Vcc
// SCL OLED --- A3 (D3) PB3  2|    |7  PB2 (D2) A1 ---
// SDA OLED --- A2 (D4) PB4  3|    |6  PB1 (D1) AC1 -- Calib Poti
//                      GND  4|    |5  PB0 (D0) AC0 -- IR Photo Diode
//                            +----+  
//
// Controller: ATtiny13
// Core:       MicroCore (https://github.com/MCUdude/MicroCore)
// Clockspeed: 1.2 MHz internal
// BOD:        BOD 2.7V
// Timing:     Micros disabled (Timer0 is in use)
//
// This project was inspired by Great Scott's DIY tachometer:
// https://www.instructables.com/DIY-Tachometer-RPM-Meter/
//
// 2020 by Stefan Wagner 
// Project Files (EasyEDA): https://easyeda.com/wagiminator
// Project Files (Github):  https://github.com/wagiminator
// License: http://creativecommons.org/licenses/by-sa/3.0/


// libraries
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

// pin definitions
#define I2C_SCL         PB3                   // I2C serial clock pin
#define I2C_SDA         PB4                   // I2C serial data pin

// -----------------------------------------------------------------------------
// I2C Implementation
// -----------------------------------------------------------------------------

// I2C macros
#define I2C_SDA_HIGH()  DDRB &= ~(1<<I2C_SDA) // release SDA   -> pulled HIGH by resistor
#define I2C_SDA_LOW()   DDRB |=  (1<<I2C_SDA) // SDA as output -> pulled LOW  by MCU
#define I2C_SCL_HIGH()  DDRB &= ~(1<<I2C_SCL) // release SCL   -> pulled HIGH by resistor
#define I2C_SCL_LOW()   DDRB |=  (1<<I2C_SCL) // SCL as output -> pulled LOW  by MCU

// I2C init function
void I2C_init(void) {
  DDRB  &= ~((1<<I2C_SDA)|(1<<I2C_SCL));  // pins as input (HIGH-Z) -> lines released
  PORTB &= ~((1<<I2C_SDA)|(1<<I2C_SCL));  // should be LOW when as ouput
}

// I2C transmit one data byte to the slave, ignore ACK bit, no clock stretching allowed
void I2C_write(uint8_t data) {
  for(uint8_t i = 8; i; i--, data<<=1) {  // transmit 8 bits, MSB first
    I2C_SDA_LOW();                        // SDA LOW for now (saves some flash this way)
    if (data & 0x80) I2C_SDA_HIGH();      // SDA HIGH if bit is 1
    I2C_SCL_HIGH();                       // clock HIGH -> slave reads the bit
    I2C_SCL_LOW();                        // clock LOW again
  }
  I2C_SDA_HIGH();                         // release SDA for ACK bit of slave
  I2C_SCL_HIGH();                         // 9th clock pulse is for the ACK bit
  I2C_SCL_LOW();                          // but ACK bit is ignored
}

// I2C start transmission
void I2C_start(uint8_t addr) {
  I2C_SDA_LOW();                          // start condition: SDA goes LOW first
  I2C_SCL_LOW();                          // start condition: SCL goes LOW second
  I2C_write(addr);                        // send slave address
}

// I2C stop transmission
void I2C_stop(void) {
  I2C_SDA_LOW();                          // prepare SDA for LOW to HIGH transition
  I2C_SCL_HIGH();                         // stop condition: SCL goes HIGH first
  I2C_SDA_HIGH();                         // stop condition: SDA goes HIGH second
}

// -----------------------------------------------------------------------------
// OLED Implementation
// -----------------------------------------------------------------------------

// OLED definitions
#define OLED_ADDR       0x78              // OLED write address
#define OLED_CMD_MODE   0x00              // set command mode
#define OLED_DAT_MODE   0x40              // set data mode
#define OLED_INIT_LEN   15                // length of OLED init command array

// OLED init settings
const uint8_t OLED_INIT_CMD[] PROGMEM = {
  0xA8, 0x1F,       // set multiplex for 128x32
  0x22, 0x00, 0x03, // set min and max page
  0x20, 0x01,       // set vertical memory addressing mode
  0xDA, 0x02,       // set COM pins hardware configuration to sequential
  0x8D, 0x14,       // enable charge pump
  0xAF,             // switch on OLED
  0x00, 0x10, 0xB0  // set cursor at home position
};

// OLED simple reduced 3x8 font
const uint8_t OLED_FONT[] PROGMEM = {
  0x7F, 0x41, 0x7F, // 0  0
  0x00, 0x00, 0x7F, // 1  1
  0x79, 0x49, 0x4F, // 2  2
  0x41, 0x49, 0x7F, // 3  3
  0x0F, 0x08, 0x7E, // 4  4
  0x4F, 0x49, 0x79, // 5  5
  0x7F, 0x49, 0x79, // 6  6
  0x03, 0x01, 0x7F, // 7  7
  0x7F, 0x49, 0x7F, // 8  8
  0x4F, 0x49, 0x7F, // 9  9
  0x7F, 0x40, 0x60, // L 10
  0x7F, 0x20, 0x7F, // W 11
  0x00, 0x00, 0x00  //   12
};

// OLED global variables
uint8_t  buffer[8] =  {12, 0, 0, 0, 0, 0, 12, 12};    // screen buffer
uint8_t  slow[8] =    {12, 12, 5, 10, 0, 11, 12, 12}; // "SLOW"
uint16_t divider[5] = {10000, 1000, 100, 10, 1};      // for BCD conversion

// OLED init function
void OLED_init(void) {
  I2C_init();                             // initialize I2C first
  I2C_start(OLED_ADDR);                   // start transmission to OLED
  I2C_write(OLED_CMD_MODE);               // set command mode
  for (uint8_t i = 0; i < OLED_INIT_LEN; i++) I2C_write(pgm_read_byte(&OLED_INIT_CMD[i])); // send the command bytes
  I2C_stop();                             // stop transmission
}

// OLED stretch a part of a byte
uint8_t OLED_stretch(uint8_t b) {
  b  = ((b & 2) << 3) | (b & 1);          // split 2 LSB into the nibbles
  b |= b << 1;                            // double the bits
  b |= b << 2;                            // double them again = 4 times
  return b;                               // return the value
}

// OLED print a big digit by stretching the character
void OLED_printD(uint8_t ch) {
  uint8_t i, j, k, b;                     // loop variables
  uint8_t sb[4];                          // stretched character bytes
  ch += ch << 1;                          // calculate position of character in font array
  for(i=8; i; i--) I2C_write(0x00);       // print spacing between characters
  for(i=3; i; i--) {                      // font has 3 bytes per character
    b = pgm_read_byte(&OLED_FONT[ch++]);  // read character byte
    for(j=0; j<4; j++, b >>= 2) sb[j] = OLED_stretch(b);  // stretch 4 times
    j=4; if(i==2) j=6;                    // calculate x-stretch value
    while(j--) {                          // write several times (x-direction)
      for(k=0; k<4; k++) I2C_write(sb[k]);// the 4 stretched bytes (y-direction)
    }
  } 
}

// OLED print buffer
void OLED_printB(uint8_t *buffer) {
  I2C_start(OLED_ADDR);                   // start transmission to OLED
  I2C_write(OLED_DAT_MODE);               // set data mode
  for(uint8_t i=0; i<8; i++) OLED_printD(buffer[i]);  // print buffer
  I2C_stop();                             // stop transmission
}

// OLED print 16 bit value (BCD conversion by substraction method)
void OLED_printW(uint16_t value) {
  for(uint8_t digit = 0; digit < 5; digit++) {      // 5 digits
    uint8_t digitval = 0;                 // start with digit value 0
    while (value >= divider[digit]) {     // if current divider fits into the value
      digitval++;                         // increase digit value
      value -= divider[digit];            // decrease value by divider
    }
    buffer[digit + 1] = digitval;         // set the digit in the screen buffer
  }
  OLED_printB(buffer);                    // print screen buffer on the OLED
}

// -----------------------------------------------------------------------------
// Main Function
// -----------------------------------------------------------------------------

// global variables
volatile uint8_t  counter_enable    = 1;  // enable update of counter result
volatile uint8_t  counter_highbyte  = 0;  // high byte of 16-bit counter
volatile uint16_t counter_result    = 0;  // counter result (timer counts per revolution)

// main function
int main(void) {
  // local variables
  uint16_t counter_value;                 // timer counts per revolution
  uint16_t rpm;                           // revolutions per minute

  // setup
  PRR    = (1<<PRADC);                    // shut down ADC to save power
  DIDR0  = (1<<AIN1D) | (1<<AIN0D);       // disable digital input buffer on AC pins
  ACSR   = (1<<ACIE) | (1<<ACIS1);        // enable analog comparator interrupt on falling edge
  TIMSK0 = (1<<TOIE0);                    // enable timer overflow interrupt
  sei();                                  // enable all interrupts
  OLED_init();                            // initialize the OLED
  
  // loop
  while(1) {                              // loop until forever                         
    counter_enable = 0;                   // lock counter result
    counter_value = counter_result;       // get counter result
    counter_enable = 1;                   // unlock counter result
    if (counter_value > 17) {             // if counter value is valid:
      rpm = (uint32_t)1125000 / counter_value; // calculate RPM value      
      OLED_printW(rpm);                   // print RPM value on the OLED
    } else OLED_printB(slow);             // else print "SLOW" on the OLED
  }
}

// analog comparator interrupt service routine
ISR(ANA_COMP_vect) {
  if(counter_enable) counter_result = (uint16_t)(counter_highbyte << 8) | TCNT0; // save result if enabled
  TCNT0 = 0;                              // reset counter
  counter_highbyte = 0;                   // reset highbyte
  TCCR0B  = (1<<CS01) | (1<<CS00);        // start timer with prescaler 64 (in case it was stopped)
}

// timer overflow interrupt service routine
ISR(TIM0_OVF_vect) {
  counter_highbyte++;                     // increase highbyte (virtual 16-bit counter)
  if(!counter_highbyte) {                 // if 16-bit counter overflows
    TCCR0B = 0;                           // stop the timer
    if(counter_enable) counter_result = 0;// result is invalid
  }
}
