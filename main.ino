/**
 * dip56245 - IoT termometer
 * 
 * used: https://github.com/tzapu/WiFiManager
 * https://github.com/knolleary/pubsubclient
 */

#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <OneWire.h>


#define WELCOME_TEXT  "\n-=[ 20170421_IoT_ds18b20 (c) dip56245 ]=\n                  __________________________________________________\n           ____.-\"\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\":\"-.\n          (___:===='==='==='==='==='==='==='=A '   '   '   '   '   '   )\n          jgs `'-._92____94____96____8__|_100____2_____4_____6_____8.-`"
#define BUTTON_PIN 12
#define ONE_WIRE_BUS 13

char mqtt_server[40] = "192.168.0.1";
char mqtt_port[6] = "1883";
char mqtt_interval[5] = "600";
bool shouldSaveConfig = false;

OneWire  ds(ONE_WIRE_BUS);
WiFiClient wifiClient;
PubSubClient client(wifiClient);

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void callback(char* topic, byte* payload, unsigned int length) {}

String macToStr(const uint8_t* mac){
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

void setup() {
  Serial.begin(115200);
  Serial.println(WELCOME_TEXT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFiManager wifiManager;
  if (digitalRead(BUTTON_PIN)==false){
    Serial.print("RESETING Data....");
    wifiManager.resetSettings();
    SPIFFS.format();
    Serial.println("ok");
    delay(1000);
  }
  wifiManager.setBreakAfterConfig(true);

  if (SPIFFS.begin()){
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_interval, json["mqtt_interval"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_interval("interval", "send data interval (seconds)", mqtt_interval, 5);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_interval);
  
  if (!wifiManager.autoConnect("IOTTEMP", "password")) {
    Serial.println("failed to connect, we should reset as see if it connects");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_interval, custom_mqtt_interval.getValue());
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_interval"] = mqtt_interval;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }
  Serial.println("connected...yeey :)");
  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  //connect to mqtt
  // Generate client name based on MAC address and last 8 bits of microsecond counter
  String clientName;
  clientName += "esp8266-";
  uint8_t mac[6];
  WiFi.macAddress(mac);
  clientName += macToStr(mac);
  clientName += "-";
  clientName += String(micros() & 0xff, 16);

  client.setServer(mqtt_server, atoi(mqtt_port));
  Serial.print("Connecting to ");
  Serial.print(mqtt_server);
  Serial.print(" as ");
  Serial.println(clientName);
  
  if (client.connect((char*) clientName.c_str())) {
    Serial.println("Connected to MQTT broker");
  }
  else {
    Serial.println("MQTT connect failed");
    Serial.println("Will reset and try again...");
    abort();
  }
}

void findAndPost(){
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];

  while(ds.search(addr)){ 
    Serial.print("ROM =");
    String romcode = "/temp/";
    for( i = 0; i < 8; i++) {
      Serial.write(' ');
      Serial.print(addr[i], HEX);
      romcode = romcode + String(addr[i], HEX);
    }

    if (OneWire::crc8(addr, 7) != addr[7]) {
        Serial.println("CRC is not valid!");
        return;
    }
    Serial.println();
 
    switch (addr[0]) {
      case 0x10:
        Serial.println("  Chip = DS18S20");  // or old DS1820
        type_s = 1;
        break;
      case 0x28:
        Serial.println("  Chip = DS18B20");
        type_s = 0;
        break;
      case 0x22:
        Serial.println("  Chip = DS1822");
        type_s = 0;
        break;
      default:
        Serial.println("Device is not a DS18x20 family device.");
        return;
    } 

    ds.reset();
    ds.select(addr);
    ds.write(0x44, 1);        // start conversion, with parasite power on at the end
    
    delay(1000);     // maybe 750ms is enough, maybe not
    // we might do a ds.depower() here, but the reset will take care of it.
    
    present = ds.reset();
    ds.select(addr);    
    ds.write(0xBE);         // Read Scratchpad

    Serial.print("  Data = ");
    Serial.print(present, HEX);
    Serial.print(" ");
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
      Serial.print(data[i], HEX);
      Serial.print(" ");
    }
    Serial.print(" CRC=");
    Serial.print(OneWire::crc8(data, 8), HEX);
    Serial.println();

    int16_t raw = (data[1] << 8) | data[0];
    if (type_s) {
      raw = raw << 3; // 9 bit resolution default
      if (data[7] == 0x10) {
        // "count remain" gives full 12 bit resolution
        raw = (raw & 0xFFF0) + 12 - data[6];
      }
    } else {
      byte cfg = (data[4] & 0x60);
      // at lower res, the low bits are undefined, so let's zero them
      if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
      else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
      else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
      //// default is 12 bit resolution, 750 ms conversion time
    }

    //convert RAW Temperature to String
    //String raw_temp = String(raw, DEC);
    int celsius = (float)raw/16.0;
    String raw_temp = String(celsius, DEC);
  
    if (client.connected()){
      Serial.print("Sending payload: ");
      Serial.println(raw_temp);
    }
      if (client.publish((char*) romcode.c_str(), (char*) raw_temp.c_str())) {
        Serial.println("Publish ok");
      } else {
        Serial.println("Publish failed");
      }
      delay(1000);
  }
}

void loop() {
  findAndPost();
  Serial.println("Deep sleep...");
  ESP.deepSleep(atoi(mqtt_interval)*1000000);
  Serial.println("error sleep");
}
