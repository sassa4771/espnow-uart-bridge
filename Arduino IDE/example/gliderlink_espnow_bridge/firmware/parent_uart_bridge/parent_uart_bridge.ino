/* parent_uart_bridge.ino
 * 機体マイコン(UART) → 親機ESP → ESP-NOW送信
 * - UART: HDR/DAT（CSV）を受信
 * - 無線: NowFrame（バイナリ）で送信（HDR/ DATA）
 * Arduino Core v3 / ESP-IDF v5 対応
 */
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ========= 設定 =========
#define UART_BAUD   115200
// UART1 を使用（例: RX=GPIO16, TX=GPIO17） ※ボードに合わせて変更
#define UART_PORT   Serial1
#define UART_RX_PIN 16
#define UART_TX_PIN 17

const uint8_t peerMac[6] = { 0x24,0x6F,0x28,0xAA,0xBB,0xCC }; // ★子機のSTA MACに変更
const int CHANNEL = 1;                 // ★子機と一致
// #define USE_WIFI_LR                    // LRを使う場合は両端で有効化

#define LINE_MAX  384                  // 1行最大長（必要なら拡張）
#define PAYLOAD_MAX  220               // ESP-NOW payload上限に余裕を持たせる

// ========= フレーム定義 =========
struct __attribute__((packed)) NowFrameHdr {
  uint8_t  ver;       // 1
  uint8_t  type;      // 1=HDR, 2=DATA
  uint16_t node_id;   // 任意（例: 1）
  uint32_t tx_seq;    // 親機の送信seq
  uint16_t payload_len;
  uint16_t crc16;     // CCITT
};

// type=HDR payload
struct __attribute__((packed)) PayloadHDR {
  uint16_t schema_id;
  uint16_t fields_len;
  // char fields[fields_len];
};

// type=DATA payload（ヘッダ部）
struct __attribute__((packed)) PayloadDATAHead {
  uint16_t schema_id;
  uint32_t src_seq;
  uint32_t t_ms;
  uint16_t value_count;
  // float values[value_count];
};

// ========= グローバル =========
static uint32_t g_tx_seq = 0;
static uint16_t g_schema_id = 0;
static String   g_fields_csv = "";  // "ax,ay,az,..."
static bool     g_have_hdr = false;

volatile bool g_send_ok=false, g_send_fail=false;

