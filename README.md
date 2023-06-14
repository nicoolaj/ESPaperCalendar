# ESPaperCalendar
 iCal driven ESP32 + ePaper calendar

# Used hardware
- Wemos S2 mini
- Waveshare 7.5 inch 3 color e-Paper with universal hat (https://www.waveshare.com/e-paper-driver-hat.htm)
# Configuration
- Copy Example.Credentials.h to Credentials.h
- Open the Credentials.h file and fill in your wifi and calendar information.
- Verify the correct pin configuration in the ino file.

## Pin layout for Wemos S2 mini
I used a Wemos/Lolin S2 mini with the pin configuration as shown below. If you make the connections as shown below the e-paper should work.

|Color|HAT|S2|
|-|-|-|
|Gray|VCC|3V3|
|Brown|GND|GND|
|Blue|DIN|11|
|Yellow|CLK|7|
|Orange|CS|4|
|Green|DC|17|
|White|RST|16|
|Purple|BUSY|4|


# Todo
- Recurring events are not implemented yet