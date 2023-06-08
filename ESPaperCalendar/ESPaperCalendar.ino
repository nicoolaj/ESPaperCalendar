/**
 * Rename Example.Credentials.h to Credentials.h and update with your wifi and calendar information
 **/
#include "Credentials.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <time.h>

#include <HTTPClient.h>
#include <GxEPD2_BW.h> // Include the GxEPD2 library
#include <GxEPD2_3C.h> // Include the GxEPD2 color extension
#include <Wire.h>
#include <SPI.h>

#include <GxEPD2_EPD.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

// Display pins, change to match your coonfig/board if required
#define RST_PIN 16
#define DC_PIN 17
#define CS_PIN 5
#define BUSY_PIN 4

WiFiUDP ntpUDP;

// NTP Server configuration
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

// Define TimeChangeRule objects for DST start and end
TimeChangeRule myDSTStart = {"CEST", Last, Sun, Mar, 2, 120};  // Central European Summer Time (UTC +2), last Sunday in March at 2:00
TimeChangeRule myDSTEnd = {"CET", Last, Sun, Oct, 2, 60};    // Central European Time (UTC +1), last Sunday in October at 2:00

// Create a Timezone object to handle time zone and DST settings
Timezone myTimezone(myDSTStart, myDSTEnd);

// Month and day names, translate if you want
String weekdagen[]={"MAANDAG","DINSDAG","WOENSDAG","DONDERDAG","VRIJDAG","ZATERDAG","ZONDAG"};
String weekdagenKort[]={"MA","DI","WO","DO","VR","ZA","ZO"};
String maanden[]={"JANUARI","FEBRUARI","MAART","APRIL","MEI","JUNI","JULI","AUGUSTUS","SEPTEMBER","OKTOBER","NOVEMBER","DECEMBER"};

//Tijd variabelen
unsigned long currentTimestamp;
int dayOfWeek;
int currentMonth;
int currentHour;

const int httpsPort = 443;
WiFiClientSecure client;

// Structure to store event details
struct Event {
  String summary;
  unsigned long startDate;
  bool isAllDay;
};

GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT / 4> display(GxEPD2_750c_Z08(/*CS=15*/ CS_PIN, /*DC=4*/ DC_PIN, /*RST=5*/ RST_PIN, /*BUSY=16*/ BUSY_PIN));

Event events[100]; // Adjust the size as needed (but be careful, to big and it crashes)
int eventCount = 0;

// Arrays to collect calendar coloring
bool dagBezetting[30] = {false};        // event during the day
bool dagBezettingAllDay[30] = {false};  // all day events
bool dagBezettingExtra[30] = {false};   // additional calendar events (e.g. AirBNB occupation)

int currentDay;

// RTC memory to store wakeup count
RTC_DATA_ATTR int wakeupCount = -1;
esp_sleep_wakeup_cause_t wakeup_reason;

/**
 * Everything happens in setup()
 */
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);

  print_wakeup_reason();
  
  // Countdown to 1 in the morning.
  if (wakeup_reason==ESP_SLEEP_WAKEUP_TIMER ) {
    --wakeupCount;
    if(wakeupCount>0) { //keep sleeping
      Serial.println("Nog " + String(wakeupCount) + " uur te gaan. Verder slapen...");
      startDeepSleep();
    }
  }

  // Connect to Wi-Fi
  connectWifi();

  // Get current date and time
  getNetworkTime(); 

  // Read and process main calendar
  verwerkKalender();

  // Read and process extra calender
  if(second_calendar) {
    extraKalender();
  }

  // Update the ePaper
  updateDisplay();

  // Turn of buildin led
  digitalWrite(LED_BUILTIN, LOW);

  //delay to allow software update on first boot
  if (wakeup_reason!=ESP_SLEEP_WAKEUP_TIMER ) {
    for(int i=0;i<180;i++) {
      delay(1000);
    }
  }

  // Set deepsleep counter based on current time
  setDeepSleepCounter();

  // Sleep until 1 AM
  startDeepSleep();
}

