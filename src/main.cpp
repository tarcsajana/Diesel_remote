#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <FirmwareOTA.h>

// ===== PIN CONFIGURATION =====
#define BUTTON_POWER 20   
#define BUTTON_DOWN  7   
#define BUTTON_UP    10
#define BUTTON_MODE  9
#define LED_PIN 8

// ===== DS18B20 PINs =====
#define DS18B20_AIR_PIN 1
#define DS18B20_CAR_PIN 2

// ===== WIFI (hardcoded) =====
const char* ssid = "HunorCar";
const char* password = "Jucymucy8581";
const char* githubToken = "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";  // GitHub PAT for private release download

// ===== MQTT =====
const char* mqttBroker = "kebnekaise.lmq.cloudamqp.com";
const int mqttPort = 1883;
const char* mqttUser = "lpomtbnx:lpomtbnx";
const char* mqttPass = "9htYKaGbPej2YOYyHYGZEbkWdpDdT0Mv";
const char* mqttClientId = "diesel_heater_remote";

// ===== UDP Configuration =====
#define UDP_PORT 4211  // Port to listen for UDP commands from ESP32-S3
WiFiUDP udp;

// ===== GLOBALS =====
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WebServer webServer(80);
FirmwareOTA ota(webServer, mqttClient, "diesel_remote", "Diesel Remote");

// OneWire + DallasTemperature for two sensors
OneWire oneWireAir(DS18B20_AIR_PIN);
DallasTemperature sensorsAir(&oneWireAir);
OneWire oneWireCar(DS18B20_CAR_PIN);
DallasTemperature sensorsCar(&oneWireCar);

int buttonPins[4] = {BUTTON_POWER, BUTTON_DOWN, BUTTON_UP, BUTTON_MODE};
const char* buttonNames[4] = {"POWER", "DOWN", "UP", "MODE"};
const char* buttonTopics[4] = {"diesel_heater/power", "diesel_heater/down", "diesel_heater/up", "diesel_heater/mode"};

unsigned long buttonPressTime[4] = {0, 0, 0, 0};  // Time when button was pressed
bool buttonActive[4] = {false, false, false, false};  // Is button currently active
// Per-button pulse durations: POWER = 2000ms, others = 1000ms
const unsigned long BUTTON_PULSE_POWER_MS = 3000;
const unsigned long BUTTON_PULSE_OTHER_MS = 1000;
unsigned long buttonPulseMs[4] = {BUTTON_PULSE_POWER_MS, BUTTON_PULSE_OTHER_MS, BUTTON_PULSE_OTHER_MS, BUTTON_PULSE_OTHER_MS};

unsigned long lastMqttReconnect = 0;
unsigned long mqttReconnectInterval = 5000;  // start with 5s, back off to 60s max
volatile int pendingButtonPress = -1;  // Flag for pending button press (-1 = none)

// Temperature polling
unsigned long lastTempRead = 0;
const unsigned long TEMP_POLL_INTERVAL = 15000;  // 15 seconds
float lastAirTemp = NAN;
float lastCarTemp = NAN;

// ===== FUNCTIONS =====
void connectWiFi() {
  Serial.print("Connecting WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // Set TX power to -8.5dBm
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    attempts++;
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK - IP: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_PIN, HIGH);
    
    // Start UDP server
    udp.begin(UDP_PORT);
    Serial.print("UDP server started on port ");
    Serial.println(UDP_PORT);
    
    // Start mDNS to advertise as "diesel-heater.local"
    if (!MDNS.begin("diesel-heater")) {
      Serial.println("mDNS start FAILED");
    } else {
      Serial.println("mDNS started - accessible as diesel-heater.local");
      // Optionally add service descriptors
      MDNS.addService("udp", "diesel-heater", UDP_PORT);
    }
  } else {
    Serial.println("WiFi FAILED");
    digitalWrite(LED_PIN, LOW);
  }
}

