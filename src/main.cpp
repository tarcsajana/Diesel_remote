#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

// ===== PIN CONFIGURATION =====
#define BUTTON_POWER 1   
#define BUTTON_DOWN  2   
#define BUTTON_UP    3
#define BUTTON_MODE  4
#define LED_PIN 8

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

int buttonPins[4] = {BUTTON_POWER, BUTTON_DOWN, BUTTON_UP, BUTTON_MODE};
const char* buttonNames[4] = {"POWER", "DOWN", "UP", "MODE"};
const char* buttonTopics[4] = {"diesel_heater/power", "diesel_heater/down", "diesel_heater/up", "diesel_heater/mode"};

unsigned long buttonPressTime[4] = {0, 0, 0, 0};  // Time when button was pressed
bool buttonActive[4] = {false, false, false, false};  // Is button currently active
const unsigned long BUTTON_PULSE_TIME = 500;   // 500ms button press simulation

unsigned long lastMqttReconnect = 0;
volatile int pendingButtonPress = -1;  // Flag for pending button press (-1 = none)

// ===== FUNCTIONS =====
void connectWiFi() {
  Serial.print("Connecting WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
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
  } else {
    Serial.println("WiFi FAILED");
    digitalWrite(LED_PIN, LOW);
  }
}

void connectMQTT() {
  if (mqttClient.connected()) return;
  
  Serial.print("MQTT connecting...");
  if (mqttClient.connect(mqttClientId, mqttUser, mqttPass)) {
    Serial.println(" OK");
    mqttClient.publish("diesel_heater/status", "online", true);
    
    // Subscribe to button topics
    mqttClient.subscribe("diesel_heater/power");
    mqttClient.subscribe("diesel_heater/down");
    mqttClient.subscribe("diesel_heater/up");
    mqttClient.subscribe("diesel_heater/mode");
    Serial.println("Subscribed to button topics");
  } else {
    Serial.print(" FAILED (rc=");
    Serial.print(mqttClient.state());
    Serial.println(")");
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
    
    // Process command
    if (strcmp(incomingPacket, "power") == 0) {
      pendingButtonPress = 0;
      Serial.println("-> Triggering POWER button");
    } else if (strcmp(incomingPacket, "down") == 0) {
      pendingButtonPress = 1;
      Serial.println("-> Triggering DOWN button");
    } else if (strcmp(incomingPacket, "up") == 0) {
      pendingButtonPress = 2;
      Serial.println("-> Triggering UP button");
    } else if (strcmp(incomingPacket, "mode") == 0) {
      pendingButtonPress = 3;
      Serial.println("-> Triggering MODE button");
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
      if (elapsed >= BUTTON_PULSE_TIME) {
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
  connectMQTT();
  
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
    if (millis() - lastMqttReconnect > 5000) {
      connectMQTT();
      lastMqttReconnect = millis();
    }
  } else {
    mqttClient.loop();
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
}
