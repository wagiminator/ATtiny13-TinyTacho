# TinyTacho - Simple RPM-Meter based on ATtiny13A

Recently Great Scott built his DIY version of a tachometer which I thought was very cool (https://youtu.be/6QZMt4yyylU). But using an ATmega for this task, I found a bit overpowered. So I tried to force all tasks (measurement, calculation, I²C protocol and OLED display) into the huge 1KByte memory of an ATtiny13.

![pic1.jpg](https://github.com/wagiminator/ATtiny13-TinyTacho/blob/main/documentation/TinyTacho_pic1.jpg)
![pic2.jpg](https://github.com/wagiminator/ATtiny13-TinyTacho/blob/main/documentation/TinyTacho_pic2.jpg)

# Hardware
Since the ATtiny13 does almost all of the tasks, the wiring is pretty simple:

![wiring.png](https://github.com/wagiminator/ATtiny13-TinyTacho/blob/main/documentation/TinyTacho_Wiring.png)

# Softare
An IR photo transistor is connected to the positive input of ATtiny's internal analog comparator, a variable resistor for calibration is connected to the negative input. An interrupt is triggered on every falling edge of the comparator output which saves the current value of timer0 and restarts the timer. The 8-bit timer is expanded to a 16-bit one by using the timer overflow interrupt. The saved timer value contains the timer counts per revolution. The RPM is calculated by utilizing the following equation:
```
RPM = 60 * F_CPU / prescaler / counter
     = 60 * 1200000 / 64 / counter
     = 1125000 / counter
```
The calculated RPM value is displayed on an I²C OLED display. The I²C protocol implementation is based on a crude bitbanging method. It was specifically designed for the limited resources of ATtiny10 and ATtiny13, but should work with some other AVRs as well. The functions for the OLED are adapted to the SSD1306 128x32 OLED module, but they can easily be modified to be used for other modules. In order to save resources, only the basic functionalities which are needed for this application are implemented.

For a detailed information on the working principle of the I²C OLED implementation visit https://github.com/wagiminator/attiny13-tinyoleddemo
