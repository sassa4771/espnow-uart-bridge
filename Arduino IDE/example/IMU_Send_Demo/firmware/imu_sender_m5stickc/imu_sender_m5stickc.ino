// M5StickC IMU → ESP-NOW 送信（50Hz）
// * M5StickC Plus の場合は #include <M5StickCPlus.h> に変更
#include <M5StickC.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

struct __attribute__((packed)) Telemetry {
  uint32_t seq;
  uint32_t t_us;   // micros() at TX
  float ax, ay, az;
  float gx, gy, gz;
};

const uint8_t peerMac[6] = { 0x4C,0x75,0x25,0xCD,0xB3,0xA0 }; // ★受信側(Atom)のSTA MACに置換
const int CHANNEL = 1;                  // ★両端で一致させる
const uint32_t HZ = 50;                 // 送信周波数
const uint32_t PERIOD_US = 1000000UL / HZ;

volatile bool sendOk=false, sendFail=false;
uint32_t seq=0, okCnt=0, failCnt=0;

void onDataSent(const uint8_t*, esp_now_send_status_t s){
  if(s==ESP_NOW_SEND_SUCCESS){ sendOk=true; okCnt++; }
  else { sendFail=true; failCnt++; }
}

void ensurePeer(const uint8_t* mac){
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr, mac, 6);
  p.ifidx = WIFI_IF_STA;
  p.channel = CHANNEL;   // 固定チャネル
  p.encrypt = false;
  esp_err_t e = esp_now_add_peer(&p);
  Serial.printf("add_peer: %s\n", esp_err_to_name(e));
}

void setup(){
  M5.begin();            // IMU/I2C初期化含む
  M5.IMU.Init();

  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFi.setSleep(false);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_max_tx_power(80); // 20 dBm（地域規制に注意）

  Serial.printf("My MAC (STA): %s\n", WiFi.macAddress().c_str());
  Serial.printf("Peer MAC    : %02X:%02X:%02X:%02X:%02X:%02X\n",
    peerMac[0],peerMac[1],peerMac[2],peerMac[3],peerMac[4],peerMac[5]);
  Serial.printf("Channel     : %d\n", CHANNEL);

  ESP_ERROR_CHECK(esp_now_init());
  esp_now_register_send_cb(onDataSent);
  ensurePeer(peerMac);
}

void loop(){
  static uint32_t next_us = micros();
  uint32_t now = micros();
  if ((int32_t)(now - next_us) < 0) return;
  next_us += PERIOD_US;

  float ax, ay, az, gx, gy, gz;
  M5.IMU.getAccelData(&ax, &ay, &az);
  M5.IMU.getGyroData (&gx, &gy, &gz);

  Telemetry t;
  t.seq = ++seq;
  t.t_us = micros();
  t.ax=ax; t.ay=ay; t.az=az;
  t.gx=gx; t.gy=gy; t.gz=gz;

  esp_err_t e = esp_now_send(peerMac, (uint8_t*)&t, sizeof(t));

  // 1秒ごとに統計を出す
  static uint32_t lastPrint=millis();
  if (millis()-lastPrint >= 1000){
    lastPrint=millis();
    Serial.printf("[TX] seq=%lu  send=%s  ok=%lu  fail=%lu\n",
      (unsigned long)seq, esp_err_to_name(e),
      (unsigned long)okCnt,(unsigned long)failCnt);
    sendOk=sendFail=false;y
  }
}