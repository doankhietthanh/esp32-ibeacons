#include "main.h"
#include "env.h"

#define BLE_SCAN_TIME 5
#define LED_ACTIVE 12
#define BTN_INTERRUPT_AP_MODE 23

FirebaseAuth auth;
FirebaseConfig config;
FirebaseData fbdo;

BLEScan *pBLEScan;

unsigned long sendDataPrevMillis = 0;

String roomId = "";
JsonDocument tagsDoc;

WiFiManager wifiMn;

volatile bool resetWifiFlag = false;

void IRAM_ATTR itrResetWifiSettings()
{
  resetWifiFlag = true;
}

void blinkLED(int ledGipo, int count = 5, int interval = 500)
{
  for (int i = 0; i < count; i++)
  {
    digitalWrite(LED_ACTIVE, HIGH);
    delay(interval);
    digitalWrite(LED_ACTIVE, LOW);
    delay(interval);
  }
}

void resetWifiSettings()
{
  // Blink LED
  blinkLED(LED_ACTIVE);
  // Reset WiFi settings
  wifiMn.resetSettings();
  esp_restart();
  Serial.println("WiFi settings reset");
}

void setup()
{
  Serial.begin(9600);
  pinMode(LED_ACTIVE, OUTPUT);
  pinMode(BTN_INTERRUPT_AP_MODE, INPUT_PULLUP);                                                // Configure the pin as an input with internal pull-up resistor
  attachInterrupt(digitalPinToInterrupt(BTN_INTERRUPT_AP_MODE), itrResetWifiSettings, RISING); // Configure the interrupt

  // Check if the device is connected to Wi-Fi
  if (WiFi.status() == WL_CONNECTED)
  {
    digitalWrite(LED_ACTIVE, HIGH);
  }
  else
  {
    blinkLED(LED_ACTIVE);
  }
  // Generate a unique SSID for the ESP32
  // Split STATION_ID by '-' and get the last element
  std::string WiFiSSID = "Station-" + std::string(STATION_ID).substr(std::string(STATION_ID).find_last_of("-") + 1);
  if (!wifiMn.autoConnect(WiFiSSID.c_str()))
  {
    Serial.println("Failed to connect and hit timeout");
    esp_restart();
    delay(1000);
  }

  Serial.println("Connected to Wi-Fi!");

  /* Assign the api key (required) */
  config.api_key = API_KEY;

  /* Assign the RTDB URL (required) */
  config.database_url = DATABASE_URL;

  /* Sign in with email and password */
  auth.user.email = FIREBASE_AUTH_EMAIL;
  auth.user.password = FIREBASE_AUTH_PASSWORD;

  /* Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); // create new scan
  pBLEScan->setActiveScan(true);   // active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99); // less or equal setInterval value
}

void loop()
{
  if (resetWifiFlag)
  {
    resetWifiSettings();
    resetWifiFlag = false;
  }

  if (Firebase.ready() && (millis() - sendDataPrevMillis > 15000 || sendDataPrevMillis == 0))
  {
    sendDataPrevMillis = millis();

    // Get Room ID
    Serial.println("Room ID: ");
    Serial.println(roomId);

    if (roomId == "" && Firebase.RTDB.getString(&fbdo, "stations/" + std::string(STATION_ID) + "/room"))
    {
      Serial.println("Try get room ID");
      if (fbdo.dataType() == "string")
      {
        roomId = fbdo.stringData();
        Serial.println("Got room ID successfully!");
      }
    }

    // Get list tag
    Serial.println("Tags: ");
    serializeJsonPretty(tagsDoc, Serial);

    if (roomId != "" && tagsDoc.isNull())
    {
      if (Firebase.RTDB.getJSON(&fbdo, "rooms/" + std::string(roomId.c_str()) + "/tags"))
      {
        Serial.println("Try get list tag");
        if (fbdo.dataType() == "json")
        {
          Serial.println("\n---------");
          JsonDocument doc;
          deserializeJson(doc, fbdo.jsonObject().raw());
          serializeJsonPretty(doc, Serial);

          // Add tag data into doc
          for (JsonPair kv : doc.as<JsonObject>())
          {
            const char *tagId = kv.key().c_str();
            const char *macAdress = kv.value()["macAddress"];
            Serial.println("---------");
            tagsDoc[serialized(macAdress)] = serialized(tagId);
          }

          serializeJsonPretty(tagsDoc, Serial);
          Serial.println("---------");
          Serial.println("Get list tag successfully!");
        }
      }
    }

    // Scan BLE devices
    Serial.println("Scanning...");
    BLEScanResults foundDevices = pBLEScan->start(BLE_SCAN_TIME, false);
    Serial.print("Devices found: ");
    Serial.println(foundDevices.getCount());
    Serial.println("Scan done!");
    // Add devices to Firebase
    for (int i = 0; i < foundDevices.getCount(); i++)
    {
      BLEAdvertisedDevice advertisedDevice = foundDevices.getDevice(i);
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
      String deviceAddress = advertisedDevice.getAddress().toString().c_str();
      String tagId = tagsDoc[deviceAddress];

      if (tagId == "null" || tagId == "" || tagId == NULL)
      {
        continue;
      }

      int deviceRSSI = advertisedDevice.getRSSI();
      int deviceTxPower = advertisedDevice.getTXPower();
      FirebaseJson *json = new FirebaseJson();
      json->set("txPower", deviceTxPower);
      json->set("rssi", deviceRSSI);
      Firebase.RTDB.setJSON(&fbdo, "rooms/" + std::string(roomId.c_str()) + "/tags/" + std::string(tagId.c_str()) + "/stations/" + STATION_ID, json);
      Serial.println("Add device to Firebase successfully!");
    }
    // Delete devices from BLE scan results
    pBLEScan->clearResults();
  }
}