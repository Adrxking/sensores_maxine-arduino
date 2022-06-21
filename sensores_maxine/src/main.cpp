#include <Arduino.h>
#include "Colors.h"
#include "IoTicosSplitter.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <DHT.h>

String dId = "121212";
String webhook_pass = "Rxa1UhE9oy";
String webhook_endpoint = "http://178.18.241.48:3001/api/getdevicecredentials";
const char *mqtt_server = "178.18.241.48";

// PINS
#define led 2
#define DHTPIN 2
#define DHTTYPE DHT22

// WiFi
const char *wifi_ssid = "Rx2";
const char *wifi_password = "@drian0303";

// DEFINICIONES DE FUNCIONES
bool get_mqtt_credentials();
void check_mqtt_connection();
bool reconnect();
void process_sensors();
void process_actuators();
void send_data_to_broker();
void callback(char *topic, byte *payload, unsigned int length);
void process_incoming_msg(String topic, String incoming);
void print_stats();
void clear();

// VARIABLES GLOBALES
WiFiClient espclient;
PubSubClient client(espclient);
IoTicosSplitter splitter;
DHT dht(DHTPIN, DHTTYPE); // Inicializar sensor DHT para Arduino 16mhz
long lastReconnectAttemp = 0;
long varsLastSend[20];
String last_received_msg = "";
String last_received_topic = "";
int prev_temp = 0;
int prev_hum = 0;
int chk;
int dif;
float hum;
float temp;

DynamicJsonDocument mqtt_data_doc(2048);

void setup() {
  Serial.begin(921600);
  dht.begin();

  pinMode(led, OUTPUT);
  clear();

  Serial.print(underlinePurple + "\n\n\nConexión WiFi en progreso" + fontReset + Purple);

  WiFi.begin(wifi_ssid, wifi_password);

  int counter = 0;

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    counter++;

    if (counter > 10) {
      Serial.print("  ⤵" + fontReset);
      Serial.print(Red + "\n\n         Ups la conexión WiFi ha fallado :(");
      Serial.println(" -> Reiniciando..." + fontReset);
      delay(2000);
      ESP.restart();
    }
  }

  Serial.print("  ⤵" + fontReset);

  // Mostrando IP Local
  Serial.println(boldGreen + "\n\n         Conexión Wifi Satisfactoria :)" + fontReset);
  Serial.print("\n         IP Local -> ");
  Serial.print(boldBlue);
  Serial.print(WiFi.localIP());
  Serial.println(fontReset);

  client.setCallback(callback);
}

void loop() {
  check_mqtt_connection();
}



// FUNCIONES DE USUARIO ⤵
void process_sensors() {
  // SIMULACIÓN DE TEMPERATURA
  temp= dht.readTemperature();
  mqtt_data_doc["variables"][0]["last"]["value"] = temp;

  // COMPROBAR SI SE DEBE GUARDAR LA TEMPERATURA
  dif = temp - prev_temp;
  
  if (dif < 0) {
    dif *= -1;
  }
  if (dif >= 0.3) {
    mqtt_data_doc["variables"][0]["last"]["save"] = 1;
    prev_temp = temp;
  } else {
    mqtt_data_doc["variables"][0]["last"]["save"] = 0;
  }


  // SIMULACIÓN DE HUMEDAD
  hum = dht.readHumidity();
  mqtt_data_doc["variables"][1]["last"]["value"] = hum;

  // COMPROBAR SI SE DEBE GUARDAR LA HUMEDAD
  dif = hum - prev_hum;

  if (dif < 0) {
    dif *= -1;
  }
  if (dif >= 3) {
    mqtt_data_doc["variables"][1]["last"]["save"] = 1;
    prev_hum = hum;
  } else {
    mqtt_data_doc["variables"][1]["last"]["save"] = 0;
  }


  // OBTENER EL ESTADO DEL LED
  mqtt_data_doc["variables"][4]["last"]["value"] = (HIGH == digitalRead(led));

  delay(2000);
}

void process_actuators() {
  if (mqtt_data_doc["variables"][2]["last"]["value"] == "true") {
    digitalWrite(led, HIGH);
    mqtt_data_doc["variables"][2]["last"]["value"] = "";
    varsLastSend[4] = 0;
  }
  else if (mqtt_data_doc["variables"][3]["last"]["value"] == "false") {
    digitalWrite(led, LOW);
    mqtt_data_doc["variables"][3]["last"]["value"] = "";
    varsLastSend[4] = 0;
  }
}

// PLANTILLA ⤵
void process_incoming_msg(String topic, String incoming){
  last_received_topic = topic;
  last_received_msg = incoming;

  String variable = splitter.split(topic, '/', 2);

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++ ) {
    if (mqtt_data_doc["variables"][i]["variable"] == variable){    
      DynamicJsonDocument doc(256);
      deserializeJson(doc, incoming);
      mqtt_data_doc["variables"][i]["last"] = doc;

      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;
    }
  }

  process_actuators();
}

void callback(char *topic, byte *payload, unsigned int length) {
  String incoming = "";

  for (int i = 0; i < length; i++) {
    incoming += (char)payload[i];
  }

  incoming.trim();

  process_incoming_msg(String(topic), incoming);
}

