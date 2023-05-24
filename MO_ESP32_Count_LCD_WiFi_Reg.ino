/*
This board counts products passing by a sensor and posts* the timestamp and current
count to a Google Sheet named TestSheet. If the reset button is pressed, the count
is reset to zero. When the system is started it attempts to connect to the previously
saved WiFi network. If no WiFi connection is available or if the user optionally presses
a button, a new WiFi network can be selected and configured with the appropriate credentials by 
accessing the ESP32's Access Point called RecycleCans. The WiFi credentials are
saved and will be reused the next time the system is started. Instructions to the
end user are displayed on the I2C LCD during WiFi configuration. If a serial number
has not been previously saved, a new serial is requested from the MO registration
service. The serial number for the device is saved in ESP32 preferences. The count
of products is also saved in ESP32 preferences, so if power is lost, the previously
saved count is maintained. 

* Data is posted to Google Sheets after every 10 cans have been crushed.

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
*/
#include <Wire.h>
#include "WiFi.h"
#include "time.h"
#include <HTTPClient.h>
#include <LiquidCrystal_I2C.h>
#include <BluetoothSerial.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <Preferences.h>

const char* ssid = "RecycleCans";
const char* password = "";

#define sensorPin 34
#define resetPin 5
#define LED 2 //Internal LED
#define lcdColumns 16
#define lcdRows 2
#define Trigger_Pin 4 //Pin to trigger the WiFi configuration portal
#define WiFiConfigLED 26
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);
bool objectDetected = false;
int objectCount = 0;
int postCount = 0;
unsigned long lastDisplayTime = 0;

String GOOGLE_SERIAL_SCRIPT_ID = "SerialScriptID";
String GOOGLE_COUNT_SCRIPT_ID = "CountScriptID";
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 19800;
const int   daylightOffset_sec = 0;
bool countIsNew = false;
int buttonState = 0; //Reset count button
String serialNumber;
Preferences preferences;

void setup() {
  Serial.begin(9600);
  Serial.println("Initializing...");

  lcd.begin(16, 2);
  lcd.init();
  lcd.backlight();
  
  pinMode(WiFiConfigLED, OUTPUT);
  pinMode(Trigger_Pin, INPUT_PULLUP);
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
  loadObjectCount();
  showDisplay();
}

void loop() {
  if (digitalRead(Trigger_Pin) == LOW) {
    Serial.println("Switch activated. Entering manual configuration mode.");
    lcd.clear();
    lcd.print("Manual Config");
    lcd.setCursor(0, 1);
    lcd.print("Connect to AP");
    digitalWrite(WiFiConfigLED, HIGH);
    enterManualConfigMode();
    digitalWrite(WiFiConfigLED, LOW);
  }
  digitalWrite(WiFiConfigLED, LOW);
  unsigned long currentTime = millis();
  unsigned long loopStartTime = millis();
  if (digitalRead(sensorPin) == LOW && !objectDetected) {
    lastDisplayTime = currentTime;
    digitalWrite(LED, HIGH);
    objectCount++;
    postCount++;
    objectDetected = true;
    countIsNew = true;
    showDisplay();
    saveObjectCount();
  }
  else if (digitalRead(sensorPin) == HIGH) {
    objectDetected = false;
    digitalWrite(LED, LOW);
    buttonState = digitalRead(resetPin);
    //if button is pushed, reset the count to zero
    if (buttonState == LOW) {
      objectCount = 0;
      lcd.setCursor(0,1);
      lcd.print("        ");
      lastDisplayTime = currentTime;
      countIsNew = true;
      showDisplay();
      saveObjectCount();
    }
  }
  // Show the display for 30 seconds
    if (currentTime - lastDisplayTime <= 30000)  {
    showDisplay();
  }else{
    //Turn power off to the VCC pin of the LCD display by turning off a P channel Mosfet
    turnOffDisplay();
  }
  delay(10);
  // Calculate loop time
  unsigned long loopTime = millis() - loopStartTime;
  //Serial.println("Loop time: " + String(loopTime) + " ms");
}
void showDisplay() {
  // Print the current count and turn on the backlight 
  //lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Product Count");
  lcd.setCursor(0,1);
  lcd.print(objectCount);
  // Turn on the backlight
  lcd.backlight();
  //If the count has changed update Google Sheets
  if (countIsNew == true && postCount == 10) {
    unsigned long postStartTime = millis();
    postDataToGoogleSheets();
    Serial.println("Post time was " + String(millis() - postStartTime) + " Milliseconds");
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
    //Serial.print("Time:");
    //Serial.println(asString);
    String urlFinal = "https://script.google.com/macros/s/"+GOOGLE_COUNT_SCRIPT_ID+"/exec?"+"serial="+serialNumber+"&date="+asString+"&count="+String(objectCount);
    //Serial.print("POST data to spreadsheet:");
    //Serial.println(urlFinal.c_str());
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
  showDisplay();
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
  showDisplay();
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