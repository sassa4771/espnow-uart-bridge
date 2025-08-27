/* student_uart_demo.ino
 * 学生側マイコン想定：UARTで HDR 行を1回出し、その後 DAT 行を一定周期で出力
 * - ここでは PC に繋いでデモ可。親機ESPの UART_RX へ配線すればブリッジ動作。
 */
#include <Arduino.h>
#define BAUD 115200
const uint32_t HZ = 50;
const uint32_t PERIOD_MS = 1000 / HZ;
uint32_t seq=0;
unsigned long t0=0;

void setup(){
  Serial.begin(BAUD);
  delay(200);
  // 好きな列を宣言（順番が重要）
  Serial.println(F("HDR,1,GLDR,fields=ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50"));
  t0 = millis();
}

void loop(){
  static unsigned long next_ms = 0;
  unsigned long now = millis();
  if (now < next_ms) return;
  next_ms = now + PERIOD_MS;

  // デモ用にダミーデータ生成
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
  float batt= 11.8f - 0.001f * (now - t0) * 0.001f; // だんだん下がる体裁
  float temp= 35.0f + 0.5f * sinf(0.1f*t);

  // DAT 行出力：カンマ区切り、最後は println（改行）
  Serial.print(F("DAT,"));
  Serial.print(++seq); Serial.print(',');
  Serial.print(now);   // t_ms
  Serial.print(',');
  Serial.print(ax,3); Serial.print(',');
  Serial.print(ay,3); Serial.print(',');
  Serial.print(az,3); Serial.print(',');
  Serial.print(gx,3); Serial.print(',');
  Serial.print(gy,3); Serial.print(',');
  Serial.print(gz,3); Serial.print(',');
  Serial.print(ail);  Serial.print(',');
  Serial.print(elv);  Serial.print(',');
  Serial.print(rud);  Serial.print(',');
  Serial.print(batt,2); Serial.print(',');
  Serial.print(temp,2);
  Serial.println();
}
