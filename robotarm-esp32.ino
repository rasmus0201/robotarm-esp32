#include <ESP32Servo.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <EspMQTTClient.h>
#include <AutoConnect.h>
#include <AutoConnectCredential.h>

// mDNS
String espHostname = "esp";

// MQTT
const char *mqttHostname = "robotarm.local";
String mqttIp = "10.142.106.136"; // 0.0.0.0
const short mqttPort = 1883;
const char *mqttUsername = "robotarm";
const char *mqttPassword = "robotarm";
EspMQTTClient *mqtt = nullptr;

struct RobotServo {
  String id;
  String name;
  int pin;
  int position;
  Servo servo;
};

int NUM_SERVOS = 6;
int SERVO_SPEED = 8;
RobotServo servos[6];

struct Input {
  String type;
  String id;
  int value;
  bool NEW;
};

Input MQTT_INPUT_DEFAULT = { "", "", -1, false };
Input MQTT_INPUT = { "", "", -1, false };

AutoConnect portal;
AutoConnectConfig acConfig;

String getDeviceId()
{
  return "esp-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
}

void setup(void)
{
  Serial.begin(115200);
  delay(500);

  espHostname = getDeviceId();

  // Enable saved past credential by autoReconnect option,
  // even once it is disconnected.
  acConfig.autoReconnect = true;
  acConfig.hostName = espHostname;
  acConfig.apid = espHostname; // Unique AP ssid
  
  portal.config(acConfig);

  if (portal.begin()) {
    if (MDNS.begin(espHostname.c_str())) {
      MDNS.addService("http", "tcp", 80);
    }

    Serial.println("WiFi connected: " + WiFi.localIP().toString());
    Serial.println("Hostname: " + String(WiFi.getHostname()));
    
    //mqttIp = findMDNS(mqttHostname);
    // Serial.println("MQTT IP via mDNS: " + mqttIp);
   
    mqtt = new EspMQTTClient(
      mqttIp.c_str(),     // MQTT Broker server ip
      mqttPort,           // MQTT Broker server port
      mqttUsername,       // Username
      mqttPassword,       // Password
      espHostname.c_str() // Client name that uniquely identify your device
    );

    mqtt->enableMQTTPersistence();
  }

  configureServos();
}

void onConnectionEstablished()
{
  Serial.println("MQTT: Established connection");
  
  mqtt->subscribe("servo/+", [] (const String& topic, const String& message)  {
    String id = String(topic.c_str()); 
    id.replace("servo/", "");
    
    int value = message.toInt();
    bool servoExists = false;

    for(int i = 0; i < NUM_SERVOS; i++) {
      if (servos[i].id == id) {
        servoExists = true;
        break;
      }
    }

    if (servoExists) {
      MQTT_INPUT.type = "SERVO";
      MQTT_INPUT.id = id;
      MQTT_INPUT.value = value;
      MQTT_INPUT.NEW = true;
    }
  });

  mqtt->subscribe("setting/speed", [] (const String &payload)  {
    int value = payload.toInt();

    if (value >= 5 && value <= 50) {
      MQTT_INPUT.type = "SPEED";
      MQTT_INPUT.id = "SPEED";
      MQTT_INPUT.value = value;
      MQTT_INPUT.NEW = true;
    }
  });

  mqtt->publish("event/connected", getDeviceId());
}

void loop()
{
  portal.handleClient();
  
  if (mqtt) {
    mqtt->loop();  
  }

  loopServos();
}

void loopServos()
{
  // Check for incoming data
  if (MQTT_INPUT.NEW == false) {
    return;
  }

  if (MQTT_INPUT.type == "SERVO") {
    for (int i = 0; i < NUM_SERVOS; i++) {
      if (servos[i].id == MQTT_INPUT.id) {
        Serial.print("Updating servo = ");
        Serial.print(servos[i].id);
        Serial.print(", new_pos=");
        Serial.print(MQTT_INPUT.value);
        Serial.print(", old_pos=");
        Serial.print(servos[i].position);
        Serial.println("");
        
        // We use for loops so we can control the speed of the servo
        
        // If previous position is bigger then current position
        if (servos[i].position > MQTT_INPUT.value) {
          // Run servo down
          for (int j = servos[i].position; j >= MQTT_INPUT.value; j--) {
            servos[i].servo.write(j);
            delay(SERVO_SPEED); // defines the speed at which the servo rotates
          }
        }

        // If previous position is smaller then current position
        if (servos[i].position < MQTT_INPUT.value) {
          for (int j = servos[i].position; j <= MQTT_INPUT.value; j++) {
            // Run servo up
            servos[i].servo.write(j);
            delay(SERVO_SPEED);
          }
        }
        
        servos[i].position = MQTT_INPUT.value; // Set current position as previous position
      }
    }
  }
  else if (MQTT_INPUT.type == "SPEED") {
    SERVO_SPEED = MQTT_INPUT.value;
  }

  MQTT_INPUT = MQTT_INPUT_DEFAULT;
}

void configureServos()
{
  RobotServo waist = { "S1", "Waist", 33, 0 };
  RobotServo shoulder = { "S2", "Shoulder", 25, 70 };
  RobotServo elbow = { "S3", "Elbow", 26, 60 };
  RobotServo wristRoll = { "S4", "Wrist roll", 27, 0 };
  RobotServo wristPitch = { "S5", "Wrist pitch", 14, 0 };
  RobotServo gribber = { "S6", "Gribber", 12, 0 };

  servos[0] = waist;
  servos[1] = shoulder;
  servos[2] = elbow;
  servos[3] = wristRoll;
  servos[4] = wristPitch;
  servos[5] = gribber;
  
  for(int i = 0; i < NUM_SERVOS; i++) {
    servos[i].servo.attach(servos[i].pin);
    servos[i].servo.write(servos[i].position);
  }

  delay(500);
}

String findMDNS(String mDnsHost)
{
  // On Ubuntu the equivalent command is: `avahi-resolve-host-name -4 mqtt-broker.local`
  // The input mDnsHost is e.g. "mqtt-broker" from "mqtt-broker.local"
  Serial.println("Finding the mDNS details...");
  
  // Need to make sure that we're connected to the wifi first
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }

  IPAddress serverIp = MDNS.queryHost(mDnsHost);
  int count = 1;
  while (serverIp.toString() == "0.0.0.0") {
    Serial.println("Trying again to resolve mDNS");
    delay(250);
    serverIp = MDNS.queryHost(mDnsHost);

    if (count++ > 8) {
      Serial.println("Could not find " + mDnsHost + " on the network");
      break;
    }
  }
  
  Serial.print("IP address of server: ");
  Serial.println(serverIp.toString());
  Serial.println("Done finding the mDNS details...");
  
  return serverIp.toString();
}