void loop() {
  // Empty loop
}


/**
 * Function to compare two events by start date
 */
int compareEvents(const void* a, const void* b) {
  Event* eventA = (Event*)a;
  Event* eventB = (Event*)b;
  if (eventA->startDate < eventB->startDate) {
    return -1;
  } else if (eventA->startDate > eventB->startDate) {
    return 1;
  } else {
    return 0;
  }
}

/**
 * Connect to wifi
 */
void connectWifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
    digitalWrite(LED_BUILTIN, LOW);
    delay(800);
    Serial.println("Connecting to Wi-Fi...");
  }
  Serial.println("Connected to Wi-Fi");
}

/**
 * Get NTP time
 */
void getNetworkTime() {
  Serial.println("NTP connect");
  timeClient.begin();
  delay(100);
  Serial.println("NTP update");
  timeClient.update();
  delay(100);
  Serial.println("Get epoch");
  currentTimestamp = timeClient.getEpochTime();
  Serial.println("Current timestamp: " + String(currentTimestamp));

  // Localize the time
  currentTimestamp = myTimezone.toLocal(currentTimestamp);

  time_t time = static_cast<time_t>(currentTimestamp);
  struct tm* timeInfo = localtime(&time);
  
  dayOfWeek = (timeInfo->tm_wday + 6) % 7;
  currentMonth = timeInfo->tm_mon;
  currentHour = timeInfo->tm_hour;
  currentDay = currentTimestamp / 86400;
}

/**
 * Read an process the main calendar
 */
