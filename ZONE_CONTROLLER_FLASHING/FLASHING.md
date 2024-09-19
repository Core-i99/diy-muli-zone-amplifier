Arduino IDE (old): 1.8.19

ATTinyCore:
    - https://github.com/SpenceKonde/ATTinyCore 
    - Version 1.5.2
    - Add ATTinyCore URL to board manager in Arduino IDE

Flash the Arduino Uno as ISP flasher:
    - Open Arduino IDE
    - File > Examples > ArduinoISP > ArduinoISP
    - Flash this program to the Arduino Uno

Arduino IDE settings:
    - Tools > Board > "ATtiny24/44/84(a) (No bootloader)"
    - Tools > Chip > "ATtiny44(a)"
    - Tools > Clock Source > "8MHz (internal)"

Flashing the ATtiny:
    - Connect Arduino Uno to the ATtiny44, see PINOUT.png
    - First burn the bootloader: Tools > Burn Bootloader
    - Flash the program

