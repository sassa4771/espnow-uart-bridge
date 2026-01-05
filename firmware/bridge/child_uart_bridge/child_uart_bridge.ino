/*
  child_csv_sink_pass_all.ino
  PCからの全コマンドを親機へスルーパスする版
*/
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#ifndef LED_PIN
#  ifdef LED_BUILTIN
#    define LED_PIN LED_BUILTIN
#  else
#    define LED_PIN 2
#  endif
#endif

#define SERIAL_BAUD 115200
const int CHANNEL = 1;

// プロトコル定義
enum : uint8_t { FT_HDR=1, FT_DATA=2, FT_HB=3, FT_LOG=4 };
struct __attribute__((packed)) NowFrameHdr{
  uint8_t ver, type; uint16_t node_id; uint32_t tx_seq; uint16_t payload_len, crc16;
};

// ==========================================
// ★設定: 親機(機体側)のMACアドレス
// もし不明な場合は {0xFF,0xFF,...} (ブロードキャスト) でも動きますが、
// 特定できているならそのアドレスを書いてください。
const uint8_t parentMac[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; 
// ==========================================

// 宛先が特定されているかチェック
bool isBroadcast = true;

void onRecv(const uint8_t* mac_addr, const uint8_t* data, int len);

void setup(){
  pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, LOW);
  Serial.begin(SERIAL_BAUD);
  delay(200);

  // MACアドレスがオール0ならブロードキャスト扱いにする判定
  isBroadcast = true;
  for(int i=0; i<6; i++) { if(parentMac[i]!=0) isBroadcast=false; }
  // ※コード上で直接 parentMac を {0xFF...} にしている場合も考慮
  if(parentMac[0]==0xFF) isBroadcast=true;

  WiFi.mode(WIFI_STA);
  ESP_ERROR_CHECK( esp_wifi_start() );
  esp_wifi_set_max_tx_power(84); 

  ESP_ERROR_CHECK( esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE) );
  WiFi.setSleep(false);

  ESP_ERROR_CHECK( esp_now_init() );
  esp_now_register_recv_cb(onRecv);
  
  // ピア登録
  esp_now_peer_info_t p{}; 
  if (isBroadcast) {
     const uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
     memcpy(p.peer_addr, bc, 6);
  } else {
     memcpy(p.peer_addr, parentMac, 6);
  }
  p.channel = CHANNEL; 
  p.ifidx = WIFI_IF_STA; 
  p.encrypt = false;
  
  if (esp_now_add_peer(&p) != ESP_OK) {
    Serial.println("Failed to add peer");
  } else {
    Serial.println("Peer Added");
  }

  Serial.println("Child Sink Ready (Pass-All)");
}

void sendCmdToParent(char c){
  uint8_t buf[2] = { 0xFE, (uint8_t)c };
  if (isBroadcast) {
    const uint8_t bc[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    esp_now_send(bc, buf, 2);
  } else {
    esp_now_send(parentMac, buf, 2);
  }
}

void loop(){
  // USBシリアルからの入力
  if (Serial.available()){
    char c = Serial.read();
    
    // ★ここが重要: 'A'や'M'だけでなく、改行以外すべての文字を通す
    if (c != '\r' && c != '\n') {
       sendCmdToParent(c); 
       
       // フィードバックLED (ごく短く点灯)
       digitalWrite(LED_PIN, HIGH);
       delay(10);
       digitalWrite(LED_PIN, LOW);
    }
  }
  delay(1);
}

// 受信処理 (変更なし)
void onRecv(const uint8_t* mac, const uint8_t* data, int len){
  if(len < (int)sizeof(NowFrameHdr)) return;
  const NowFrameHdr* h = (const NowFrameHdr*)data;
  const uint8_t* p = data + sizeof(NowFrameHdr);
  
  // データ受信時点灯
  digitalWrite(LED_PIN, HIGH);
  
  if(h->type==FT_HDR){
    Serial.print("HDR,1,GLDR,fields=");
    for(int i=4; i<h->payload_len; i++) Serial.write(p[i]);
    Serial.println();
  }else if(h->type==FT_DATA){
    uint32_t src_seq; memcpy(&src_seq, p+2, 4);
    uint32_t t_ms;    memcpy(&t_ms,    p+6, 4);
    uint16_t count;   memcpy(&count,   p+10,2);
    const float* vals = (const float*)(p + 12);

    Serial.print("DAT,"); Serial.print(src_seq);
    Serial.print(',');    Serial.print(t_ms);
    for(int i=0;i<count;i++){ Serial.print(','); Serial.print(vals[i], 3); }
    Serial.println();
  }else if(h->type==FT_LOG){
    Serial.write(p, h->payload_len);
    Serial.println();
  }
  digitalWrite(LED_PIN, LOW);
}