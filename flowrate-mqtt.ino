#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <EthernetUdp.h>
#include <TimeLib.h>

// Konfigurasi server MQTT
const char* mqtt_server = "192.168.1.14";
const int mqtt_port = 1883;
const char* mqtt_user = "admin";
const char* mqtt_password = "admin";

// Alamat MAC untuk Ethernet shield
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// Inisialisasi Ethernet client dan MQTT client
EthernetClient ethClient;
PubSubClient client(ethClient);

// Inisialisasi NTP client
EthernetUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200); // GMT+7 offset in seconds (7 * 3600)

// Pin sensor
#define SENSOR_PIN 3

// Interval dan variabel penghitungan
const unsigned long ONE_MINUTE = 60000;
const unsigned long FIVE_MINUTES = 300000;
unsigned long previousMillis = 0;
unsigned long previousSendMillis = 0;
unsigned long pulseCount = 0;
float totalVolume = 0.0;

// Buffer untuk menyimpan flowRate setiap menit
float flowRateBuffer[5] = {0.0, 0.0, 0.0, 0.0, 0.0};
int bufferIndex = 0;

// Fungsi untuk menghubungkan ke server MQTT
void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ArduinoClient", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// Fungsi untuk menghitung pulsa
void countPulse() {
  pulseCount++;
}

// Fungsi untuk menghitung dan menampilkan flow rate
void calculateFlowRate() {
  float frequency = (float)pulseCount / (ONE_MINUTE / 1000.0); // Frekuensi pulsa dalam Hz
  float flowRate = frequency / 5.5; // Flow rate dalam L/min

  // Menambahkan flow rate ke buffer
  flowRateBuffer[bufferIndex] = flowRate;
  bufferIndex = (bufferIndex + 1) % 5; // Mengatur indeks buffer secara melingkar

  // Menghitung volume yang terukur dalam satu menit
  float volume = flowRate;

  // Menambahkan volume ke total volume
  totalVolume += volume;

  // Menampilkan flow rate dan total volume di Serial Monitor
  Serial.print("Flow Rate: ");
  Serial.print(flowRate);
  Serial.println(" L/min");

  Serial.print("Total Volume: ");
  Serial.print(totalVolume);
  Serial.println(" L");

  // Reset pulse count untuk periode berikutnya
  pulseCount = 0;
}

// Fungsi untuk mengirim data ke server MQTT
void sendDataToMQTT() {
  // Mengambil waktu sekarang dari NTP dan mengonversi ke string
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  char dateTime[25];
  sprintf(dateTime, "%04d-%02d-%02d %02d:%02d:%02d", year(epochTime), month(epochTime), day(epochTime), hour(epochTime), minute(epochTime), second(epochTime));

  // Menghitung rata-rata flow rate dari buffer
  float averageFlowRate = 0.0;
  for (int i = 0; i < 5; i++) {
    averageFlowRate += flowRateBuffer[i];
  }
  averageFlowRate /= 5.0;

  // Menyiapkan payload JSON
  StaticJsonDocument<512> doc;
  doc["flowRate"] = averageFlowRate;
  doc["totalVolume"] = totalVolume;
  doc["updated_at"] = dateTime;

  char jsonBuffer[512];
  size_t n = serializeJson(doc, jsonBuffer);

  // Menampilkan JSON payload ke Serial Monitor untuk debugging
  Serial.print("JSON Payload: ");
  Serial.println(jsonBuffer);
  Serial.print("Payload Size: ");
  Serial.println(n);

  // Mengirimkan payload JSON ke topik 'data_sensor_flow'
  if (client.publish("sensor_data_flow", jsonBuffer)) {
    Serial.println("Publish succeeded");
  } else {
    Serial.println("Publish failed");
    Serial.print("MQTT Client State: ");
    Serial.println(client.state());
  }

  // Reset buffer setelah pengiriman
  for (int i = 0; i < 5; i++) {
    flowRateBuffer[i] = 0.0;
  }
  bufferIndex = 0;
}

void setup() {
  pinMode(SENSOR_PIN, INPUT);
  Serial.begin(9600);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), countPulse, RISING);

  // Inisialisasi Ethernet dengan DHCP
  Ethernet.begin(mac);
  delay(1500); // Menunggu Ethernet terhubung
  Serial.print("IP Address: ");
  Serial.println(Ethernet.localIP());

  // Inisialisasi MQTT client
  client.setServer(mqtt_server, mqtt_port);

  // Menghubungkan ke server MQTT
  connectToMQTT();

  // Inisialisasi NTP client
  timeClient.begin();
  timeClient.update();
}

void loop() {
  unsigned long currentMillis = millis();

  if (!client.connected()) {
    connectToMQTT();
  }
  client.loop();  // Pastikan client.loop() dipanggil untuk menjaga koneksi MQTT

  // Perhitungan flow rate setiap satu menit
  if (currentMillis - previousMillis >= ONE_MINUTE) {
    previousMillis = currentMillis;
    calculateFlowRate();
  }

  // Pengiriman data ke MQTT setiap lima menit
  if (currentMillis - previousSendMillis >= FIVE_MINUTES) {
    previousSendMillis = currentMillis;
    sendDataToMQTT();
  }
}