void connectMQTT() {
  if (mqttClient.connected()) return;
  
  // Don't attempt MQTT connection if WiFi is not connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT: WiFi not connected, skipping MQTT connection");
    return;
  }
  
  Serial.print("MQTT connecting...");
  if (mqttClient.connect(mqttClientId, mqttUser, mqttPass, "diesel_heater/status", 0, true, "offline")) {
    Serial.println(" OK");
    mqttClient.publish("diesel_heater/status", "online", true);
    
    // Subscribe to button topics
    mqttClient.subscribe("diesel_heater/power");
    mqttClient.subscribe("diesel_heater/down");
    mqttClient.subscribe("diesel_heater/up");
    mqttClient.subscribe("diesel_heater/mode");
    // Subscribe to firmware update command topic
    mqttClient.subscribe("diesel_remote/command");
    Serial.println("Subscribed to button and firmware update topics");
  } else {
    Serial.print(" FAILED (rc=");
    Serial.print(mqttClient.state());
    Serial.println(")");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.printf("\n*** MQTT Message Received ***\nTopic: %s\nMessage: %s\n", topic, message.c_str());
  Serial.printf("Message length: %u bytes\n", length);
  
  // Debug: print topic string
  String topicStr(topic);
  Serial.printf("Topic string: '%s', length: %u\n", topicStr.c_str(), topicStr.length());
  
  // Let OTA library handle firmware update commands
  Serial.printf("Calling ota.handleMqttUpdate()...\n");
  if (ota.handleMqttUpdate(topic, message)) {
    Serial.println("****** OTA Update Handled by FirmwareOTA ******\n");
    return;  // OTA handled it
  } else {
    Serial.println("****** OTA handleMqttUpdate returned false ******\n");
  }
  
  // Handle button commands
  if (strcmp(topic, "diesel_heater/power") == 0) {
    pendingButtonPress = 0;
  } else if (strcmp(topic, "diesel_heater/down") == 0) {
    pendingButtonPress = 1;
  } else if (strcmp(topic, "diesel_heater/up") == 0) {
    pendingButtonPress = 2;
  } else if (strcmp(topic, "diesel_heater/mode") == 0) {
    pendingButtonPress = 3;
  }
}

void processUDPCommand() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    char incomingPacket[255];
    int len = udp.read(incomingPacket, 255);
    if (len > 0) {
      incomingPacket[len] = 0;  // Null terminate
    }
    
    Serial.print("UDP received: ");
    Serial.print(incomingPacket);
    Serial.print(" from ");
    Serial.print(udp.remoteIP());
    Serial.print(":");
    Serial.println(udp.remotePort());
    
    // Process command (case-insensitive, trimmed)
    String cmd = String(incomingPacket);
    cmd.trim();
    cmd.toLowerCase();
    if (cmd == "power") {
      pendingButtonPress = 0;
      Serial.println("-> Triggering POWER button");
    } else if (cmd == "down") {
      pendingButtonPress = 1;
      Serial.println("-> Triggering DOWN button");
    } else if (cmd == "up") {
      pendingButtonPress = 2;
      Serial.println("-> Triggering UP button");
    } else if (cmd == "mode") {
      pendingButtonPress = 3;
      Serial.println("-> Triggering MODE button");
    } else if (cmd == "temps") {
      // Reply with latest cached temperatures as JSON
      char out[64];
      snprintf(out, sizeof(out), "{\"air\":%.1f,\"car\":%.1f}", lastAirTemp, lastCarTemp);
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write((const uint8_t*)out, strlen(out));
      udp.endPacket();
      Serial.println("-> Sent temps JSON over UDP");
    } else {
      Serial.println("-> Unknown command");
    }
  }
}