void verwerkKalender() {
  // Connect to the server
  startRequest(host,host_url);

  // Read and parse the iCal file
  String currentLine = "";
  String currentSummary = "";
  String currentStartDate = "";
  String currentEndDate = "";
  bool inEvent = false;
  bool isMultiday = false;
  bool calendarEnd = false;
  unsigned long currentStartDateEpoch;
  unsigned long startDateMultiEpoch;
  unsigned long endDateMultiEpoch;
  int multiDayCount=0;
  int lineCount=0;
  
  while (client.connected() && eventCount<50 && !calendarEnd) {
    String line = client.readStringUntil('\n');
    lineCount++;

    if(inEvent) {
      digitalWrite(LED_BUILTIN, HIGH);
    } else {
      digitalWrite(LED_BUILTIN, LOW);
    }

    if (line.startsWith("BEGIN:VEVENT")) {
      inEvent = true;
    } else if (line.startsWith("END:VCALENDAR")) {
      calendarEnd = true;
      Serial.println("Stop parsing: END:VCALENDAR.");
    } else if (inEvent && line.startsWith("END:VEVENT")) {
      if(currentStartDate.length()<8) {
          Serial.println("Issue with event start date: " + String(currentStartDate) + " / " + String(currentSummary) + " -> skip...");
      } else {
        currentStartDateEpoch = zuluToEpoch(currentStartDate.c_str());
        int dayDifference;
        if(isMultiday) { // Loop trough days
          // Multi- or allday event
          currentStartDate.trim();
          currentEndDate.trim();
          startDateMultiEpoch = zuluToEpoch((currentStartDate + "T000000Z").c_str());
          endDateMultiEpoch = zuluToEpoch((currentEndDate + "T000000Z").c_str());
          multiDayCount = int(endDateMultiEpoch/86400) - int(startDateMultiEpoch/86400);
                   
          if(endDateMultiEpoch+86400<currentTimestamp || startDateMultiEpoch > currentTimestamp+30*84600 ) {
            // Event in the past or far in the future
          } else {
            for (int i=0;i<multiDayCount;i++) {
              // Check day
              if(startDateMultiEpoch + (i+1) * 86400>currentTimestamp) {
                events[eventCount].summary = currentSummary;
                events[eventCount].startDate = startDateMultiEpoch + i * 86400;
                events[eventCount].isAllDay = true;
                dayDifference = int((startDateMultiEpoch+i*86400)/86400) - currentDay;
                // Check if the epoch time is within the 30-day window
                if (dayDifference >= 0 && dayDifference < 30) {
                  dagBezettingAllDay[dayDifference] = true;
                }
                eventCount++;
              }
            }
          }
        } else {
          //Event with timestamp
          if(currentStartDate!="") {
            events[eventCount].summary = currentSummary;
            events[eventCount].startDate = zuluToEpoch(currentStartDate.c_str());
            events[eventCount].isAllDay = false;
            
            // Add to occupation
            dayDifference = int(currentStartDateEpoch/86400) - currentDay;
            // Check if the epoch time is within the 30-day window
            if (dayDifference >= 0 && dayDifference < 30) {
              dagBezetting[dayDifference] = true;
            }
            eventCount++;
          } else {
            // Something is wrong
          }
        }
      }

      // Clear temporary event data
      inEvent = false;
      isMultiday = false;
      currentSummary = "";
      currentStartDate = "";
      currentEndDate = "";
    } else if (inEvent) {
      if (line.startsWith("SUMMARY:")) {
        currentSummary = line.substring(8); // 8 is the length of "SUMMARY:"
      } else if (line.startsWith("DTSTART:")) {
        currentStartDate = line.substring(8); // 8 is the length of "DTSTART:"
        
        if(currentStartDate.length()<4) { // No usable date
          inEvent = false;
          currentSummary = "";
          currentStartDate = "";
        } else {
          currentStartDateEpoch = zuluToEpoch(currentStartDate.c_str());
        }

        // Check if the epoch time is within the 30-day window
        if(currentStartDateEpoch<currentTimestamp || currentStartDateEpoch > currentTimestamp+2592000) {
          inEvent = false;
          currentSummary = "";
          currentStartDate = "";
        }

      } else if (line.startsWith("DTEND;VALUE=DATE:")) { //multiday and recurring
        currentEndDate = line.substring(17);
      } else if (line.startsWith("DTSTART;")) { //multiday and recurring

        currentStartDate = line.substring(8); // 8 is the length of "DTSTART;"
       
        // Multiday event
        if(currentStartDate.startsWith("VALUE=DATE:")) {
          isMultiday = true;
          currentStartDate=currentStartDate.substring(11);
        }

        //recurring event - not implemented!
        /* Recurring events
          BEGIN:VEVENT
          DTSTART;TZID=Europe/Brussels:20200615T090000
          DTEND;TZID=Europe/Brussels:20200615T130000
          RRULE:FREQ=WEEKLY;WKST=TU
          EXDATE;TZID=Europe/Brussels:20220822T090000
          EXDATE;TZID=Europe/Brussels:20220815T090000
          EXDATE;TZID=Europe/Brussels:20220808T090000
          */
        if(currentStartDate.startsWith("TZID=")) {
          inEvent = false;
          currentSummary = "";
          currentStartDate = "";
        }
      }
    }
  }
  Serial.println("End of loop, " + String(eventCount) + " events found.");

  // Sort the events by start date
  qsort(events, eventCount, sizeof(Event), compareEvents);
}

/**
 * Additional calendar for occupation of days (e.g. AirBNB occupation)
 */
