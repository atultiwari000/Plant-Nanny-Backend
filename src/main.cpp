#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define USER_EMAIL "1atultiwari062@gmail.com"
#define USER_PASSWORD "XXXXXXXX"
#define WIFI_SSID "XXXXXXXXX"
#define WIFI_PASSWORD "XXXXXXXXXXXX"
#define API_KEY "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define DATABASE_URL "XXXXXXXXXXXXXXXXXXXXXXXXX"

String thingspeakapiKey = "XXXXXXXXXXXXXXXX";
const char* server = "api.thingspeak.com";

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

bool isMotorOn = false;
bool autoMotorOnOff = true;
bool manualMode = false;
int soilMoistureValue = 0;

#define DHTPIN 4
#define DHTTYPE DHT11
#define RELAY_PIN 14
#define MOISTURE_PIN 34
#define thresholdValue 3500

DHT dht(DHTPIN, DHTTYPE);
WiFiClient client;

// for this setup the relay was on active low state
// need to change the code for active high


void setMotor(bool isOn) {
    Serial.print("setMotor called with: ");
    Serial.println(isOn ? "ON" : "OFF");
    if (isMotorOn != isOn) {
        isMotorOn = isOn;
        digitalWrite(RELAY_PIN, isMotorOn ? LOW : HIGH); // Note: LOW for ON, HIGH for OFF
        Serial.print("Setting motor to ");
        Serial.println(isMotorOn ? "ON" : "OFF");

        if (Firebase.ready() && signupOK) {
            if (Firebase.RTDB.setBool(&fbdo, "App/motor/isOn", isOn)) {
                Serial.println("PASSED: Motor state updated to Firebase");
            } else {
                Serial.println("FAILED: Motor state update failed");
                Serial.println("REASON: " + fbdo.errorReason());
            }
            if (Firebase.RTDB.setBool(&fbdo, "App/motor/manualMode", manualMode)) {
                Serial.println("PASSED: Manual mode state updated to Firebase");
            } else {
                Serial.println("FAILED: Manual mode state update failed");
                Serial.println("REASON: " + fbdo.errorReason());
            }
        }
    }
}

void setCurrentMoistureValue(int value) {
    soilMoistureValue = value;
    if (Firebase.ready() && signupOK) {
        Firebase.RTDB.setInt(&fbdo, "App/soilMoisture/currentValue", value);
    }
}

void setTempAndHumidity(float temp, float hum) {
    if (Firebase.ready() && signupOK) {
        FirebaseJson json;
        json.add("temperature", temp);
        json.add("humidity", hum);
        if (Firebase.RTDB.setJSON(&fbdo, "App/environment", &json))
        {
            Serial.println("PASSED: Temperature and humidity data updated to Firebase");
        }
        else
        {
            Serial.println("FAILED: Temperature and humidity data update failed");
            Serial.println("REASON: " + fbdo.errorReason());
        }

    }
}

String callAPI(const String &url) {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(url);
        int httpResponseCode = http.GET();

        if (httpResponseCode > 0) {
            String payload = http.getString();
            http.end();
            return payload;
        }

        http.end();
        return "Error: " + String(httpResponseCode);
    }
    return "WiFi Disconnected";
}

String getDate() {
    return callAPI("http://worldtimeapi.org/api/timezone/Asia/Kathmandu");
}

void setMoistureDataWithDateAndTimeAsJson(int value) {
    if (Firebase.ready() && signupOK) {
        FirebaseJson json;
        json.add("value", value);
        json.add("datetime", getDate());
        Firebase.RTDB.pushJSON(&fbdo, "App/soilMoisture/wholeData", &json);
    }
}

void readData() {
    if (Firebase.RTDB.getBool(&fbdo, "App/motor/manualMode")) {
        manualMode = fbdo.boolData();
    }

    if (manualMode && Firebase.RTDB.getBool(&fbdo, "App/motor/isOn")) {
        bool motorStateFromDB = fbdo.boolData();
        if (motorStateFromDB != isMotorOn) {
            setMotor(motorStateFromDB); 
        }
    }

    int value = analogRead(MOISTURE_PIN);
    if (value != soilMoistureValue) {
        soilMoistureValue = value;
        setCurrentMoistureValue(soilMoistureValue);
        setMoistureDataWithDateAndTimeAsJson(soilMoistureValue);
    }

    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (isnan(temp) || isnan(hum)) {
        Serial.println("Failed to read from DHT sensor!");
    } else {
        Serial.print("Temperature: ");
        Serial.print(temp);
        Serial.print(" °C, Humidity: ");
        Serial.print(hum);
        Serial.println(" %");
        setTempAndHumidity(temp, hum);
    }

    delay(2000); // Add delay to allow DHT sensor to stabilize
}


void connectToWiFi() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
    }
}

void setupFirebase() {
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.database_url = DATABASE_URL;

    if (Firebase.signUp(&config, &auth, USER_EMAIL, USER_PASSWORD)) {
        signupOK = true;
    } else {
        if (config.signer.signupError.message == "EMAIL_EXISTS") {
            signupOK = true;
        }
    }

    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
}

void autoLogic() {
    if (!manualMode) {
        if (soilMoistureValue > thresholdValue) {
            Serial.println("Soil is dry. Turning motor ON by turning relay OFF.");
            setMotor(false);  // Relay OFF (LOW), Motor ON
        } else {
            Serial.println("Soil is wet. Turning motor OFF by turning relay ON.");
            setMotor(true); // Relay ON (HIGH), Motor OFF
        }
    } else {
        Serial.println("Manual mode is ON. Ignoring auto logic.");
    }
}

void sendDataToThingSpeak(float temp, float hum, int soilMoisture) {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip;
        if (WiFi.hostByName(server, ip)) {
            if (client.connect(ip, 80)) {
                String postStr = "api_key=" + thingspeakapiKey;
                postStr += "&field1=" + String(temp);
                postStr += "&field2=" + String(hum);
                postStr += "&field3=" + String(soilMoisture);
                postStr += "\r\n\r\n";

                client.print("POST /update HTTP/1.1\n");
                client.print("Host: " + String(server) + "\n");
                client.print("Connection: close\n");
                client.print("Content-Type: application/x-www-form-urlencoded\n");
                client.print("Content-Length: ");
                client.print(String(postStr.length()));
                client.print("\n\n");
                client.print(postStr);

                unsigned long timeout = millis();
                while (client.available() == 0) {
                    if (millis() - timeout > 5000) {
                        client.stop();
                        return;
                    }
                }

                while (client.available()) {
                    client.readStringUntil('\r');
                }

                client.stop();
            }
        }
    }
}

void setup() {
    delay(2000);
    Serial.begin(921600);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);  // Ensure motor is off initially
    dht.begin();
    connectToWiFi();
    setupFirebase();
}

void loop() {
    if (Firebase.ready() && signupOK) {
        if (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0) {
            sendDataPrevMillis = millis();
            readData();
            autoLogic();
            sendDataToThingSpeak(dht.readTemperature(), dht.readHumidity(), soilMoistureValue);
        }
    } else {
        Serial.println("Firebase not ready or signup not OK");
    }

    // Check for changes in Firebase
    if (Firebase.RTDB.getBool(&fbdo, "App/motor/manualMode")) {
        manualMode = fbdo.boolData();
    }
    if (Firebase.RTDB.getBool(&fbdo, "App/motor/isOn")) {
        bool motorStateFromDB = fbdo.boolData();
        if (motorStateFromDB != isMotorOn) {
            setMotor(motorStateFromDB);
        }
    }

    delay(1000); // Small delay to avoid overwhelming the loop
}

