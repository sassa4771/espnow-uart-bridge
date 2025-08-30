/*
  XIAO-ESP32-C3 MACアドレス確認 (STA/AP両方 + EFUSE直読み)
  - 依存: esp32 Arduino core v3.x
  - ボード: Seeed XIAO_ESP32C3 または ESP32C3 Dev Module
  - シリアル: 115200 bps
*/

#include <Arduino.h>
#include <WiFi.h>       // ← これが無いと WiFi が未定義
#include <esp_wifi.h>   // esp_wifi_get_mac / WIFI_IF_STA/AP
#include <esp_system.h> // esp_read_mac
#include <esp_mac.h>    // ESP_MAC_WIFI_STA / ESP_MAC_WIFI_SOFTAP

static void printMac(const char* tag, const uint8_t m[6]) {
  Serial.printf("%s %02X:%02X:%02X:%02X:%02X:%02X\n",
                tag, m[0],m[1],m[2],m[3],m[4],m[5]);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== XIAO-ESP32-C3 MAC CHECK ===");

  // 1) WiFiドライバ経由（STA/AP）
  WiFi.mode(WIFI_STA);                 // STAインタフェースを有効化
  delay(50);

  uint8_t sta1[6]={}, ap1[6]={};
  esp_wifi_get_mac(WIFI_IF_STA, sta1);
  esp_wifi_get_mac(WIFI_IF_AP,  ap1);  // AP起動前でも読めることが多い

  printMac("STA (esp_wifi_get_mac):", sta1);
  printMac("AP  (esp_wifi_get_mac):", ap1);

  Serial.printf("STA (WiFi.macAddress):      %s\n", WiFi.macAddress().c_str());
  Serial.printf("AP  (WiFi.softAPmacAddress): %s\n", WiFi.softAPmacAddress().c_str());

  // 2) EFUSE直読み（WiFiを起動しなくても読める）
  uint8_t sta2[6]={}, ap2[6]={};
  esp_read_mac(sta2, ESP_MAC_WIFI_STA);
  esp_read_mac(ap2,  ESP_MAC_WIFI_SOFTAP);
  printMac("STA (esp_read_mac EFUSE):", sta2);
  printMac("AP  (esp_read_mac EFUSE):", ap2);

  Serial.println("\n※ 親機のpeer登録には通常 STA MAC を使います。");
}

void loop() {
  delay(1000);
}
