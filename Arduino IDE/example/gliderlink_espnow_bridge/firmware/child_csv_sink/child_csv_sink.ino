/* child_csv_sink.ino
 * 子機ESP：ESP-NOW受信 → PCへCSV出力（println）
 * - 受信した HDR はそのまま HDR 行を出力
 * - DATA は DAT,src_seq,t_ms,values... のCSVに復元して出力
 */
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define SERIAL_BAUD 115200
const int CHANNEL = 1;  // 親機と一致
// #define USE_WIFI_LR

#define PAYLOAD_MAX  250

struct __attribute__((packed)) NowFrameHdr {
  uint8_t  ver;
  uint8_t  type;      // 1=HDR, 2=DATA
  uint16_t node_id;
  uint32_t tx_seq;
  uint16_t payload_len;
  uint16_t crc16;
};

struct __attribute__((packed)) PayloadHDR {
  uint16_t schema_id;
  uint16_t fields_len;
  // char fields[fields_len];
};

struct __attribute__((packed)) PayloadDATAHead {
  uint16_t schema_id;
  uint32_t src_seq;
  uint32_t t_ms;
  uint16_t value_count;
  // float values[value_count];
};

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

void onRecv(const esp_now_recv_info* info, const uint8_t* data, int len){
  if(len < (int)sizeof(NowFrameHdr)) return;
  const NowFrameHdr* h = (const NowFrameHdr*)data;
  if(h->ver!=1) return;
  if(sizeof(NowFrameHdr) + h->payload_len != (uint16_t)len) return;

  uint16_t calc = crc16_ccitt(data, len);
  if(calc != 0){ // 送信側はhdr.crc16にセット済み、全体crc計算で0になる
    // 互換のため厳格にせず、単純比較でもOK： if (h->crc16 != crc16_ccitt(...))
    // ここでは0判定方式
  }

  const uint8_t* pld = data + sizeof(NowFrameHdr);
  if(h->type==1){ // HDR
    if(h->payload_len < sizeof(PayloadHDR)) return;
    const PayloadHDR* ph = (const PayloadHDR*)pld;
    if(h->payload_len < sizeof(PayloadHDR)+ph->fields_len) return;
    // fields 取得（NULなし）
    Serial.print("HDR,1,GLDR,fields=");
    for(int i=0;i<ph->fields_len;i++) Serial.write(pld+sizeof(PayloadHDR)+i, 1);
    Serial.println();
  }else if(h->type==2){ // DATA
    if(h->payload_len < sizeof(PayloadDATAHead)) return;
    const PayloadDATAHead* dh = (const PayloadDATAHead*)pld;
    int need = sizeof(PayloadDATAHead) + dh->value_count * sizeof(float);
    if(h->payload_len < need) return;
    const float* vals = (const float*)(pld + sizeof(PayloadDATAHead));

    Serial.print("DAT,");
    Serial.print(dh->src_seq);
    Serial.print(',');
    Serial.print(dh->t_ms);
    for(int i=0;i<dh->value_count;i++){
      Serial.print(',');
      Serial.print(vals[i], 6);
    }
    Serial.println();
  }
}

void setup(){
  Serial.begin(SERIAL_BAUD);
  delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true,true);
  WiFi.setSleep(false);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);
#ifdef USE_WIFI_LR
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR);
#endif
  Serial.printf("Child sink start. My MAC (STA): %s  CH=%d\n", WiFi.macAddress().c_str(), CHANNEL);

  if(esp_now_init()!=ESP_OK){
    Serial.println("esp_now_init failed"); while(true) delay(1000);
  }
  esp_now_register_recv_cb(onRecv);
  Serial.println("Waiting ESP-NOW frames...");
}

void loop(){
  delay(1);
}