void extraKalender() {
  Serial.println("BNB kalender");
  startRequest(host_2,host_url_2);

  // Read and parse the iCal file
  String currentLine = "";
  String currentSummary = "";
  String currentStartDate = "";
  String currentEndDate = "";
  bool inEvent = false;
  bool isMultiday = false;
  bool calendarEnd = false;
  unsigned long currentStartDateEpoch;
  unsigned long startDateMultiEpoch;
  unsigned long endDateMultiEpoch;
  int multiDayCount=0;
  
  while (client.connected() && !calendarEnd) {
    String line = client.readStringUntil('\n');
    if (line.startsWith("BEGIN:VEVENT")) {
      inEvent = true;
    } else if (line.startsWith("END:VCALENDAR")) {
      calendarEnd = true;
    } else if (inEvent && line.startsWith("END:VEVENT")) {
      currentStartDateEpoch = zuluToEpoch(currentStartDate.c_str());
      int dayDifference;
      if(isMultiday) {
        // Multi- of allday event
        currentStartDate.trim();
        currentEndDate.trim();
        startDateMultiEpoch = zuluToEpoch((currentStartDate + "T000000Z").c_str());
        endDateMultiEpoch = zuluToEpoch((currentEndDate + "T000000Z").c_str());
        multiDayCount = int(endDateMultiEpoch/86400) - int(startDateMultiEpoch/86400);
        if(endDateMultiEpoch+86400<currentTimestamp || startDateMultiEpoch > currentTimestamp+30*84600 ) {
        } else {
          for (int i=0;i<multiDayCount;i++) {
            // Chack day
            if(startDateMultiEpoch + (i+1) * 86400>currentTimestamp) {
              dayDifference = int((startDateMultiEpoch+i*86400)/86400) - currentDay;
              // Check if the epoch time is within the 30-day window
              if (dayDifference >= 0 && dayDifference < 30) {
                dagBezettingExtra[dayDifference] = true;
              }
            }
          } 
        }
      }
      // event reset
      inEvent = false;
      isMultiday = false;
      currentSummary = "";
      currentStartDate = "";
      currentEndDate = "";
    } else if (inEvent) {
      if (line.startsWith("SUMMARY:")) {
        currentSummary = line.substring(8); // 8 is the length of "SUMMARY:"
        /* THIS IS SPECIFIC FOR AIRBNB CALENDAR, change if needed! */
        if(currentSummary!="Reserved") {
          inEvent = false;
          isMultiday = false;
          currentSummary = "";
          currentStartDate = "";
          currentEndDate = "";
        }
      } else if (line.startsWith("DTSTART:")) {
        currentStartDate = line.substring(8); // 8 is the length of "DTSTART:"
        currentStartDateEpoch = zuluToEpoch(currentStartDate.c_str());
        if(currentStartDateEpoch<currentTimestamp || currentStartDateEpoch > currentTimestamp+2592000) {
          inEvent = false;
          currentSummary = "";
          currentStartDate = "";
        }

      } else if (line.startsWith("DTEND;VALUE=DATE:")) { // multiday and recurring
        currentEndDate = line.substring(17);
      } else if (line.startsWith("DTSTART;")) { // multiday and recurring

        currentStartDate = line.substring(8); // 8 is the length of "DTSTART;"
       
        // multiday event
        if(currentStartDate.startsWith("VALUE=DATE:")) {
          //Serial.println("- Meerdagen event -");
          isMultiday = true;
          currentStartDate=currentStartDate.substring(11);

        }
        // recurring event
        if(currentStartDate.startsWith("TZID=")) {
          inEvent = false;
          currentSummary = "";
          currentStartDate = "";
        }
      }
    }
  }
}

/**
 * Start the request
 */
void startRequest(const char* request_host, const char* request_host_url) {
  digitalWrite(LED_BUILTIN, HIGH);
  // Connect to the server
  client.stop();
  delay(100);
  client.setInsecure(); // Avoid using certificates
  if (!client.connect(request_host, httpsPort)) {
    Serial.println("Connection failed!");
    return;
  }
  Serial.println("Connection made to " + String(request_host) + " on port " + String(httpsPort));

  // Send a request to download the iCal file
  String request = "GET " + String(request_host_url) + " HTTP/1.1\r\n";
  request += "Host: " + String(request_host) + "\r\n";
  request += "Connection: close\r\n\r\n";
  client.print(request);
  Serial.println("Requested.");
  digitalWrite(LED_BUILTIN, LOW);
}

/**
 * Zulu time string to epoch
 */
