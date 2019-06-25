#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define LED_PIN 5
#define PWR_OFF_PIN 12
#define ADC_PIN A0
//maximale Anzahl der Fehlversuche, bevor das Modul aufgibt
#define MAX_RETRIES 5
//Low Power Modus deaktiviert die Statusled auf der Platine um Energie zu sparen
#define LOW_POWER false
//Leerlaufzeit in Sekunden
#define IDLE_TIME 10

const char* ssid = "ESP8266";
const char* password = "FFEEDDCCBB";
const char* mqtt_server = "mqtt.iot.informatik.uni-oldenburg.de";
const char* payload_topic = "aamn/Briefkasten/1";
char bat_buff[5];
float v_bat = 0;

WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);
IPAddress ipAdresse;
String macAdresse = String(WiFi.macAddress());

bool connect_to_wifi(void);
void print_wifi_info(void);
void connect_to_broker(void);
void print_broker_info(void);
void shutdown(void);
float get_bat_voltage(void);

void setup() {
  //Setze sofort den Pegel von PWR_OFF_PIN auf High, damit das Modul nicht mehr über den Magnetschalter versorgt wird
  pinMode(PWR_OFF_PIN, OUTPUT);
  digitalWrite(PWR_OFF_PIN, HIGH);
  //Init serielle Schnittstelle für Debugmeldungen
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(ADC_PIN, INPUT);
  //Messe Batteriespannung
  v_bat = get_bat_voltage();
  String v_bat_str = String(v_bat);
  v_bat_str.toCharArray(bat_buff, 5);
  //Verbinde mit dem WiFi
  connect_to_wifi();
  //Gebe Informationen zum Netzwerk und Empfangsqualität zurück
  print_wifi_info();
  //Konfiguriere Verbindung zum MQTT Broker
  mqtt_client.setServer(mqtt_server, 1883);
  //Verbinde mit dem Broker
  connect_to_broker();
  //Bestätige den Verbindungsaufbau zum Broker
  print_broker_info();
  mqtt_client.publish(payload_topic, bat_buff);
}

//Anzahl der Versuche den Payload zu versenden
int retries = 0;
//Timestamp des letzten Sendeversuchs
long last_msg = 0;
//Flag um festzustellen, ob der Payload versendet wurde
bool payload_sent = false;

void loop() {
  mqtt_client.loop();
  long now = millis();
  //Wiederhole Kommunikationsversuch zum Broker alle zwei Sekunden
  if (now - last_msg > 2000 && !payload_sent) {
    last_msg = now;
    //Versuche Nachricht zu versenden
    if(mqtt_client.publish(payload_topic, bat_buff)){
        //Falls Payload erfolgreich versendet wurde, kann das Modul sich ausschalten
        Serial.println("[INFO] Payload erfolgreich versendet.");
        payload_sent = true;
        //shutdown();
    } else {
      //Falls Payload nicht versendet werden konnte, merke den Fehlversuch
      //und probiere es in 2 Sekunden nochmal
      Serial.print("[ERROR] Payload konnte nicht versendet werden.");
      retries += 1;
    }
  }
  //sobald alle Versuche verbraucht sind, schaltet sich das Modul aus.
  if(retries == MAX_RETRIES){
    Serial.print("[ERROR] Payload konnte entgültig nicht versendet werden. Gute Nacht.");
    shutdown();
  }
  //Prüfe, ob weiterhin eine Verbindung zum WLAN besteht und unternehme ggf. ein erneuten Verbindungsversuch
  if(WiFi.status() != WL_CONNECTED){
    Serial.print("[ERROR] Verbindung zum WiFi verloren.");
    connect_to_wifi();
  }
  //Prüfe, ob weiterhin eine Verbindung zum Broker besteht und unternehme ggf. ein erneuten Verbindungsversuch
  if(!mqtt_client.connected()){
    Serial.print("[ERROR] Verbindung zum Broker verloren.");
    connect_to_broker();
  }

  //IDLE für 10 Sekunden eingebaut, um für die Zukunft noch Zeit zum empfangen zu haben.
  if(payload_sent && (now - last_msg > IDLE_TIME * 1000)){
      shutdown();
  }

}

/*
 * Gibt nach dem erfolgreichen Verbindungsversuch
 * einige Informationen über das Netzwerk aus
 */
void print_wifi_info(void) {
  String essid = String(WiFi.SSID());
  ipAdresse = WiFi.localIP();
  long signalQuali = WiFi.RSSI();

  Serial.println();
  Serial.println("[INFO] Verbindung aufgebaut zu " + essid);
  Serial.print("[INFO] IP-Adresse ");
  Serial.println(ipAdresse);
  Serial.println("[INFO] Mac-Adresse " + macAdresse);
  Serial.println("[INFO] Signalstärke " + String(signalQuali) + " dBm");
}

