// version : 1.0.1
// Update version: change count totalVolume, change count NTP time, reset at 00:00:00 WIB
// - check mqtt server connection at 00:00 sec
// - send data mqtt server at 00:02 sec
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
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200);  // GMT+7 offset in seconds (7 * 3600)

// Pin sensor
#define SENSOR_PIN 3

// Interval dan variabel penghitungan
const unsigned long ONE_MINUTE = 60000;
unsigned long previousMillis = 0;
unsigned long pulseCount = 0;
float totalVolume = 0.0;

// Buffer untuk menyimpan flowRate setiap menit
float flowRateBuffer[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
int bufferIndex = 0;

// Variabel untuk mereset volume setiap tengah malam
bool volumeReset = false;

// Variabel untuk memastikan pengiriman data hanya dilakukan sekali
bool dataSent = false;

// Fungsi untuk menghubungkan ke server MQTT
void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT server...");
    if (client.connect("ArduinoClient", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 4 seconds");
      delay(4000);
    }
  }
}

// Fungsi untuk menghitung pulsa
void countPulse() {
  pulseCount++;
}

// Fungsi untuk menghitung dan menampilkan flow rate
void calculateFlowRate() {
  float frequency = (float)pulseCount / (ONE_MINUTE / 1000.0);  // Frekuensi pulsa dalam Hz
  float flowRate = frequency / 5.5;                             // Flow rate dalam L/min

  // Menambahkan flow rate ke buffer
  flowRateBuffer[bufferIndex] = flowRate;
  bufferIndex = (bufferIndex + 1) % 5;  // Mengatur indeks buffer secara melingkar

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

  // Mengecek koneksi ke MQTT dan mencoba mengirim payload JSON
  if (client.connected()) {
    if (client.publish("sensor_data_flow", jsonBuffer)) {
      Serial.println("Publish succeeded");
    } else {
      Serial.println("Publish failed");
    }
  } else {
    Serial.println("MQTT not connected, attempting to reconnect");
    connectToMQTT();  // Mencoba menghubungkan kembali jika tidak terhubung

    // Mencoba mengirim ulang data setelah koneksi berhasil
    if (client.publish("sensor_data_flow", jsonBuffer)) {
      Serial.println("Publish succeeded after reconnect");
    } else {
      Serial.println("Publish failed after reconnect");
    }
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
  delay(1500);  // Menunggu Ethernet terhubung
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

  client.loop();  // Pastikan client.loop() dipanggil untuk menjaga koneksi MQTT

  // Perhitungan flow rate setiap satu menit
  if (currentMillis - previousMillis >= ONE_MINUTE) {
    previousMillis = currentMillis;
    calculateFlowRate();
  }

  // Mengambil waktu sekarang dari NTP
  timeClient.update();
  unsigned long currentEpochTime = timeClient.getEpochTime();
  unsigned long currentSecond = currentEpochTime % 60;

  // Pemeriksaan koneksi ke MQTT setiap 5 menit pada detik ke-0
  if (currentSecond == 0 && currentEpochTime % 300 == 0) {
    if (!client.connected()) {
      Serial.println("MQTT not connected, attempting to reconnect");
      connectToMQTT();
    }
    dataSent = false;  // Reset flag pengiriman data
  }

  // Pengiriman data ke MQTT setiap 5 menit 2 detik
  if (currentSecond == 2 && currentEpochTime % 300 == 2) {
    if (!dataSent) {
      sendDataToMQTT();
      dataSent = true;  // Set flag pengiriman data agar tidak mengirimkan data lebih dari sekali
    }
  }

  // Reset total volume setiap tengah malam (jam 00:00)
  if (hour(currentEpochTime) == 0 && minute(currentEpochTime) == 0 && !volumeReset) {
    totalVolume = 0;
    volumeReset = true;
    Serial.println("Total Volume reset to 0 at midnight");
  } else if (hour(currentEpochTime) != 0 || minute(currentEpochTime) != 0) {
    volumeReset = false;
  }
}