uint16_t crc16_ccitt(const uint8_t* data, size_t len){
  uint16_t crc = 0xFFFF;
  for(size_t i=0;i<len;i++){
    crc ^= (uint16_t)data[i] << 8;
    for(int b=0;b<8;b++){
      if(crc & 0x8000) crc = (crc<<1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

// 簡易16bitハッシュ（FNV-1aの16bit縮約）
uint16_t hash16_fnv1a(const String& s){
  uint32_t h = 2166136261u;
  for(size_t i=0;i<s.length();++i){
    h ^= (uint8_t)s[i];
    h *= 16777619u;
  }
  return (uint16_t)((h >> 16) ^ (h & 0xFFFF));
}

void onSend(const uint8_t*, esp_now_send_status_t s){
  if(s==ESP_NOW_SEND_SUCCESS) g_send_ok=true; else g_send_fail=true;
}

void ensurePeer(const uint8_t* mac){
  if (esp_now_is_peer_exist(mac)) return;
  esp_now_peer_info_t p{};
  memcpy(p.peer_addr, mac, 6);
  p.ifidx = WIFI_IF_STA;
  p.channel = CHANNEL;
  p.encrypt = false;
  esp_err_t e = esp_now_add_peer(&p);
  Serial.printf("add_peer: %s\n", esp_err_to_name(e));
}

bool sendFrame(uint8_t type, const uint8_t* payload, uint16_t plen){
  NowFrameHdr h{};
  h.ver = 1;
  h.type = type;
  h.node_id = 1;
  h.tx_seq = ++g_tx_seq;
  h.payload_len = plen;
  h.crc16 = 0; // いったん0で計算

  uint8_t buf[sizeof(NowFrameHdr) + PAYLOAD_MAX];
  if (plen > PAYLOAD_MAX) return false;
  memcpy(buf, &h, sizeof(h));
  memcpy(buf+sizeof(h), payload, plen);
  uint16_t crc = crc16_ccitt(buf, sizeof(h)+plen);
  ((NowFrameHdr*)buf)->crc16 = crc;

  esp_err_t e = esp_now_send(peerMac, buf, sizeof(h)+plen);
  if(e!=ESP_OK){
    Serial.printf("esp_now_send: %s\n", esp_err_to_name(e));
    return false;
  }
  return true;
}

bool parseHDR(const char* line){
  // 例: HDR,1,GLDR,fields=ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50
  // fields= の後を抽出
  const char* p = strstr(line, "fields=");
  if(!p) return false;
  p += 7;
  // 行末までがフィールド群。末尾の改行や追加パラメータは無視せず、そのまま取り出し後にカンマ区切り領域だけに切る
  String rest = String(p);
  // 末尾の改行等を除去
  rest.trim();
  // もし ",rate=" のような追加パラメータが続くならそこまでを切り出し
  int pos = rest.indexOf("rate=");
  if (pos>0){
    // 一つ前のカンマまで戻す
    int commaBefore = rest.lastIndexOf(',', pos);
    if (commaBefore>=0) rest = rest.substring(0, commaBefore);
  }
  // 保存
  g_fields_csv = rest;
  g_schema_id = hash16_fnv1a(g_fields_csv);
  g_have_hdr = true;
  Serial.printf("[HDR] schema_id=0x%04X fields=%s\n", g_schema_id, g_fields_csv.c_str());

  // 送信（type=HDR）
  uint16_t len = g_fields_csv.length();
  uint8_t payload[4 + 256];
  PayloadHDR ph{}; ph.schema_id = g_schema_id; ph.fields_len = len;
  memcpy(payload, &ph, sizeof(ph));
  memcpy(payload+sizeof(ph), g_fields_csv.c_str(), len);
  return sendFrame(1, payload, sizeof(ph)+len);
}

bool parseDAT(const char* line){
  // 例: DAT,12345,1740.12,-9.801,0.031,...
  if(!g_have_hdr) return false;

  // トークン分解
  // 注意: strtokは入力を破壊するので、コピーして使う
  char buf[LINE_MAX]; strncpy(buf, line, LINE_MAX-1); buf[LINE_MAX-1]=0;
  char* saveptr=nullptr;
  char* tok = strtok_r(buf, ",", &saveptr);
  if(!tok || strcmp(tok,"DAT")!=0) return false;

  // src_seq
  tok = strtok_r(nullptr, ",", &saveptr); if(!tok) return false;
  uint32_t src_seq = strtoul(tok, nullptr, 10);

  // t_ms
  tok = strtok_r(nullptr, ",", &saveptr); if(!tok) return false;
  float t_ms_f = atof(tok);
  uint32_t t_ms = (uint32_t)(t_ms_f+0.5f);

  // 残りの値を float に詰める
  float values[64];
  int n=0;
  while((tok = strtok_r(nullptr, ",", &saveptr)) && n < 64){
    values[n++] = atof(tok);
  }
  uint16_t value_count = (uint16_t)n;

  // ペイロード構築
  uint8_t payload[PAYLOAD_MAX];
  PayloadDATAHead dh{};
  dh.schema_id = g_schema_id;
  dh.src_seq   = src_seq;
  dh.t_ms      = t_ms;
  dh.value_count = value_count;

  size_t off=0;
  memcpy(payload+off, &dh, sizeof(dh)); off+=sizeof(dh);
  memcpy(payload+off, values, value_count*sizeof(float)); off+= value_count*sizeof(float);

  return sendFrame(2, payload, off);
}

void setup(){
  Serial.begin(115200);
  delay(200);
  UART_PORT.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  Serial.printf("Parent bridge start. UART:%d bps RX=%d TX=%d\n", UART_BAUD, UART_RX_PIN, UART_TX_PIN);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true,true);
  WiFi.setSleep(false);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
#ifdef USE_WIFI_LR
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
#endif
  Serial.printf("My MAC (STA): %s  CH=%d\n", WiFi.macAddress().c_str(), CHANNEL);

  if (esp_now_init()!=ESP_OK){
    Serial.println("esp_now_init failed"); while(true) delay(1000);
  }
  esp_now_register_send_cb(onSend);
  ensurePeer(peerMac);

  Serial.println("Waiting UART HDR/DAT lines...");
}

void loop(){
  static char line[LINE_MAX];
  static size_t idx=0;

  // UARTから行読み
  while(UART_PORT.available()){
    int c = UART_PORT.read();
    if(c=='\r') continue;
    if(c=='\n'){
      line[idx]=0;
      if(idx>0){
        if(strncmp(line,"HDR,",4)==0) parseHDR(line);
        else if(strncmp(line,"DAT,",4)==0) parseDAT(line);
      }
      idx=0;
    }else{
      if(idx<LINE_MAX-1) line[idx++]=(char)c;
    }
  }

  // 送信結果のログ（任意）
  if(g_send_ok){ Serial.println("[TX] OK"); g_send_ok=false; }
  if(g_send_fail){ Serial.println("[TX] FAIL"); g_send_fail=false; }
}