void send_data_to_broker() {
  long now = millis();

  for (int i = 0; i < mqtt_data_doc["variables"].size(); i++) {
    if (mqtt_data_doc["variables"][i]["variableType"] == "output") {
      continue;
    }

    int freq = mqtt_data_doc["variables"][i]["variableSendFreq"];

    if (mqtt_data_doc["variables"][i]["last"]["save"] == 1) {
      varsLastSend[i] = millis();

      String str_root_topic = mqtt_data_doc["topic"];
      String str_variable = mqtt_data_doc["variables"][i]["variable"];
      String topic = str_root_topic + str_variable + "/sdata";

      String toSend = "";

      serializeJson(mqtt_data_doc["variables"][i]["last"], toSend);

      client.publish(topic.c_str(), toSend.c_str());


      // ESTADISTICAS
      long counter = mqtt_data_doc["variables"][i]["counter"];
      counter++;
      mqtt_data_doc["variables"][i]["counter"] = counter;
    }
  }
}

bool reconnect() {
  if (!get_mqtt_credentials()) {
    Serial.println(boldRed + "\n\n      Error obteniendo credenciales MQTT :( \n\n REINICIANDO EN 10 SEGUNDOS");
    Serial.println(fontReset);
    delay(10000);
    ESP.restart();
  }

  // MQTT Server
  client.setServer(mqtt_server, 1883);

  Serial.print(underlinePurple + "\n\n\nIntentando conexión MQTT" + fontReset + Purple + "  ⤵");

  String str_client_id = "device_" + dId + "_" + random(1, 9999);
  const char *username = mqtt_data_doc["username"];
  const char *password = mqtt_data_doc["password"];
  String str_topic = mqtt_data_doc["topic"];

  if (client.connect(str_client_id.c_str(), username, password)) {
    Serial.print(boldGreen + "\n\n         Cliente MQTT Conectado :) " + fontReset);
    delay(2000);
    client.subscribe((str_topic + "+/actdata").c_str());
    return true;
  }
  else {
    Serial.print(boldRed + "\n\n         Conexión MQTT Fallida :( " + fontReset);
    return false;
  }
}

void check_mqtt_connection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print(Red + "\n\n         Ups Conexión WiFi fallida :( ");
    Serial.println(" -> Reiniciando..." + fontReset);
    delay(15000);
    ESP.restart();
  }

  if (!client.connected()) {
    long now = millis();

    if (now - lastReconnectAttemp > 5000) {
      lastReconnectAttemp = millis();
      if (reconnect()) {
        lastReconnectAttemp = 0;
      }
    }
  } else {
    client.loop();
    process_sensors();
    send_data_to_broker();
    print_stats();
  }
}

bool get_mqtt_credentials() {
  Serial.print(underlinePurple + "\n\n\nObteniendo credenciales MQTT del WebHook" + fontReset + Purple + "  ⤵");
  delay(1000);

  String toSend = "dId=" + dId + "&password=" + webhook_pass;

  HTTPClient http;
  http.begin(webhook_endpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  int response_code = http.POST(toSend);
  if (response_code < 0) {
    Serial.print(boldRed + "\n\n         Error enviando Post Request :( " + fontReset);
    http.end();
    return false;
  }

  if (response_code != 200) {
    Serial.print(boldRed + "\n\n         Error en respuesta :(   e-> " + fontReset + " " + response_code);
    http.end();
    return false;
  }

  if (response_code == 200) {
    String responseBody = http.getString();

    Serial.print(boldGreen + "\n\n         Credenciales MQTT obtenidas satisfactoriamente :) " + fontReset);

    deserializeJson(mqtt_data_doc, responseBody);
    http.end();
    delay(1000);
  }

  return true;
}

void clear() {
  Serial.write(27);
  Serial.print("[2J"); // Comando para limpiar pantalla
  Serial.write(27);
  Serial.print("[H");
}

long lastStats = 0;

void print_stats() {
  long now = millis();

  if (now - lastStats > 2000) {
    lastStats = millis();
    clear();

    Serial.print("\n");
    Serial.print(Purple + "\n╔══════════════════════════════════════╗" + fontReset);
    Serial.print(Purple + "\n║       ESTADISTICAS DEL SISTEMA       ║" + fontReset);
    Serial.print(Purple + "\n╚══════════════════════════════════════╝" + fontReset);
    Serial.print("\n\n");
    Serial.print("\n\n");

    Serial.print(boldCyan + "#" + " \t Nombre" + " \t\t Var" + " \t\t Tipo" + " \t\t Contador" + " \t\t Última V" + fontReset + "\n\n");

    for (int i = 0; i < mqtt_data_doc["variables"].size(); i++) {
      String variableFullName = mqtt_data_doc["variables"][i]["variableFullName"];
      String variable = mqtt_data_doc["variables"][i]["variable"];
      String variableType = mqtt_data_doc["variables"][i]["variableType"];
      String lastMsg = mqtt_data_doc["variables"][i]["last"];
      long counter = mqtt_data_doc["variables"][i]["counter"];

      Serial.println(String(i) + " \t " + variableFullName.substring(0,5) + " \t\t " + variable.substring(0,10) + " \t " + variableType.substring(0,5) + " \t\t " + String(counter).substring(0,10) + " \t\t " + lastMsg);
    }

    Serial.print(boldGreen + "\n\n RAM LIBRE -> " + fontReset + ESP.getFreeHeap() + " Bytes");

    Serial.print(boldGreen + "\n\n Último mensaje -> " + fontReset + last_received_msg);
  }
}