/*
  Verbindet mit dem zuvor definierten WiFi Netzwerk
*/
bool connect_to_wifi(void) {
  //setze Stationmode
  WiFi.mode(WIFI_STA);
  //Setze auf 802.11g, da TX Power dann höher ist
  WiFi.setPhyMode(WIFI_PHY_MODE_11B);
  WiFi.begin(ssid, password);
  Serial.println("[INFO] Verbinde mit SSID: " + WiFi.SSID());
  int strikes = 0;
  //Solange nicht verbunden, lasse LED blinken. Sobald verbunden, leuchtet die LED permanent. Falls MAX_RETRIES überschritten, schaltet sich das Modul aus
  while (WiFi.status() != WL_CONNECTED) {

    for (int i = 0; i < 10; i++) {
      //LOW_POWER FLAG deaktiviert LED
      if(!LOW_POWER){
        digitalWrite(LED_PIN, HIGH);
      }
      delay(250);
      digitalWrite(LED_PIN, LOW);
      delay(250);
      Serial.print(".");
    }

    strikes += 1;
    if(strikes > MAX_RETRIES){
      Serial.println();
      Serial.println("[ERROR] Verbindung zum Netzwerk entgültig fehlgeschlagen. Gute Nacht.");
      shutdown();
    }

  }
  Serial.println();
  //LOW_POWER FLAG deaktiviert LED
  if(!LOW_POWER){
    digitalWrite(LED_PIN, HIGH);
  }
  return true;
}

/*
 * Verbindet sich mit dem MQTT Broker
 */
void connect_to_broker(void) {
  Serial.println();
  Serial.print("[INFO] Verbinde mit MQTT Broker");
  int mqtt_strikes = 0;
  while (!mqtt_client.connected()) {
    Serial.print(".");
    //erzeuge Random Client ID, damit sich Geräte mit identischer ID nicht gegenseitig beim Broker rausschmeißen
    String clientId = "Briefkasten-";
    clientId += String(random(0xffff), HEX);
    //wiederhole loop, bis Verbindung zum Broker aufgebaut oder MAX_RETRIES erreicht
    if (mqtt_client.connect(clientId.c_str())) {
      mqtt_strikes = 0;
    } else {
      Serial.println("[ERROR] Verbindung zum Broker fehlgeschlagen. Fehlercode: ");
      Serial.println(mqtt_client.state());
      delay(5000);
      mqtt_strikes++;
    }
    /*
     * Falls sich nach dem fünften Versuch nicht mit dem Broker verbunden werden konnte,
     * schaltet sich das Modul wieder aus, um Energie zu sparen.
     */
    if (mqtt_strikes == MAX_RETRIES) {
      mqtt_strikes = 0;
      Serial.println("[ERROR] Verbindung zum Broker entgültig fehlgeschlagen. Gute Nacht.");
      shutdown();
    }
  }
}

/*
* Bestätigung, dass eine Verbindung zum Broker aufgebaut wurde
*/
void print_broker_info(void) {
  Serial.println();
  Serial.println("[INFO] Verbindung zum MQTT Broker aufgebaut.");
  Serial.print("[INFO] MQTT Broker ");
  Serial.println(mqtt_server);

  Serial.print("[INFO] MQTT Broker Port ");
  Serial.println(1883);
  Serial.println();
}

/*
 * Liest 10x den Wert am ADC aus, berechnet die Batteriespannung
 * und gibt diese dann zurück
 */
float get_bat_voltage(){
  analogRead(ADC_PIN);
  delay(5);
  int adc_in = 0;
  for(int i = 0; i < 10; i++){
      adc_in += analogRead(ADC_PIN);
      delay(5);
  }
  float bat_adc = (float)adc_in/10.0;
  float bat_voltage = (bat_adc/1024.0)*3.3;
  return bat_voltage;
}

//Setzt Gate vom NPN auf Low, damit PMOS wieder sperrt
void shutdown(void){
  Serial.println("[INFO] Shutdown aktiviert. Gute Nacht.");
  digitalWrite(LED_PIN, LOW);
  digitalWrite(PWR_OFF_PIN, LOW);
  //falls Briefkastenklappe noch offen ist, wird DeepSleep aktiviert
  Serial.println("[INFO] Hardware-Shutdown fehlgeschlagen. Leite DeepSleep (Software-Shutdown) ein...");
  ESP.deepSleep(0);
  delay(100);
}