void updateButtons() {
  // Check if any button press duration has expired
  for (int i = 0; i < 4; i++) {
    if (buttonActive[i]) {
      unsigned long elapsed = millis() - buttonPressTime[i];
      if (elapsed >= buttonPulseMs[i]) {
        digitalWrite(buttonPins[i], LOW);  // Release button
        buttonActive[i] = false;
        Serial.print("Button ");
        Serial.print(buttonNames[i]);
        Serial.println(" released");
      }
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== Diesel Heater Remote (UDP + MQTT + mDNS) ===\n");
  
  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  // Buttons as outputs
  for (int i = 0; i < 4; i++) {
    pinMode(buttonPins[i], OUTPUT);
    digitalWrite(buttonPins[i], LOW);  // Keep buttons LOW by default
  }
  
  // WiFi
  connectWiFi();
  
  // MQTT
  mqttClient.setServer(mqttBroker, mqttPort);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(30);
  mqttClient.setBufferSize(256);
  connectMQTT();

  // Configure OTA library
  ota.setVersionInfo("1.0.1", __DATE__ " " __TIME__);
  ota.setMqttTopics(
    "diesel_remote/firmware",      // Firmware info topic
    "diesel_remote/command",       // Update command topic
    "diesel_remote/availability"   // Availability topic
  );
  ota.setFirmwareUrl("https://github.com/tarcsajana/Diesel_remote/releases/latest/download/firmware.bin");
  if (githubToken[0] != '\0') {
    ota.setGitHubToken(githubToken);
  }
  ota.setupWebRoutes();
  webServer.begin();
  Serial.println("Web server started (FirmwareOTA)");

  // DS18B20 sensors
  // Enable weak internal pull-ups (still use external 4.7k to 3.3V)
  pinMode(DS18B20_AIR_PIN, INPUT_PULLUP);
  pinMode(DS18B20_CAR_PIN, INPUT_PULLUP);
  sensorsAir.begin();
  sensorsCar.begin();
  sensorsAir.setResolution(12);
  sensorsCar.setResolution(12);
  
  Serial.println("Ready - Waiting for UDP/MQTT commands\n");
}

// ===== LOOP =====
void loop() {
  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastWiFiAttempt = 0;
    if (millis() - lastWiFiAttempt > 10000) {
      Serial.println("WiFi reconnecting...");
      connectWiFi();
      lastWiFiAttempt = millis();
    }
  }
  
  // MQTT
  if (!mqttClient.connected()) {
    if (millis() - lastMqttReconnect > mqttReconnectInterval) {
      connectMQTT();
      lastMqttReconnect = millis();
      if (mqttClient.connected()) {
        ota.publishFirmwareInfo();
        mqttReconnectInterval = 5000;
      } else {
        if (mqttReconnectInterval < 60000) {
          mqttReconnectInterval = min(mqttReconnectInterval * 2, 60000UL);
        }
      }
    }
  } else {
    mqttClient.loop();
  }
  
  // Handle web server requests
  webServer.handleClient();
  
  // Process UDP commands
  processUDPCommand();
  
  // Update button states (release expired presses)
  updateButtons();
  
  // Handle pending button press from MQTT/UDP callback
  if (pendingButtonPress >= 0 && pendingButtonPress < 4) {
    int btn = pendingButtonPress;
    digitalWrite(buttonPins[btn], HIGH);
    buttonActive[btn] = true;
    buttonPressTime[btn] = millis();
    Serial.print("Button ");
    Serial.print(buttonNames[btn]);
    Serial.println(" activated");
    pendingButtonPress = -1;  // Clear flag
  }
  
  delay(10);  // Reduced delay for faster UDP response

  // Periodically read and publish temperatures
  if (millis() - lastTempRead >= TEMP_POLL_INTERVAL) {
    lastTempRead = millis();
    sensorsAir.requestTemperatures();
    sensorsCar.requestTemperatures();
    lastAirTemp = sensorsAir.getTempCByIndex(0);
    lastCarTemp = sensorsCar.getTempCByIndex(0);
    Serial.print("Temps -> Air: "); Serial.print(lastAirTemp);
    Serial.print(" C, Car: "); Serial.print(lastCarTemp); Serial.println(" C");
    if (mqttClient.connected()) {
      char bufAir[16];
      char bufCar[16];
      snprintf(bufAir, sizeof(bufAir), "%.1f", lastAirTemp);
      snprintf(bufCar, sizeof(bufCar), "%.1f", lastCarTemp);
      mqttClient.publish("diesel_heater/air_temp", bufAir, true);
      mqttClient.publish("diesel_heater/car_temp", bufCar, true);
    }
  }
}
