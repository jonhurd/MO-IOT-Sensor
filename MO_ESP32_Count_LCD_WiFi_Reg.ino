/*
This board counts products passing by a sensor and posts* the timestamp and current
count to a Google Sheet named TestSheet. If the reset button is pressed, the count
is reset to zero and saved to the TestSheet. When the system is started it attempts to connect to the previously
saved WiFi network. If no WiFi connection is available or if the user optionally presses
a button, a new WiFi network can be selected and configured with the appropriate credentials by 
accessing the ESP32's Access Point called RecycleCans. The WiFi credentials are
saved and will be reused the next time the system is started. Instructions to the
end user are displayed on the I2C LCD during WiFi configuration. If a serial number
has not been previously saved, a new serial is requested from the MO registration
service. The serial number for the device is saved in ESP32 preferences. The count
of products is also saved in ESP32 preferences, so if power is lost, the previously
saved count is maintained.

On startup the recycle message is extracted by parsing the comma delimited
string in cell A2 of the AppConfiguration Google Sheet.

On startup the display shows the following in sequence:
"Connecting..."" 
"Connected to Wifi Network"
"M/O Serial #"
"Reading Configuration"

Then the display begins cycling through the following messages:
"Date and Time"
"Saving the Earth - xxx cans crushed"
"Reduce Reuse Recycle"  

If a can is crushed the display shows:
"Good job! - xxx cans crushed" 
then returns to cycling through the date time, saving the earth, reduce reuse recycle messages.

xxx is the number of cans crushed since the last can count reset.

* Data is posted to Google Sheets after every n cans have been crushed where n is set by
the value saved in the AppConfiguration Google Sheet.

Adding Backlight power save read from AppConfiguration


Circuit:  ESP32
The circuit:
  - ESP32
  - Count Reset Button Switch connected to ground and Pin GPIO5 of the ESP32.
  - Wifi Manual Configuration Request Button Switch connected to ground and Pin GPIO4 of the ESP32.
  - I2C Display
    - VCC to Vin
    - Gnd to Gnd
    - SCL to GPIO22 of ESP32
    - SDA to GPIO21 of ESP32
  - Proximity Sensor
    - VCC to 3.3 volts
    - Gnd to Gnd
    - Output to pin GPIO34 of ESP32
  - Network Configuration LED
    - 220 ohm resistor one end to pin GPIO26 of ESP32 other end to Anode of LED
    - LED Cathode connects to ground
  - Reset Count Switch
    - One side of switch to ESP32 pin D5
    - Other side of switch to Gnd
  - Reset WiFi Configuration Switch
    - One side of switch to ESP32 pin D4
    - Other side of switch to Gnd
  - Vin Filter Capacitors 10uf and .1uf
    - Plus sides to Vin
    - Minus sides to Gnd
  - 3.3 Volt Filter Capacitors 10uf and .1uf
    - Plus sides to 3V3
    - Mius sides to Gnd
*/
#include <Wire.h>
#include "WiFi.h"
#include "time.h"
#include <NTPClient.h>
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>
#include <BluetoothSerial.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Preferences.h>

const char* ssid = "RecycleCans";
const char* password = "";

#define sensorPin 34
#define resetCount 5
#define LED 2 //Internal LED
#define lcdColumns 16
#define lcdRows 2
#define resetWiFi 4 //Pin to trigger the WiFi configuration portal
#define WiFiConfigLED 26
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);
bool objectDetected = false;
int objectCount = 0;
int postCount = 0;
int recycleMessageDuration;
int timeTemperatureDuration;
int currentRecycleCountDuration;
int canCrushedDuration;
int howOftenToPost;
String recycleMessage1;
String recycleMessage2;
bool backlightPowerSave=false;
unsigned long currentTime;
unsigned long loopStartTime;
unsigned long lastDisplayTime = 0;
unsigned long previousDisplayTime = 0;
unsigned long lastCanCrushedTime = 0;
const unsigned long displayOffDelay = 60000; // 1 minute (in milliseconds)


