#include <Arduino.h>
#include <WiFi.h>
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

void updateButtons() {
  // Check if any button press duration has expired
  for (int i = 0; i < 4; i++) {
    if (buttonActive[i]) {
      unsigned long elapsed = millis() - buttonPressTime[i];
      if (elapsed >= BUTTON_PULSE_TIME) {
        digitalWrite(buttonPins[i], LOW);  // Release button
        buttonActive[i] = false;
        Serial.print("Button ");
        Serial.print(i);
        Serial.println(" released");
      }
    }
  }
}

// ===== SETUP =====
// Debug function to test all GPIO pins
void debugAllPins() {
  Serial.println("\n\n=== GPIO PIN TEST ===");
  Serial.println("Testing each GPIO pin - should pulse LOW for 1 second each\n");
  
  // Test all common GPIO pins on ESP32-C3
  int testPins[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  int numPins = sizeof(testPins) / sizeof(testPins[0]);
  
  for (int i = 0; i < numPins; i++) {
    int pin = testPins[i];
    Serial.print("Testing GPIO");
    Serial.print(pin);
    Serial.print(" - Setting LOW for 1 second... ");
    
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    delay(100);
    digitalWrite(pin, LOW);   // Pull LOW
    delay(1000);
    digitalWrite(pin, HIGH);  // Release
    
    Serial.println("DONE");
    delay(500);
  }
  
  Serial.println("\n=== GPIO PIN TEST COMPLETE ===\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n=== Diesel Heater Remote ===\n");
  
  // Uncomment the line below to run GPIO pin debug test
   //debugAllPins();
   //return;  // Comment out to skip normal startup
  
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
  
  Serial.println("Ready - Waiting for MQTT commands\n");
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
  
  // Update button states (release expired presses)
  updateButtons();
  
  // Handle pending button press from MQTT callback
  if (pendingButtonPress >= 0 && pendingButtonPress < 3) {
    int btn = pendingButtonPress;
    digitalWrite(buttonPins[btn], HIGH);
    buttonActive[btn] = true;
    buttonPressTime[btn] = millis();
    Serial.print("Button ");
    Serial.print(btn);
    Serial.println(" activated");
    pendingButtonPress = -1;  // Clear flag
  }
  
  delay(50);
}