unsigned long zuluToEpoch(const char* zuluTimeString) {
  int year, month, day, hour, minute, second;
  sscanf(zuluTimeString, "%4d%2d%2dT%2d%2d%2dZ", &year, &month, &day, &hour, &minute, &second);

  // Calculate the number of days since the UNIX epoch (January 1, 1970)
  unsigned long days = 0;
  int y = year - 1970;
  int m = month;
  int d = day;
  
  // Account for leap years
  for (int i = 1970; i < year; i++) {
    if ((i % 4 == 0 && i % 100 != 0) || (i % 400 == 0)) {
      days += 366; // Leap year
    } else {
      days += 365; // Non-leap year
    }
  }
  
  // Account for days in the current year
  int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  
  // Adjust for leap year
  if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
    daysPerMonth[1] = 29;
  }
  
  for (int i = 0; i < month - 1; i++) {
    days += daysPerMonth[i];
  }
  
  days += day - 1;

  // Convert to seconds
  unsigned long epochTime = days * 86400 + hour * 3600 + minute * 60 + second;
  
  // make local
  epochTime = myTimezone.toLocal(epochTime);
  return epochTime;
}

/**
 * Epoch time to readable date/time
 */
String formatEpochTime(time_t epoch_time) {
  static char buffer[25];
  struct tm *tm_info;

  tm_info = localtime(&epoch_time);
  strftime(buffer, 25, "%d-%m-%Y %H:%M", tm_info);

  return String(buffer);
}
String formatEpochDate(time_t epoch_time) {
  static char buffer[25];
  struct tm *tm_info;

  tm_info = localtime(&epoch_time);
  strftime(buffer, 25, "%d-%m-%Y", tm_info);

  return String(buffer);
}

/**
 * Check if day has event
 */
bool hasEvent(int i) {
  return dagBezetting[i];
}
bool hasAllDayEvent(int i) {
  return dagBezettingAllDay[i];
}
bool hasExtraEvent(int i) {
  return dagBezettingExtra[i];
}

/**
 * Draw centered string
 */
void drawCentreString(const String &buf, int x, int y)
{
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(buf, x, y, &x1, &y1, &w, &h); //calc width of new string
    display.setCursor(x - w / 2, y+h/2);
    display.print(buf);
}

/**
 * Update display with new data
 */
