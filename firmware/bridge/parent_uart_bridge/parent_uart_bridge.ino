/*
  =============================================================================
   parent_uart_bridge_nvs.ino — UART→ESP-NOW ブリッジ（送信キュー/NVS/再接続/LED/HB）
  =============================================================================
  - 学生MCUが UART で出す CSV（HDR/DAT）を受け取り、ESP-NOWで子機へ送信。
  - 前回のHDR(fields=..., schema_id)をNVSへ保存→起動直後に復元・即送。
  - 5秒ごとにHDRを再送（途中参加の子機へ自己同期）。
  - 送信キューで瞬間的な詰まりにも強い。in-flight制限/自動再初期化/LED表示あり。
  - ボード例: Seeed XIAO ESP32-C3（D7=RX20, D6=TX21）/他のESP32系も可。
  =============================================================================
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_err.h>
#include <Preferences.h>   // ★ NVS

// ---------- LED設定 ----------
#ifndef LED_PIN
#  ifdef LED_BUILTIN
#    define LED_PIN LED_BUILTIN
#  else
#    define LED_PIN 2   // ←ボードに合わせて変更（XIAO ESP32C3はLED_BUILTINが定義されることが多い）
#  endif
#endif

// ---- 前方宣言（Arduinoの自動プロトタイプ対策）----
struct QueueItem;
static bool queue_push(uint8_t type, const uint8_t* payload, uint16_t len);
static bool queue_pop(QueueItem& out);
static void ensurePeer();
static void tryReinitIfStalled();
static void ledTick();
static void saveSchemaToNVS();
static bool loadSchemaFromNVS();
static void enqueueHDRNow();

// ========= 設定 =========
#define UART_BAUD   115200
#define UART_PORT   Serial1
#ifndef D7
  #define D7 20  // XIAO ESP32C3: RX
#endif
#ifndef D6
  #define D6 21  // XIAO ESP32C3: TX
#endif
#define UART_RX_PIN D7
#define UART_TX_PIN D6

// 子機の STA MAC（必ず子機のSTAアドレス）
const uint8_t peerMac[6] = { 0x58,0xCF,0x79,0xF3,0xC8,0xB4 };
const int CHANNEL = 1;
// #define USE_WIFI_LR

#define LINE_MAX     384
#define PAYLOAD_MAX  220

// ========= プロトコル =========
enum : uint8_t { FT_HDR=1, FT_DATA=2, FT_HB=3 };

struct __attribute__((packed)) NowFrameHdr {
  uint8_t  ver;      // 1
  uint8_t  type;     // 1=HDR, 2=DATA, 3=HB
  uint16_t node_id;
  uint32_t tx_seq;
  uint16_t payload_len;
  uint16_t crc16;    // CCITT
};

struct __attribute__((packed)) PayloadHDR {
  uint16_t schema_id;
  uint16_t fields_len;
  // char fields[fields_len];
};

struct __attribute__((packed)) PayloadDATAHead {
  uint16_t schema_id;
  uint32_t src_seq;  // 学生MCUのシーケンス
  uint32_t t_ms;     // 学生MCUの時刻（ms）
  uint16_t value_count;
  // float values[value_count];
};

struct __attribute__((packed)) PayloadHB {
  uint32_t tx_ms;
  uint16_t q_depth;
  uint16_t inflight;
};

// ========= 送信キュー =========
struct __attribute__((packed)) QueueItem {
  uint8_t  type;      // FT_*
  uint16_t len;
  uint8_t  payload[PAYLOAD_MAX];
};

#define Q_CAP 256
static QueueItem qbuf[Q_CAP];
static uint16_t  q_head=0, q_tail=0;
static uint32_t  q_dropped=0;

static inline uint16_t q_count(){ return (uint16_t)((q_head - q_tail) & (Q_CAP-1)); }

static bool queue_push(uint8_t type, const uint8_t* payload, uint16_t len){
  uint16_t next = (q_head + 1) & (Q_CAP-1);
  if (next == q_tail){ q_tail = (q_tail + 1) & (Q_CAP-1); q_dropped++; }  // 最古を捨てる＝最新優先
  QueueItem& it = qbuf[q_head]; it.type = type; it.len = len; memcpy(it.payload, payload, len); q_head = next; return true;
}
static bool queue_pop(QueueItem& out){
  if (q_head == q_tail) return false;
  out = qbuf[q_tail]; q_tail = (q_tail + 1) & (Q_CAP-1); return true;
}

// ========= ユーティリティ =========
static uint16_t crc16_ccitt(const uint8_t* data, size_t len){
  uint16_t crc = 0xFFFF;
  for(size_t i=0;i<len;i++){ crc ^= (uint16_t)data[i] << 8; for(int b=0;b<8;b++) crc = (crc & 0x8000) ? (crc<<1)^0x1021 : (crc<<1); }
  return crc;
}
static uint16_t hash16_fnv1a(const String& s){
  uint32_t h = 2166136261u; for(size_t i=0;i<s.length();++i){ h ^= (uint8_t)s[i]; h *= 16777619u; } return (uint16_t)((h>>16) ^ (h & 0xFFFF));
}

// ========= 状態 =========
static uint32_t g_tx_seq = 0;
static uint16_t g_schema_id = 0;
static String   g_fields_csv = "";
static bool     g_have_hdr = false;

volatile bool g_send_ok=false, g_send_fail=false;
volatile int  g_inflight=0;
const    int  MAX_INFLIGHT = 6;

static uint32_t last_tx_ok_ms = 0;
static uint32_t last_tx_attempt_ms = 0;
static uint32_t last_hb_ms = 0;
static uint32_t last_hdr_ms = 0;

// 再初期化用バックオフ
static uint32_t next_reinit_allowed_ms = 0;
static uint32_t reinit_backoff_ms = 500;     // 0.5s → 1s → 2s … 8s
static const  uint32_t reinit_backoff_max = 8000;

// NVS
Preferences prefs;

// ========= コールバック =========
void onSend(const uint8_t*, esp_now_send_status_t s){
  if (s==ESP_NOW_SEND_SUCCESS){ g_send_ok=true; last_tx_ok_ms = millis(); }
  else                         { g_send_fail=true; }
  if (g_inflight>0) g_inflight--;
}

// ========= 低レベル送信 =========
static bool tx_now(uint8_t type, const uint8_t* payload, uint16_t plen){
  if (plen > PAYLOAD_MAX) return false;
  if (g_inflight >= MAX_INFLIGHT)  return false;

  NowFrameHdr h{}; h.ver=1; h.type=type; h.node_id=1; h.tx_seq=++g_tx_seq; h.payload_len=plen; h.crc16=0;
  uint8_t buf[sizeof(NowFrameHdr) + PAYLOAD_MAX];
  memcpy(buf, &h, sizeof(h));
  memcpy(buf+sizeof(h), payload, plen);
  ((NowFrameHdr*)buf)->crc16 = crc16_ccitt(buf, sizeof(h)+plen);

  last_tx_attempt_ms = millis();
  esp_err_t e = esp_now_send(peerMac, buf, sizeof(h)+plen);
  if (e != ESP_OK){
    return false;
  }
  g_inflight++;
  return true;
}

// ========= UART行パース → キューへ =========
static bool enqueueHDR(const char* line){
  const char* p = strstr(line, "fields="); if(!p) return false;
  String rest = String(p+7); rest.trim();
  int pos = rest.indexOf("rate="); if (pos>0){ int c = rest.lastIndexOf(',', pos); if (c>=0) rest = rest.substring(0, c); }

  g_fields_csv = rest; g_schema_id  = hash16_fnv1a(g_fields_csv); g_have_hdr = true;
  Serial.printf("[HDR] schema_id=0x%04X fields=%s\n", g_schema_id, g_fields_csv.c_str());

  // 保存して即送
  saveSchemaToNVS();
  enqueueHDRNow();
  return true;
}

static bool enqueueDAT(const char* line){
  if(!g_have_hdr) return false;

  char buf[LINE_MAX]; strncpy(buf, line, LINE_MAX-1); buf[LINE_MAX-1]=0;
  char* save=nullptr; char* tok=strtok_r(buf, ",", &save);
  if(!tok || strcmp(tok,"DAT")!=0) return false;

  tok = strtok_r(nullptr, ",", &save); if(!tok) return false; uint32_t src_seq = strtoul(tok, nullptr, 10);
  tok = strtok_r(nullptr, ",", &save); if(!tok) return false; uint32_t t_ms    = (uint32_t)(atof(tok)+0.5f);

  float values[64]; int n=0;
  while((tok=strtok_r(nullptr, ",", &save)) && n<64){ values[n++] = atof(tok); }

  uint8_t payload[PAYLOAD_MAX]; size_t off=0;
  PayloadDATAHead dh{}; dh.schema_id=g_schema_id; dh.src_seq=src_seq; dh.t_ms=t_ms; dh.value_count=(uint16_t)n;
  memcpy(payload+off, &dh, sizeof(dh)); off+=sizeof(dh);
  memcpy(payload+off, values, n*sizeof(float));   off+= n*sizeof(float);

  return queue_push(FT_DATA, payload, off);
}

// ========= ピア維持 / 再接続 =========
static void ensurePeer(){
  if (!esp_now_is_peer_exist(peerMac)){
    esp_now_peer_info_t p{}; memcpy(p.peer_addr, peerMac, 6);
    p.ifidx = WIFI_IF_STA; p.channel = CHANNEL; p.encrypt = false;
    esp_now_add_peer(&p);
  }
}

static void doReinit(){
  Serial.println("[REINIT] restarting WiFi/ESP-NOW...");
  esp_now_deinit();
  esp_wifi_stop();
  delay(50);
  esp_wifi_start();
  delay(50);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
#ifdef USE_WIFI_LR
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
#endif
  esp_now_init();
  esp_now_register_send_cb(onSend);
  ensurePeer();

  // 起動直後からDATを通せるよう、復元HDRがあれば即送
  if (g_have_hdr) enqueueHDRNow();

  next_reinit_allowed_ms = millis() + reinit_backoff_ms;
  reinit_backoff_ms = min<uint32_t>(reinit_backoff_ms*2, reinit_backoff_max);
}

static void tryReinitIfStalled(){
  const uint32_t now = millis();
  bool link_stalled = (now - last_tx_ok_ms > 2000) && (now - last_tx_attempt_ms < 1000);
  if (link_stalled && now >= next_reinit_allowed_ms){
    doReinit();
  }
  static uint32_t lastPeerChk=0;
  if (now - lastPeerChk > 3000){ ensurePeer(); lastPeerChk = now; }
}

// ========= HB / HDR再送 =========
static void enqueueHB(){
  PayloadHB hb{ (uint32_t)millis(), (uint16_t)q_count(), (uint16_t)g_inflight };
  queue_push(FT_HB, (uint8_t*)&hb, sizeof(hb));
}
static void enqueueHDRNow(){
  if (!g_have_hdr) return;
  const uint16_t len = g_fields_csv.length();
  uint8_t payload[4 + 256];
  PayloadHDR ph{}; ph.schema_id = g_schema_id; ph.fields_len = len;
  memcpy(payload, &ph, sizeof(ph));
  memcpy(payload+sizeof(ph), g_fields_csv.c_str(), len);
  queue_push(FT_HDR, payload, sizeof(ph)+len);
  last_hdr_ms = millis();
}

// ========= NVS =========
static void saveSchemaToNVS(){
  prefs.begin("glink", false);
  prefs.putString("fields", g_fields_csv);
  prefs.putUShort("schema", g_schema_id);
  prefs.end();
  Serial.println("[NVS] schema saved");
}
static bool loadSchemaFromNVS(){
  prefs.begin("glink", true);
  String f  = prefs.getString("fields", "");
  uint16_t s = prefs.getUShort("schema", 0);
  prefs.end();
  if (f.length()==0 || s==0) return false;

  // 再計算で整合チェック
  uint16_t s_calc = hash16_fnv1a(f);
  if (s != s_calc) {
    Serial.printf("[NVS] schema mismatch: stored=0x%04X calc=0x%04X (use calc)\n", s, s_calc);
    s = s_calc;
  }
  g_fields_csv = f; g_schema_id = s; g_have_hdr = true;
  Serial.printf("[NVS] restored schema 0x%04X fields=%s\n", g_schema_id, g_fields_csv.c_str());

  // 復元HDRを即送
  enqueueHDRNow();
  return true;
}

// ========= LED =========
static void ledWrite(bool on){ if (LED_PIN>=0) digitalWrite(LED_PIN, on ? HIGH : LOW); }
static void ledTick(){
  static uint32_t lastBlink=0; static uint8_t phase=0;
  const uint32_t now = millis();
  const bool linked = (now - last_tx_ok_ms) < 2000;
  const bool congested = q_count() > (Q_CAP*3/4);

  if (congested){
    if (now - lastBlink > 120){
      lastBlink = now; phase = (phase+1)%6;
      ledWrite(phase%2==0); // ON-OFF-ON-OFF-ON-長めOFF
    }
    return;
  }
  if (linked){
    ledWrite(true);   // 点灯
  }else{
    if (now - lastBlink > 500){ lastBlink = now; phase ^= 1; ledWrite(phase); } // 1Hz点滅
  }
}

// ========= セットアップ =========
void setup(){
  pinMode(LED_PIN, OUTPUT); ledWrite(false);

  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== PARENT UART BRIDGE (NVS/robust) ===");

  UART_PORT.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.printf("UART: %u bps RX=%d TX=%d\n", UART_BAUD, UART_RX_PIN, UART_TX_PIN);

  WiFi.mode(WIFI_STA);
  ESP_ERROR_CHECK( esp_wifi_start() );
  delay(50);
  ESP_ERROR_CHECK( esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE) );
#ifdef USE_WIFI_LR
  ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR) );
#endif
  WiFi.setSleep(false);
  Serial.printf("My MAC: %s  CH=%d\n", WiFi.macAddress().c_str(), CHANNEL);

  ESP_ERROR_CHECK( esp_now_init() );
  esp_now_register_send_cb(onSend);
  ensurePeer();

  // ★ 前回HDRを復元して即送
  loadSchemaFromNVS();

  last_tx_ok_ms = 0; last_tx_attempt_ms = 0; next_reinit_allowed_ms = millis();
}

// ========= メインループ =========
void loop(){
  // UART 行取り込み（CR/LF両対応・行頭トリム）
  static char line[LINE_MAX]; static size_t idx=0;
  static uint32_t lastStat=0, bytes=0;
  static uint32_t stat_hdr=0, stat_dat=0, stat_tx_ok=0, stat_tx_fail=0;

  while (UART_PORT.available()){
    int c = UART_PORT.read(); if (c<0) break;
    bytes++;
    if (c=='\r' || c=='\n'){
      if (idx>0){
        line[idx]=0; char* s=line; while(*s==' ' || *s=='\t') ++s;
        if      (strncmp(s,"HDR,",4)==0){ if(enqueueHDR(s)) stat_hdr++; }
        else if (strncmp(s,"DAT,",4)==0){ if(enqueueDAT(s)) stat_dat++; }
        idx=0;
      }
      continue;
    }
    if (idx < LINE_MAX-1) line[idx++]=(char)c;
  }

  // ハートビート 1s
  if (millis() - last_hb_ms >= 1000){ enqueueHB(); last_hb_ms = millis(); }

  // HDRの定期再送 5s
  if (g_have_hdr && millis() - last_hdr_ms > 5000){ enqueueHDRNow(); }

  // キューから送信
  if (g_inflight < MAX_INFLIGHT){
    QueueItem it;
    while (g_inflight < MAX_INFLIGHT && queue_pop(it)){
      if (!tx_now(it.type, it.payload, it.len)){
        break; // 送れなければ次周回で再試行
      }
    }
  }

  // 送信結果のカウント
  if (g_send_ok)   { stat_tx_ok++;   g_send_ok=false; }
  if (g_send_fail) { stat_tx_fail++; g_send_fail=false; }

  // 再接続判定
  tryReinitIfStalled();

  // LED
  ledTick();

  // 1秒ごと統計
  if (millis() - lastStat > 1000){
    Serial.printf("[STAT] UART=%lu B/s  Q=%u/%u drop=%lu  HDR=%lu DAT=%lu  TX ok=%lu fail=%lu inflight=%d\n",
      (unsigned long)bytes, (unsigned)q_count(), (unsigned)Q_CAP, (unsigned long)q_dropped,
      (unsigned long)stat_hdr, (unsigned long)stat_dat,
      (unsigned long)stat_tx_ok, (unsigned long)stat_tx_fail, g_inflight);
    bytes=0; stat_hdr=stat_dat=stat_tx_ok=stat_tx_fail=0; lastStat=millis();
  }
}
