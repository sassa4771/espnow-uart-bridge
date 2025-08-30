/*
 * ESP-NOW Ping-Pong (ESP32 / Arduino) — Serial 115200出力
 * - INITIATOR: PING送信→PONG受信でRTT(μs)計測
 * - RESPONDER: 受信したPINGに即時PONGで応答
 *
 * 手順:
 *  1) それぞれのESP32のMACをシリアルで確認
 *  2) peerMac[] に「相手側」のMACを設定
 *  3) 片方は ROLE_INITIATOR を有効、もう片方はコメントアウト
 *  4) シリアルモニタ 115200bps で表示
 */

// #define ROLE_INITIATOR                 // ← 片方だけ有効、もう片方はコメントアウト
const uint8_t peerMac[6] = {0x4C,0x75,0x25,0xCD,0xB3,0xA0};  // 相手のMACに変更：
const int CHANNEL = 1;                 // 両端で合わせる
const uint32_t PING_INTERVAL_MS = 500; // 送信周期[ms]

// Long Range（到達性↑/帯域↓）を使うならコメント解除（両端で同設定が必要）
// #define USE_WIFI_LR

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

struct __attribute__((packed)) Msg {
  uint8_t  type;   // 1=PING, 2=PONG
  uint32_t seq;    // シーケンス番号
  uint32_t t0_us;  // 送信時刻(micros)
};
enum { MSG_PING=1, MSG_PONG=2 };

volatile bool sendOk = false, sendFail = false;

#ifdef ROLE_INITIATOR
volatile uint32_t lastRxSeq = 0;
uint32_t seq = 0;
uint32_t okCount = 0, lostCount = 0;
uint32_t txSeqInFlight = 0;  // 直近送信シーケンス（コールバック表示用）
#endif

// 送信完了コールバック
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) sendOk = true;
  else sendFail = true;
}

// 受信コールバック（ESP-IDF v5系の新シグネチャ）
void onDataRecv(const esp_now_recv_info* info, const uint8_t* data, int len) {
  if (len < (int)sizeof(Msg)) return;

  Msg in;
  memcpy(&in, data, sizeof(Msg));

#ifdef ROLE_INITIATOR
  if (in.type == MSG_PONG) {
    uint32_t now = micros();
    uint32_t rtt = now - in.t0_us;
    lastRxSeq = in.seq;
    okCount++;
    Serial.printf("[PONG]  seq=%lu  RTT=%lu us  (ok=%lu, lost=%lu)\n",
                  (unsigned long)in.seq, (unsigned long)rtt,
                  (unsigned long)okCount, (unsigned long)lostCount);
  }
#else
  if (in.type == MSG_PING) {
    // 受け取ったPINGをそのまま返す
    Msg out = in;
    out.type = MSG_PONG;
    esp_now_send(peerMac, (uint8_t*)&out, sizeof(out));

    // デバッグ表示（必要なら）
    Serial.printf("[PING→PONG] from %02X:%02X:%02X:%02X:%02X:%02X  seq=%lu\n",
                  info->src_addr[0], info->src_addr[1], info->src_addr[2],
                  info->src_addr[3], info->src_addr[4], info->src_addr[5],
                  (unsigned long)in.seq);
  }
#endif
}

void printMac(const uint8_t* m) {
  for (int i=0;i<6;i++) {
    if (i) Serial.print(':');
    if (m[i] < 0x10) Serial.print('0');
    Serial.print(m[i], HEX);
  }
}

void ensurePeer(const uint8_t* mac) {
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = CHANNEL;        // 0なら現在のチャネル
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ERR] esp_now_add_peer failed");
  }
}

void setup() {
  Serial.begin(115200);            // ★ 115200bps
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);

  if (CHANNEL > 0) {
    esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
  }

#ifdef USE_WIFI_LR
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
#endif

  if (esp_now_init() != ESP_OK) {
    Serial.println("[FATAL] ESP-NOW init failed");
    while (true) delay(1000);
  }
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);
  ensurePeer(peerMac);

#ifdef ROLE_INITIATOR
  Serial.println("Role: INITIATOR");
#else
  Serial.println("Role: RESPONDER");
#endif

  Serial.print("My MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Peer MAC  : ");
  printMac(peerMac); Serial.println();
  Serial.print("Channel: "); Serial.println(CHANNEL);
}

void loop() {
#ifdef ROLE_INITIATOR
  static uint32_t lastPingMs = 0;
  uint32_t nowMs = millis();

  // 周期PING
  if (nowMs - lastPingMs >= PING_INTERVAL_MS) {
    lastPingMs = nowMs;

    Msg out;
    out.type = MSG_PING;
    out.seq  = ++seq;
    out.t0_us = micros();
    txSeqInFlight = out.seq;

    Serial.printf("[PING]  seq=%lu\n", (unsigned long)out.seq);

    esp_err_t err = esp_now_send(peerMac, (uint8_t*)&out, sizeof(out));
    if (err != ESP_OK) {
      Serial.printf("[SEND ERR] seq=%lu  err=%d\n", (unsigned long)out.seq, err);
    }
  }

  // 送信結果の表示
  if (sendOk)  { Serial.printf("[SEND-OK]   seq=%lu\n", (unsigned long)txSeqInFlight); sendOk=false; }
  if (sendFail){ Serial.printf("[SEND-FAIL] seq=%lu\n", (unsigned long)txSeqInFlight); sendFail=false; }

  // タイムアウト判定（簡易）
  static uint32_t lastCheckedSeq = 0;
  if (seq != lastCheckedSeq) {
    uint32_t watchSeq = seq;
    lastCheckedSeq = seq;

    uint32_t startMs = millis();
    const uint32_t TIMEOUT_MS = 200;   // 必要に応じ調整
    while (millis() - startMs < TIMEOUT_MS) {
      if (lastRxSeq == watchSeq) break; // 返ってきた
      delay(1);
    }
    if (lastRxSeq != watchSeq) {
      lostCount++;
      Serial.printf("[TIMEOUT]  seq=%lu  (ok=%lu, lost=%lu)\n",
                    (unsigned long)watchSeq,
                    (unsigned long)okCount, (unsigned long)lostCount);
    }
  }
#else
  // 応答側は待機
  delay(10);
#endif
}