void updateDisplay() {
  display.init(115200, true, 2, false);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500); 
  digitalWrite(LED_BUILTIN, LOW);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();


  int currentCircleDay = getDayOfMonth(currentTimestamp);
  int currentDayOfMonth = getDayOfMonth(currentTimestamp);
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // Current day
    display.fillRect(20, 10, 280, 60, GxEPD_RED);
    display.drawRect(20, 10, 280, 120, GxEPD_RED);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&FreeSansBold18pt7b);
    drawCentreString(weekdagen[dayOfWeek],160,39);

    display.setTextColor(GxEPD_RED);
    drawCentreString(String(currentDayOfMonth) + " " + maanden[currentMonth],160,97);
    

    // Vertical line between calendar and events
    display.fillRect(340, 0, 2, 480, GxEPD_RED);
    
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold9pt7b);

    // Weekdays on calendar
    for(int i=0;i<7;i++) {
      drawCentreString(weekdagenKort[i],40+ i*40,170);
    }
  
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold9pt7b);

    // Draw circles for each day
    for (int i = 0; i < 29; i++) {
        int x = 40 + ((i+dayOfWeek) % 7) * 40;
        int y = 210 + ((i+dayOfWeek) / 7) * 40;


        // Border color
        if (hasEvent(i)) { // Check if there's an event on this day
            display.fillCircle(x, y, 15, GxEPD_RED);
        } else {
            //display.fillCircle(x, y, 15, GxEPD_BLACK);
        }

        // Fill color
        if(hasAllDayEvent(i)) { // All day event
            display.fillCircle(x, y, 15, GxEPD_RED);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.fillCircle(x, y, 13, GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
        }
        
        // Extra calendar
        if(second_calendar && hasExtraEvent(i)) {
            display.fillCircle(x+12, y+12, 5, GxEPD_RED);
        }

        currentCircleDay = getDayOfMonth(currentTimestamp + i*86400);
        if (currentCircleDay<10) {
          display.setCursor(x - 6, y + 5);
        } else {
          display.setCursor(x - 10, y + 5);
        }
        display.print(currentCircleDay);
    }
    
    
    // Print the sorted events
    display.setTextColor(GxEPD_BLACK);
    for (int i = 0; i < eventCount && i < 8; i++) {
      Serial.println(String(i) + " Event: " + events[i].summary + ", Start Date: " + formatEpochTime(events[i].startDate));
      display.setCursor(400, i*55 + 40);
      display.setFont(&FreeSansBold9pt7b);

      if(events[i].isAllDay) {
        display.print(formatEpochDate(events[i].startDate));
      } else {
        display.print(formatEpochTime(events[i].startDate));
      }
      display.setCursor(400, i*55 + 60);
      display.setFont(&FreeSans9pt7b);
      display.print(events[i].summary);
      if(events[i].isAllDay) {
        display.fillRect(390, i*55+25, 2, 40, GxEPD_BLACK);
      }
    }
    

    // Last update information
    display.setCursor(400, 8*55 + 30);
    display.print("Laatste update: " + formatEpochTime(currentTimestamp));       
    


    // info bezetting bnb
    if(second_calendar) {
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(30, 420);
      display.setFont(&FreeSans9pt7b);
      if(hasExtraEvent(0)) {
        display.print("BnB is bezet vandaag");
        if(!hasExtraEvent(1)) {
          display.setCursor(30, 445);
          display.setFont(&FreeSans9pt7b);;
          display.print("Gasten vertrekken morgen");
        }
      } else {
        display.print("BnB is vrij vandaag");
        if(hasExtraEvent(1)) {
          display.setCursor(30, 445);
          display.setFont(&FreeSans9pt7b);
          display.setTextColor(GxEPD_RED);
          display.print("Gasten komen morgen aan");
        }
      }
    }


    Serial.println("setup do"); 
  }
  while (display.nextPage());
  Serial.println("setup done"); 
  display.hibernate();
}

/**
 * Get day of the month
 */
int getDayOfMonth(time_t epochTime) {
  const int daysPerMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}; // Number of days in each month
  int year, month, day;
  
  // Calculate number of days since epoch (1970-01-01)
  int numDays = epochTime / 86400; // 86400 seconds per day

  // Calculate year
  year = 1970; // Start from 1970
  while (numDays >= 365) {
    if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
      // Leap year
      if (numDays >= 366) {
        numDays -= 366;
        year++;
      } else {
        break;
      }
    } else {
      // Non-leap year
      numDays -= 365;
      year++;
    }
  }

  // Calculate month and day
  month = 0; // Start from January
  day = numDays + 1; // Add 1 to get the correct day of the month
  while (day > daysPerMonth[month]) {
    if (month == 1 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
      // Leap year, February has 29 days
      if (day > 29) {
        day -= 29;
        month++;
      } else {
        break;
      }
    } else {
      // Non-leap year, or other months
      day -= daysPerMonth[month];
      month++;
    }
  }

  // Return day of the month
  return day;
}

/**
 * Deepsleep counter around 1 AM to synch
 */
void setDeepSleepCounter() {
  wakeupCount = (25 - currentHour) % 24;
  Serial.println("Het is " + String(currentHour) + " uur. We gaan " + String(wakeupCount) + " uur slapen...");
}

/**
 * Start deepsleep
 */
 void startDeepSleep() {

  Serial.println("Slaapwel!");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup(3600000000); //1 hour
  //esp_sleep_enable_timer_wakeup(5000000); //5 seconds, for testing
  esp_deep_sleep_start();
 }

 /*
  * Show wakeup reason
  */
void print_wakeup_reason(){
  
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}