enum DisplayMode {
  RECYCLE_MESSAGE,
  DATE_TIME,
  CURRENT_RECYCLE_COUNT,
  CAN_CRUSHED,
};

DisplayMode currentMode = RECYCLE_MESSAGE;

//Google Sheets RecycleSerial
String GOOGLE_SERIAL_SCRIPT_ID = "AKfycbwDBgEl-vjkz7JfAZqBd7DNydgBpyJnFOduEUIBSs0EKhRnbxLy1C75RvFu0Y22lnGD";
//Google Sheets Test Sheet
String GOOGLE_COUNT_SCRIPT_ID = "AKfycbwMUCfc434eCU5brwDQ3PPhnH9iSRZCmiQVdWEoEPeLo-qWjn3YntCWfKlbMSbZc_2U";
//Google Sheets AppConfiguration
String GOOGLE_CONFIG_SCRIPT_ID = "AKfycbz6nPRo1sWOxLn442Nehc_aPl2JUPEC6icez96tvmOhycn0y36o_zvOICIT6pzUWUxneg";

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   daylightOffset_sec = 0;
// Define NTP Client properties
const long gmtOffsetInSeconds = -4 * 3600;
const int daylightOffsetInSeconds = 3600; // Modify this if you observe daylight saving time
float temperatureF;
float temperatureC;
String date;
String timeOfDay;
bool countIsNew = false;
int buttonState = 0; //Reset count button
String serialNumber;

// GPIO where the DS18B20 is connected to
//const int oneWireBus = 18;

// Setup a oneWire instance to communicate with any OneWire devices
//OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor
//DallasTemperature sensors(&oneWire);

// Initialize the UDP client for NTP
WiFiUDP ntpUDP;

// Initialize the NTP client
NTPClient timeClient(ntpUDP, ntpServer, gmtOffsetInSeconds, daylightOffsetInSeconds);

Preferences preferences;

String getFormattedDate() {
  String formattedDate;
  time_t epochTime = timeClient.getEpochTime();
  struct tm *timeinfo;
  timeinfo = localtime(&epochTime);

  formattedDate += String(timeinfo->tm_mon + 1);
  formattedDate += "/";
  formattedDate += String(timeinfo->tm_mday);
  formattedDate += "/";
  formattedDate += String(timeinfo->tm_year + 1900);
  return formattedDate;
}

void displayDateTime(boolean firstTime) {
  // Update the time from the NTP server
  timeClient.update();

  // Get the current local date and time
  date = getFormattedDate();
  timeOfDay = timeClient.getFormattedTime();
  
  //time_t epochTime = timeClient.getEpochTime();
  //struct tm *timeinfo;
  //timeinfo = localtime(&epochTime);

  //char formattedTime[9]; // Allocate space for HH:MM AM/PM

  // Format the time in AM/PM format
  //strftime(formattedTime, sizeof(formattedTime), "%I:%M %p", timeinfo);
  
  if (firstTime==true) {
    lcd.clear();
  }
  lcd.setCursor(0, 0);
  lcd.print("Date:");
  lcd.setCursor(6, 0);
  lcd.print(date);
  lcd.setCursor(0, 1);
  lcd.print("Time:");
  lcd.setCursor(6, 1);
  lcd.print(timeOfDay);
  //lcd.print(formattedTime);
}

