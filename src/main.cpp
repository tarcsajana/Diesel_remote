#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

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

volatile int pendingButtonPress = -1;  // Flag for pending button press (-1 = none)

// Temperature polling
unsigned long lastTempRead = 0;
const unsigned long TEMP_POLL_INTERVAL = 30000;  // 30 seconds
float lastAirTemp = NAN;
float lastCarTemp = NAN;

// ===== FUNCTIONS =====
void connectWiFi() {
  // Configure static IP: 192.168.1.102
  IPAddress staticIP(192, 168, 1, 102);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress dns(192, 168, 1, 1);
  
  WiFi.mode(WIFI_STA);
  WiFi.config(staticIP, gateway, subnet, dns);
  
  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
}

void reconnectMQTT() {
  if (mqttClient.connected()) return;
  
  // Don't attempt MQTT connection if WiFi is not connected
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT: WiFi not connected, skipping MQTT connection");
    return;
  }
  
  // Non-blocking reconnection - only try once per call
  static unsigned long lastReconnectAttempt = 0;
  unsigned long currentMillis = millis();
  
  // Only attempt reconnection every 5 seconds
  if (currentMillis - lastReconnectAttempt >= 5000) {
    lastReconnectAttempt = currentMillis;
    
    // Debug WiFi and DNS status
    Serial.print("WiFi Status: ");
    Serial.println(WiFi.status());  // 3 = WL_CONNECTED
    Serial.print("Signal Strength: ");
    Serial.println(WiFi.RSSI());
    
    // Test DNS resolution
    IPAddress resolved;
    Serial.print("Testing DNS resolution for ");
    Serial.println(mqttBroker);
    int dnsResult = WiFi.hostByName(mqttBroker, resolved);
    Serial.print("DNS Result: ");
    Serial.println(dnsResult);  // 1 = success, 0 = failed
    if (dnsResult == 1) {
      Serial.print("Resolved IP: ");
      Serial.println(resolved);
    }
    
    Serial.print("Connecting to MQTT...");
    if (mqttClient.connect(mqttClientId, mqttUser, mqttPass, "diesel_heater/status", 0, true, "offline")) {
      Serial.println("connected");
      mqttClient.publish("diesel_heater/status", "online", true);
      
      // Subscribe to button topics
      mqttClient.subscribe("diesel_heater/power");
      mqttClient.subscribe("diesel_heater/down");
      mqttClient.subscribe("diesel_heater/up");
      mqttClient.subscribe("diesel_heater/mode");
      Serial.println("Subscribed to button topics");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" will retry in 5 seconds");
      // rc=-2 = CONNECT_FAILED, -1 = CONNECT_BAD_PROTOCOL, 0-5 = server rejection
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Simple callback - just check topic and set flag
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
  
  Serial.println("\n\n=== Diesel Heater Remote (UDP + MQTT) ===\n");
  
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
  // Check WiFi connection and retry if disconnected
  static unsigned long lastWiFiCheck = 0;
  static bool wasConnected = false;
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - lastWiFiCheck >= 10000) {  // Retry every 10 seconds
      lastWiFiCheck = millis();
      
      // Detailed WiFi status
      Serial.print("WiFi Status: ");
      Serial.print(WiFi.status());
      Serial.print(" (");
      switch(WiFi.status()) {
        case WL_NO_SSID_AVAIL: Serial.print("SSID not found"); break;
        case WL_CONNECT_FAILED: Serial.print("Connection failed"); break;
        case WL_CONNECTION_LOST: Serial.print("Connection lost"); break;
        case WL_DISCONNECTED: Serial.print("Disconnected"); break;
        case WL_IDLE_STATUS: Serial.print("Idle"); break;
        default: Serial.print("Unknown");
      }
      Serial.println(")");
      
      Serial.print("Retrying WiFi SSID: ");
      Serial.println(ssid);
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      wasConnected = false;
    }
  } else {
    if (!wasConnected) {
      Serial.println("WiFi connected!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      Serial.print("Signal Strength (RSSI): ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
      
      // Start UDP server when WiFi connects
      udp.begin(UDP_PORT);
      Serial.print("UDP server started on port ");
      Serial.println(UDP_PORT);
      
      wasConnected = true;
    }
  }
  
  // MQTT - only if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    if (!mqttClient.connected()) {
      reconnectMQTT();
    } else {
      mqttClient.loop();
    }
  }
  
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
