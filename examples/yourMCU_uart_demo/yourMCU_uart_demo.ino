/*
  =============================================================================
   esp32-wroom-32 Student UART Demo (HDR+DAT/CSV 出力) — 時間情報(dt_ms)追加版
  =============================================================================
  ■ 追加点
    - サンプリング間隔の実測値 dt_ms を values に追加。
      出力: DAT,<seq>,<t_ms>,<dt_ms>,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp
    - fields= も dt_ms を先頭に追加（並び順がスキーマIDの元になるため）。
  =============================================================================
*/

#include <Arduino.h>

// --- 外部機器向けのUART（親機ESPへ出す） ---
HardwareSerial& OUT = Serial1;
const int OUT_TX = 23;   // 学生側 TX ピン（例）
const int OUT_RX = -1;   // 受信不要なら -1
const unsigned OUT_BAUD = 115200;

// ローカルデバッグ用（USBシリアル／UART0）
const unsigned DBG_BAUD = 115200;

const uint32_t HZ = 50;
const uint32_t PERIOD_MS = 1000 / HZ;
uint32_t seq = 0;
unsigned long t0 = 0;

void setup() {
  Serial.begin(DBG_BAUD);                       // USBモニタ（UART0）
  OUT.begin(OUT_BAUD, SERIAL_8N1, OUT_RX, OUT_TX);  // 外部UART（TXのみでもOK）

  delay(200);
  // ヘッダは1回以上出す：親機がスキーマを把握
  // ★ dt_ms を先頭に追加しました
  const char* hdr =
    "HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50";
  Serial.println(hdr);  // デバッグにも表示
  OUT.println(hdr);     // 親機へ送信

  t0 = millis();
}

void loop() {
  static unsigned long next_ms = 0;
  static unsigned long prev_ms = 0;  // 前回送信の時刻
  unsigned long now = millis();
  if (now < next_ms) return;
  next_ms = now + PERIOD_MS;

  // サンプリング間隔（実測）を算出
  float dt_ms = (prev_ms == 0) ? (float)PERIOD_MS : (float)(now - prev_ms);
  prev_ms = now;

  // ダミー生成（例）
  float t = (now - t0) * 0.001f;
  float ax = -9.806f + 0.05f * sinf(t);
  float ay = 0.02f * sinf(2*t);
  float az = 0.02f * cosf(1.5f*t);
  float gx = 0.2f * sinf(1.2f*t);
  float gy = 0.3f * cosf(0.9f*t);
  float gz = 0.1f * sinf(0.7f*t);
  int   ail = 1500 + (int)(100.0f * sinf(0.5f*t));
  int   elv = 1500 + (int)(80.0f * cosf(0.4f*t));
  int   rud = 1500 + (int)(60.0f * sinf(0.3f*t));
  float batt= 11.8f - 0.001f * (now - t0) * 0.001f;
  float temp= 35.0f + 0.5f * sinf(0.1f*t);

  // DAT 1行を作成（t_ms は now=millis()、dt_ms を values の先頭に）
  char line[320];
  int n = snprintf(line, sizeof(line),
    "DAT,%lu,%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d,%d,%.2f,%.2f",
    (unsigned long)++seq, (unsigned long)now,  // <seq>,<t_ms>
    dt_ms,                                      // ★ <dt_ms>
    ax, ay, az, gx, gy, gz, ail, elv, rud, batt, temp);

  if (n > 0) {
    Serial.println(line);  // USBシリアルへ
    OUT.println(line);     // 親機へ
  }
}