void displayCurrentRecycleCount(boolean firstTime) {
  if (firstTime == true) {
    lcd.setCursor(0, 0);
    lcd.print("Saving the earth");
    lcd.setCursor(0, 1);
    lcd.print("                "); // Clear the second line
    lcd.setCursor(0, 1);
    lcd.print(objectCount);
    if (objectCount == 1) {
      lcd.print(" can crushed");
    } else {
      lcd.print(" cans crushed");
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Saving the earth");
    lcd.setCursor(0, 1);
    lcd.print(objectCount);
    if (objectCount == 1) {
      lcd.print(" can crushed");
    } else {
      lcd.print(" cans crushed");
    }
  }
}

void displayRecycleMessage(boolean firstTime) {
  if (firstTime == true) {
    lcd.clear();  // Ensures previous text is erased
    lcd.setCursor(0, 0);
    lcd.print(recycleMessage1);
    lcd.setCursor(0, 1);
    lcd.print(recycleMessage2);
  }else {
    lcd.setCursor(0, 0);
    lcd.print(recycleMessage1);
    lcd.setCursor(0, 1);
    lcd.print(recycleMessage2);
  }
}

void displaySerial() {
    lcd.setCursor(0, 0);
    lcd.print("M/O Serial:");
    lcd.setCursor(0, 1);
    lcd.print(serialNumber);
    delay(10000);
}

void parseConfigurationSettings(String configCellData) {
  // Split the comma-delimited cell data into individual settings
  int pos = 0;
  String settings[8];  // Assuming you have 8 settings in this order

  for (int i = 0; i < 8; i++) {
    int nextPos = configCellData.indexOf(',', pos);
    if (nextPos == -1) {
      nextPos = configCellData.length();
    }
    settings[i] = configCellData.substring(pos, nextPos);
    pos = nextPos + 1;
  }

  // Assuming the settings are in the order: recycleMessageDuration, timeTemperatureDuration, 
  // currentRecycleCountDuration, canCrushedDuration,howOftenToPost,recycleMessage1, recycleMessage2, backlightPowerSave
  recycleMessageDuration = settings[0].toInt();
  timeTemperatureDuration = settings[1].toInt();
  currentRecycleCountDuration = settings[2].toInt();
  canCrushedDuration = settings[3].toInt();
  howOftenToPost = settings[4].toInt();
  recycleMessage1 = settings[5];
  recycleMessage2 = settings[6];
  if(settings[7].equalsIgnoreCase("TRUE")) {
    backlightPowerSave = true;
  } else if(settings[7].equalsIgnoreCase("FALSE")) {
    backlightPowerSave = false;
  } else {
    Serial.println("Invalid value for backlightPowerSave");
  }

  // Assuming you have declared backlightPowerSave as a bool somewhere in your code
  Serial.print("Backlight Power Save Mode = ");
  Serial.println(backlightPowerSave ? "true" : "false");
  Serial.println("recycleMessage2 = " + recycleMessage2);
  Serial.println("recycleMessage2 = " + recycleMessage2);
}

void getConfiguration(void) {
    lcd.setCursor(0,0);
    lcd.print("Reading         ");
    lcd.setCursor(0,1);
    lcd.print("Configuration   ");

    HTTPClient http;
    String url = "https://script.google.com/macros/s/" + GOOGLE_CONFIG_SCRIPT_ID + "/exec?read";
    
    Serial.println("Fetching configuration from: " + url);
    
    http.begin(url.c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(10000);  // Increase timeout to 10 seconds

    int httpCode = http.GET();
    Serial.print("HTTP Status Code: ");
    Serial.println(httpCode);

    if (httpCode <= 0) {
        Serial.println("⚠️ Error on HTTP request - Possible Timeout or Network Issue");
        Serial.println(http.errorToString(httpCode)); // Print detailed error
        http.end();
        return;
    }

    // Read response
    String configSettings = http.getString();
    Serial.println("Received Configuration Data: [" + configSettings + "]");

    parseConfigurationSettings(configSettings);
    
    http.end();
}


void configModeCallback(WiFiManager *myWiFiManager) {
  lcd.clear();
  lcd.backlight();
  digitalWrite(WiFiConfigLED, HIGH);
  lcd.print("Configuring...");
  lcd.setCursor(0, 1);
  lcd.print("Please connect");
  delay(4000);
  
  lcd.clear();
  lcd.print("to WiFi network:");
  lcd.setCursor(0, 1);
  lcd.print(ssid);
  delay(2000);
}

void connectToWiFi() {
  Serial.println("Connecting to Wi-Fi...");
  
  lcd.clear();
  lcd.backlight();
  lcd.print("Connecting...");
  
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);

  if (!wifiManager.autoConnect(ssid, password)) {
    Serial.println("Failed to connect and hit timeout!");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  
  Serial.println("Connected to Wi-Fi network!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  lcd.clear();
  lcd.backlight();
  lcd.print("Connected to:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.SSID());
  delay(5000);
  lcd.clear();
  lastDisplayTime = millis();
  displayCurrentRecycleCount(true);
  currentMode=CURRENT_RECYCLE_COUNT;
}

void displayCanCrushed(boolean firstTime) {
  // Print the current count and turn on the backlight 
  if (firstTime == true) {
    lcd.setCursor(0, 0);
    lcd.print("Good Job!          ");
    lcd.setCursor(0, 1);
    lcd.print("               "); // Clear the second line
    lcd.setCursor(0, 1);
    lcd.print(objectCount);
    if (objectCount == 1) {
      lcd.print(" can crushed");
    } else {
      lcd.print(" cans crushed");
    }
  } else {
    lcd.setCursor(0, 0);
    lcd.print("Good Job!          ");
    lcd.setCursor(0, 1);
    lcd.print(objectCount);
    if (objectCount == 1) {
      lcd.print(" can crushed");
    } else {
      lcd.print(" cans crushed");
    }
  }
  // Turn on the backlight
  lcd.backlight();
  // If the count has changed, update Google Sheets
  if (countIsNew == true && postCount == howOftenToPost) {
    unsigned long postStartTime = millis();
    postDataToGoogleSheets();
    Serial.println("Post time was " + String(millis() - postStartTime) + " Milliseconds");
    Serial.println("How often: " + howOftenToPost);
    postCount = 0;
    countIsNew = false;
  }
}

void turnOffDisplay() {
  // Turn off the backlight
  lcd.noBacklight();
}

void postDataToGoogleSheets() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        String url = "https://script.google.com/macros/s/" + GOOGLE_COUNT_SCRIPT_ID + "/exec?" +
                     "serial=" + serialNumber + "&count=" + String(objectCount);
        Serial.println("Posting to: " + url);
        
        http.begin(url.c_str());
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int httpCode = http.GET();

        Serial.print("HTTP Response Code: ");
        Serial.println(httpCode);

        if (httpCode > 0) {
            String payload = http.getString();
            Serial.println("Google Sheets Response: " + payload);
        } else {
            Serial.println("Error: Failed to post data");
        }
        http.end();
    } else {
        Serial.println("WiFi not connected, cannot post data.");
    }
}



/*
void postDataToGoogleSheets() {
  if (WiFi.status() == WL_CONNECTED) {
    static bool flag = false;
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }
    char timeStringBuff[50]; //50 chars should be enough
    strftime(timeStringBuff, sizeof(timeStringBuff), "%m/%d/%y %H:%M:%S", &timeinfo);
    String asString(timeStringBuff);
    asString.replace(" ", "-");
    Serial.print("Time:");
    Serial.println(asString);
    String urlFinal = "https://script.google.com/macros/s/"+GOOGLE_COUNT_SCRIPT_ID+"/exec?"+"serial="+serialNumber+"&date="+asString+"&count="+String(objectCount);
    Serial.print("POST data to spreadsheet:");
    Serial.println(urlFinal.c_str());
    HTTPClient http;
    http.begin(urlFinal.c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET(); 
    //Serial.print("HTTP Status Code: ");
    //Serial.println(httpCode);
    //---------------------------------------------------------------------
    //getting response from google sheet
    String payload;
    if (httpCode > 0) {
        payload = http.getString();
        //Serial.println("Payload: "+payload);    
    }
    //---------------------------------------------------------------------
    http.end();
  }
}
*/

void checkForSerialNumber() {
  //preferences.clear();
  // Check if the serial number exists
  serialNumber = preferences.getString("serialNumber","");
  Serial.println("Saved serialNumber = "+serialNumber);
  if (serialNumber.startsWith("MO")) {
    Serial.println("Serial Number already exists");
    } else {
    Serial.println("Serial Number does not exist");
    requestSerialNumber();
  }
}
void requestSerialNumber(void) {
   //-----------------------------------------------------------------------------------
   HTTPClient http;
   String url="https://script.google.com/macros/s/"+GOOGLE_SERIAL_SCRIPT_ID+"/exec?read";
  //Serial.print(url);
  Serial.println("Reading Serial Number from Google Sheet");
  http.begin(url.c_str());
  //-----------------------------------------------------------------------------------
  //Removes the error "302 Moved Temporarily Error"
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  //-----------------------------------------------------------------------------------
  //Get the returning HTTP status code
  int httpCode = http.GET();
  Serial.print("HTTP Status Code: ");
  Serial.println(httpCode);
  //-----------------------------------------------------------------------------------
  if(httpCode <= 0){Serial.println("Error on HTTP request"); http.end(); return;}
  //-----------------------------------------------------------------------------------
  //Read next available serial number from Google Sheet
  serialNumber = http.getString();
  Serial.println("Serial Number"+ serialNumber);
  preferences.putString("serialNumber",serialNumber);
  //-----------------------------------------------------------------------------------
  if(httpCode == 200){
    //If we reached this point tell the Google Sheet we set the serial number successfully
    updateNextSerialNumber("serial_status=SerialSaved");
  //-------------------------------------------------------------------------------------
  http.end();
  }
}
void updateNextSerialNumber(String params) {
   HTTPClient http;
   String url="https://script.google.com/macros/s/"+GOOGLE_SERIAL_SCRIPT_ID+"/exec?"+params;
    Serial.print(url);
    Serial.println("Updating Serial Save Status");
    http.begin(url.c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpCode = http.GET();  
    //Serial.print("HTTP Status Code: ");
    //Serial.println(httpCode);
    
    String payload;
    if (httpCode > 0) {
        payload = http.getString();
        //Serial.println("Payload: "+payload);     
    }
    http.end();
}


void loadObjectCount() {
  // Check if the objectCount has been saved
    objectCount = preferences.getInt("objectCount", 0);
    Serial.println("Saved objectCount = " + String(objectCount));
}

void saveObjectCount() {
  preferences.putInt("objectCount", objectCount);
}

void enterManualConfigMode() {
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setConfigPortalTimeout(180);
  
  if (!wifiManager.startConfigPortal(ssid, password)) {
    Serial.println("Failed to connect in manual configuration mode and hit timeout!");
    delay(3000);
    ESP.restart();
    delay(5000);
  }
  
  Serial.println("Connected in manual configuration mode!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  lcd.clear();
  lcd.backlight();
  lcd.print("Connected to:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.SSID());
  delay(3000);
  lcd.clear();
  lastDisplayTime = millis();
  displayCurrentRecycleCount(true);
  currentMode=CURRENT_RECYCLE_COUNT;
}
void checkManualWifiConfig() {
  if (digitalRead(resetWiFi) == LOW) {
    Serial.println("Switch activated. Entering manual configuration mode.");
    lcd.clear();
    lcd.print("Manual Config");
    lcd.setCursor(0, 1);
    lcd.print("Connect to AP");
    digitalWrite(WiFiConfigLED, HIGH);
    enterManualConfigMode();
    digitalWrite(WiFiConfigLED, LOW);
  }
}
void checkIfCountIsReset() {
  buttonState = digitalRead(resetCount);
    //if button is pushed, reset the count to zero
    if (buttonState == LOW) {
      objectCount = 0;
      lcd.setCursor(0,1);
      lcd.print("        ");
      lcd.backlight();
      lastCanCrushedTime=currentTime;
      lastDisplayTime = currentTime;
      countIsNew = true;
      displayCurrentRecycleCount(true);
      currentMode=CURRENT_RECYCLE_COUNT;
      saveObjectCount();
      postDataToGoogleSheets();
  }
}
void setup() {
  Serial.begin(115200);
  Serial.println("Initializing...");

  lcd.begin(16, 2);
  lcd.init();
  lcd.backlight();
  
  pinMode(WiFiConfigLED, OUTPUT);
  pinMode(resetWiFi, INPUT_PULLUP);
  pinMode(resetCount,INPUT_PULLUP);
  pinMode(sensorPin, INPUT);
  pinMode(LED, OUTPUT);
  connectToWiFi();
  lcd.clear();
  lcd.print("Connected to:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.SSID());
  delay(2000);
  lcd.clear();
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  preferences.begin("MOSettings", false); // Open preferences with the namespace "MOSettings"
  checkForSerialNumber();
  displaySerial();
  loadObjectCount();
  getConfiguration();
  displayRecycleMessage(true);
  currentMode = RECYCLE_MESSAGE;
  
  // Initialize the DS18B20 sensor
  // sensors.begin();
  
  // Configure NTP client
  timeClient.begin();
  timeClient.update();
}

void loop() {
  checkManualWifiConfig();
  digitalWrite(WiFiConfigLED, LOW);
  currentTime = millis();
  loopStartTime = millis();
  unsigned long currentMillis = millis();
  unsigned long timeElapsed = currentMillis - previousDisplayTime;
  if (digitalRead(sensorPin) == LOW && !objectDetected) {
    lastDisplayTime = currentTime;
    lastCanCrushedTime = currentTime; // Update last crushed time
    digitalWrite(LED, HIGH);
    objectCount++;
    postCount++;
    objectDetected = true;
    countIsNew = true;
    displayCanCrushed(true);
    saveObjectCount();
    previousDisplayTime = currentMillis;
    currentMode = CAN_CRUSHED;
  }
  else if (digitalRead(sensorPin) == HIGH) {
    objectDetected = false;
    digitalWrite(LED, LOW);
    checkIfCountIsReset();
  }
  
  // Check if it's time to turn off the display due to inactivity
  if (currentTime - lastCanCrushedTime >= displayOffDelay && backlightPowerSave) {
    turnOffDisplay();
  }
  
  switch (currentMode) {
      case CAN_CRUSHED:
        if (timeElapsed >= canCrushedDuration) {
          displayDateTime(true);
          previousDisplayTime = currentMillis;
          currentMode = DATE_TIME;
        } else {
          displayCanCrushed(false);
        }
        break;

      case DATE_TIME:
        if (timeElapsed >= timeTemperatureDuration) {
          displayCurrentRecycleCount(true);
          previousDisplayTime = currentMillis;
          currentMode = CURRENT_RECYCLE_COUNT;
        } else {
          displayDateTime(false);
        }
        break;

      case CURRENT_RECYCLE_COUNT:
        if (timeElapsed >= currentRecycleCountDuration) {
          displayRecycleMessage(true);
          previousDisplayTime = currentMillis;
          currentMode = RECYCLE_MESSAGE;
        } else {
          displayCurrentRecycleCount(false);
        }
        break;
        
      case RECYCLE_MESSAGE:
        if (timeElapsed >= recycleMessageDuration) {
          displayDateTime(true);
          previousDisplayTime = currentMillis;
          currentMode = DATE_TIME;
        } else {
          displayRecycleMessage(false);
        }
        break;
        

  }
  delay(10);
  // Calculate loop time
  unsigned long loopTime = millis() - loopStartTime;
  //Serial.println("Loop time: " + String(loopTime) + " ms");
}
