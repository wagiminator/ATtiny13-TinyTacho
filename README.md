# TinyTacho - Simple RPM-Meter based on ATtiny13A

Recently Great Scott built his DIY version of a tachometer which I thought was very cool (https://youtu.be/6QZMt4yyylU). But using an ATmega for this task, I found a bit overpowered. So I tried to force all tasks (measurement, calculation, I²C protocol and OLED display) into the huge 1KByte memory of an ATtiny13.

- Project Video: https://youtu.be/Iz7LjheLYKo

![pic1.jpg](https://github.com/wagiminator/ATtiny13-TinyTacho/blob/main/documentation/TinyTacho_pic1.jpg)
![pic2.jpg](https://github.com/wagiminator/ATtiny13-TinyTacho/blob/main/documentation/TinyTacho_pic2.jpg)

# Hardware
Since the ATtiny13 does almost all of the tasks, the wiring is pretty simple:

![wiring.png](https://github.com/wagiminator/ATtiny13-TinyTacho/blob/main/documentation/TinyTacho_Wiring.png)

The IR LED emits light, which is reflected by the rotating object and detected by the IR phototransistor. The phototransistor changes its conductivity depending on the strength of the reflected light. If the rotating object has exactly one white stripe on an otherwise black surface, then the phototransistor changes its electrical resistance twice per revolution: it rises once above and falls once below a certain threshold, which is defined by the variable resistor.

# Software
The IR photo transistor is connected to the positive input of ATtiny's internal analog comparator, the variable resistor for calibration is connected to the negative input. An interrupt is triggered on every falling edge of the comparator output which saves the current value of timer0 and restarts the timer. The 8-bit timer is expanded to a 16-bit one by using the timer overflow interrupt. The saved timer value contains the timer counts per revolution. The RPM is calculated by utilizing the following equation:
```
RPM = 60 * F_CPU / prescaler / counter
    = 60 * 1200000 / 64 / counter
    = 1125000 / counter
```
The calculated RPM value is displayed on an I²C OLED display. The I²C protocol implementation is based on a crude bitbanging method. It was specifically designed for the limited resources of ATtiny10 and ATtiny13, but should work with some other AVRs as well. The functions for the OLED are adapted to the SSD1306 128x32 OLED module, but they can easily be modified to be used for other modules. In order to save resources, only the basic functionalities which are needed for this application are implemented.

For a detailed information on the working principle of the I²C OLED implementation visit https://github.com/wagiminator/attiny13-tinyoleddemo

```c
// global variables
volatile uint8_t  counter_enable    = 1;  // enable update of counter result
volatile uint8_t  counter_highbyte  = 0;  // high byte of 16-bit counter
volatile uint16_t counter_result    = 0;  // counter result (timer counts per revolution)

// main function
int main(void) {
  uint16_t counter_value;                 // timer counts per revolution
  uint16_t rpm;                           // revolutions per minute
  PRR    = (1<<PRADC);                    // shut down ADC to save power
  DIDR0  = (1<<AIN1D) | (1<<AIN0D);       // disable digital input buffer on AC pins
  ACSR   = (1<<ACIE) | (1<<ACIS1);        // enable analog comparator interrupt on falling edge
  TIMSK0 = (1<<TOIE0);                    // enable timer overflow interrupt
  sei();                                  // enable all interrupts
  OLED_init();                            // initialize the OLED
  
  // main loop
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
```

# References, Links and Notes
1. Great Scott's Tachometer: https://youtu.be/6QZMt4yyylU
2. ATtiny13 I²C OLED Tutorial: https://github.com/wagiminator/attiny13-tinyoleddemo
3. SSD1306 Datasheet: https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf
4. ATtiny13A Datasheet: http://ww1.microchip.com/downloads/en/DeviceDoc/doc8126.pdf
