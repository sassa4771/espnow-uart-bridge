/*
  child_csv_sink_robust.ino — ESP-NOW受信→CSV出力（LED/再初期化/厳格CRC）
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_mac.h>

#ifndef LED_PIN
#  ifdef LED_BUILTIN
#    define LED_PIN LED_BUILTIN
#  else
#    define LED_PIN 2
#  endif
#endif

#define SERIAL_BAUD 115200
const int CHANNEL = 1;      // 親機と一致
// #define USE_WIFI_LR

#define PAYLOAD_MAX  250
#define REQUIRE_HDR  0   // 1にするとHDR未取得時のDATを捨てる

enum : uint8_t { FT_HDR=1, FT_DATA=2, FT_HB=3 };

struct __attribute__((packed)) NowFrameHdr{
  uint8_t ver, type; uint16_t node_id; uint32_t tx_seq; uint16_t payload_len, crc16;
};
struct __attribute__((packed)) PayloadHDR{ uint16_t schema_id, fields_len; };
struct __attribute__((packed)) PayloadDATAHead{ uint16_t schema_id; uint32_t src_seq, t_ms; uint16_t value_count; };
struct __attribute__((packed)) PayloadHB{ uint32_t tx_ms; uint16_t q_depth; uint16_t inflight; };

static uint16_t crc16_ccitt(const uint8_t* d, size_t n){
  uint16_t c=0xFFFF; for(size_t i=0;i<n;i++){ c^=(uint16_t)d[i]<<8; for(int b=0;b<8;b++) c=(c&0x8000)?(c<<1)^0x1021:(c<<1); }
  return c;
}

static volatile uint32_t last_rx_ms = 0;
static bool     got_hdr = false;
static uint16_t expect_schema = 0;

static void onRecv(const esp_now_recv_info* info, const uint8_t* data, int len){
  if(len < (int)sizeof(NowFrameHdr)) return;
  const NowFrameHdr* h = (const NowFrameHdr*)data;
  if(h->ver!=1) return;
  if(sizeof(NowFrameHdr)+h->payload_len != (uint16_t)len) return;

  // 厳格CRC（CRCフィールドを0にして再計算）
  uint8_t tmp[PAYLOAD_MAX + sizeof(NowFrameHdr)];
  if (len > (int)sizeof(tmp)) return;
  memcpy(tmp, data, len); ((NowFrameHdr*)tmp)->crc16 = 0;
  if (crc16_ccitt(tmp, len) != h->crc16) return;

  const uint8_t* p = data + sizeof(NowFrameHdr);
  last_rx_ms = millis();
  digitalWrite(LED_PIN, HIGH); // フラッシュ

  if(h->type==FT_HDR){
    if(h->payload_len < sizeof(PayloadHDR)) return;
    const PayloadHDR* ph=(const PayloadHDR*)p;
    if(h->payload_len < sizeof(PayloadHDR)+ph->fields_len) return;
#if REQUIRE_HDR
    got_hdr = true;
    expect_schema = ph->schema_id;
#endif
    Serial.print("HDR,1,GLDR,fields=");
    for(int i=0;i<ph->fields_len;i++) Serial.write(p+sizeof(PayloadHDR)+i,1);
    Serial.println();

  }else if(h->type==FT_DATA){
    if(h->payload_len < sizeof(PayloadDATAHead)) return;
    const PayloadDATAHead* dh=(const PayloadDATAHead*)p;
    int need = sizeof(PayloadDATAHead) + dh->value_count*sizeof(float);
    if(h->payload_len < need) return;

#if REQUIRE_HDR
    if (!got_hdr) { Serial.println("#WARN no HDR yet; drop DATA"); return; }
    if (dh->schema_id != expect_schema) { Serial.println("#INFO schema changed; wait HDR"); got_hdr=false; return; }
#endif

    const float* vals=(const float*)(p + sizeof(PayloadDATAHead));
    Serial.print("DAT,"); Serial.print(dh->src_seq);
    Serial.print(',');    Serial.print(dh->t_ms);
    for(int i=0;i<dh->value_count;i++){ Serial.print(','); Serial.print(vals[i], 6); }
    Serial.println();

  }else if(h->type==FT_HB){
    // 必要なら可視化：
    // const PayloadHB* hb=(const PayloadHB*)p;
    // Serial.printf("HB,tx_ms=%lu,q=%u,if=%u\n",(unsigned long)hb->tx_ms,hb->q_depth,hb->inflight);
  }
}

static void reinit_now(){
  Serial.println("[REINIT] RX idle too long → restart ESP-NOW");
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
  esp_now_register_recv_cb(onRecv);
}

void setup(){
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);

  Serial.begin(SERIAL_BAUD);
  delay(200);

  WiFi.mode(WIFI_STA);
  ESP_ERROR_CHECK( esp_wifi_start() );
  delay(50);
  ESP_ERROR_CHECK( esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE) );
#ifdef USE_WIFI_LR
  ESP_ERROR_CHECK( esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR) );
#endif
  WiFi.setSleep(false);

  Serial.printf("Child sink start. My MAC (STA): %s  CH=%d\n", WiFi.macAddress().c_str(), CHANNEL);

  ESP_ERROR_CHECK( esp_now_init() );
  esp_now_register_recv_cb(onRecv);
  Serial.println("Waiting ESP-NOW frames...");
}

void loop(){
  uint32_t now = millis();
  static uint32_t lastBlink=0; static bool bl=false;

  if (now - last_rx_ms < 2000){
    digitalWrite(LED_PIN, HIGH);
  }else{
    if (now - lastBlink > 500){ lastBlink = now; bl = !bl; digitalWrite(LED_PIN, bl); }
  }

  // 10秒以上無受信で再初期化
  static uint32_t lastReinitTry=0;
  if (now - last_rx_ms > 10000 && now - lastReinitTry > 10000){
    reinit_now();
    lastReinitTry = now;
  }

  delay(1);
}
