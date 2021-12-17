#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <InfluxDbClient.h>

#define SENSOR_MAX_VAL 1024
#define MOMENTARY_LED_DUR 200
#define EEPROM_LEN 512

#define AP_SSID "NoiseMonitorAP"
#define AP_PASSWORD "ohmynoize"

#define MEASUREMENT_NAME "noise_level"

WiFiManager wifiManager;

InfluxDBClient client;
Point sensor(MEASUREMENT_NAME);

// lit when setup() done
static const int successConnectLedPin = LED_BUILTIN;

// lit on successful write, unlit otherwise
static const int successWriteLedPin = D5;

// lit when sensor value exceeded threshold 
static const int sensorLedPin = D6;
int sensorLedThreshold = 20;

// analog read measurements
static const int sensorInPin = A0;

// short this pin to ground if you want to reset the settings
static const int resetSettingsPin = D1;

int sensorProcessThreshold = 3;

int sensorLedValue;
int sensorValue = 0;
float dbValue = 0;

unsigned long currMillis = 0;
unsigned long msLastWriteLedOn  = 0; 
unsigned long msLastWrite = 0;
unsigned long writeInterval = 2000;

bool shouldSaveConfig = false;

char influxdb_url[128];
char influxdb_org[64];
char influxdb_bucket[64];
char influxdb_token[64];


void saveConfigCallback () {
  shouldSaveConfig = true;
}


void writeEEPROM() {
    int offset = 0;

    EEPROM.put(offset, influxdb_url);
    offset += sizeof(influxdb_url);
    EEPROM.put(offset, influxdb_org);
    offset += sizeof(influxdb_org);
    EEPROM.put(offset, influxdb_bucket);
    offset += sizeof(influxdb_bucket);
    EEPROM.put(offset, influxdb_token);
    offset += sizeof(influxdb_token);
    
    EEPROM.commit();
}


void readEEPROM() {
    int offset = 0;

    EEPROM.get(offset, influxdb_url);
    offset += sizeof(influxdb_url);
    EEPROM.get(offset, influxdb_org);
    offset += sizeof(influxdb_org);
    EEPROM.get(offset, influxdb_bucket);
    offset += sizeof(influxdb_bucket);
    EEPROM.get(offset, influxdb_token);
    offset += sizeof(influxdb_token); 
}


void clearEEPROM() {
  	for (int i = 0 ; i < EEPROM_LEN ; i++) {
	    EEPROM.write(i, 0);
	  }
    EEPROM.commit();
}

void resetSettings() {
    WiFi.mode(WIFI_STA);  // Needed to apply changes
    WiFi.disconnect(true);  // Forget connection and hopefuly credentials
    WiFi.begin("xxx", "xxx");  // Make sure credentials are scrambled in the end
    clearEEPROM();
    delay(100);
}


void setup() {
  pinMode(successConnectLedPin, OUTPUT);
  pinMode(sensorLedPin, OUTPUT);
  pinMode(successWriteLedPin, OUTPUT);
  pinMode(resetSettingsPin, INPUT_PULLUP);

  digitalWrite(successConnectLedPin, HIGH);

  bool resetOn = (digitalRead(resetSettingsPin) == LOW);

  Serial.begin(115200);
  EEPROM.begin(EEPROM_LEN);

  delay(100);

  if (resetOn) {
    resetSettings();
  }

  WiFiManagerParameter param_influxdb_url(
    "influxdb_url", "InfluxDB url", influxdb_url, 64);
  WiFiManagerParameter param_influxdb_org(
    "influxdb_org", "InfluxDB org", influxdb_org, 32);
  WiFiManagerParameter param_influxdb_bucket(
    "influxdb_bucket", "InfluxDB bucket", influxdb_bucket, 32);
  WiFiManagerParameter param_influxdb_token(
    "influxdb_token", "InfluxDB token", influxdb_token, 32);

  wifiManager.addParameter(&param_influxdb_url);
  wifiManager.addParameter(&param_influxdb_org);
  wifiManager.addParameter(&param_influxdb_bucket);
  wifiManager.addParameter(&param_influxdb_token);
  
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect(AP_SSID, AP_PASSWORD)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.restart();  // reset and try again
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  if (shouldSaveConfig) {
    strcpy(influxdb_url, param_influxdb_url.getValue());
    strcpy(influxdb_org, param_influxdb_org.getValue());
    strcpy(influxdb_bucket, param_influxdb_bucket.getValue());
    strcpy(influxdb_token, param_influxdb_token.getValue());
    writeEEPROM();
  } else {
    readEEPROM();
  }

  EEPROM.end();

  client.setConnectionParams(
    influxdb_url, influxdb_org, influxdb_bucket, influxdb_token);

  digitalWrite(successConnectLedPin, LOW);
}


double getdBValue(int sensorValue) {
 // Always interpret low read as minimum dB noise
  if (sensorValue == 0) {
    return -120.0;
  } else {
    return 20 * log10((float)sensorValue / SENSOR_MAX_VAL);    
  }
}

bool sendMeasurement(double dbValue) {
    sensor.clearFields();
    sensor.addField("volume", dbValue);

    bool success = client.writePoint(sensor);

    if (!success) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    }
    return success;
}


void loop() {
  currMillis = millis();
  sensorValue = analogRead(sensorInPin);
  dbValue = getdBValue(sensorValue);

  sensorLedValue = map(sensorValue, 0, SENSOR_MAX_VAL, 0, 255);
  analogWrite(sensorLedPin, sensorLedValue);

  if (currMillis - msLastWriteLedOn > MOMENTARY_LED_DUR) {
    digitalWrite(successWriteLedPin, LOW);
  }
 
  // Don't bother processing low values
  if (sensorValue <= sensorProcessThreshold) {
    return;
  }

  if (currMillis - msLastWrite > writeInterval) {
    Serial.printf("Writing amplitude %.2g dB\n", dbValue);
    msLastWrite = currMillis;

    int status = WiFi.status();

    if (status != WL_CONNECTED) {
      Serial.printf("Wifi connection lost, status: %d\n", status);
    }

    bool sendSuccess = sendMeasurement(dbValue);

    if (sendSuccess) {
        digitalWrite(successWriteLedPin, HIGH);
        msLastWriteLedOn = currMillis;
    }
  }
  delay(100);
}