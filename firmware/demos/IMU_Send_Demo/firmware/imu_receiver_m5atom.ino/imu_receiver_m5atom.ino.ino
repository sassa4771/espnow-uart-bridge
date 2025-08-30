// M5Atom で ESP-NOW 受信 → シリアル表示（+ Matrixなら5x5で傾き可視化）
#include <M5Atom.h>      // Matrix/Lite どちらでもOK
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>

struct __attribute__((packed)) Telemetry {
  uint32_t seq;
  uint32_t t_us;
  float ax, ay, az;
  float gx, gy, gz;
};

const int CHANNEL = 1;     // ★送信側と一致させる
bool isMatrix = false;

uint32_t lastSeq=0, rxCnt=0;

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b){ return (r<<16)|(g<<8)|b; }

// 5x5 座標→インデックス（Matrix前提。Liteは何もしない）
int xy2i(int x,int y){ return y*5 + x; }

void drawTiltLED(float pitch_deg, float roll_deg){
  if(!isMatrix) return;
  // ピッチ[-45,45]をy=0..4、ロール[-45,45]をx=0..4にマッピング
  auto clamp = [](float v,float mn,float mx){ return v<mn?mn:(v>mx?mx:v); };
  int x = (int)roundf( (clamp(roll_deg,-45,45) + 45.0f) * 4.0f / 90.0f );
  int y = (int)roundf( (clamp(pitch_deg,-45,45) + 45.0f) * 4.0f / 90.0f );

  // 画面更新
  for(int i=0;i<25;i++) M5.dis.drawpix(i, 0);
  int idx = xy2i(x,y);
  M5.dis.drawpix(idx, rgb(0, 120, 255));
  // 上部にロールバー、左にピッチバーっぽい表示も追加（簡易）
  for(int xi=0; xi<=x; ++xi) M5.dis.drawpix(xy2i(xi,0), rgb(20,20,20));
  for(int yi=0; yi<=y; ++yi) M5.dis.drawpix(xy2i(0,yi), rgb(20,20,20));
}

void onDataRecv(const esp_now_recv_info* info, const uint8_t* data, int len){
  if(len < (int)sizeof(Telemetry)) return;
  Telemetry t; memcpy(&t, data, sizeof(Telemetry));
  rxCnt++; lastSeq = t.seq;

  // 加速度から簡易ピッチ/ロール算出
  // roll = atan2(ay, az), pitch = atan2(-ax, sqrt(ay^2+az^2))
  float roll  = atan2f(t.ay, t.az) * 57.2958f;
  float pitch = atan2f(-t.ax, sqrtf(t.ay*t.ay + t.az*t.az)) * 57.2958f;

  // 受信時刻（相対）
  static uint32_t t0=micros();
  uint32_t rxt_us = micros()-t0;

  // Serial Monitor用
  // Serial.printf("[IMU] seq=%lu t=%luus  ax=%.3f ay=%.3f az=%.3f  gx=%.2f gy=%.2f gz=%.2f  pitch=%.1f roll=%.1f\n",
  //   (unsigned long)t.seq, (unsigned long)rxt_us,
  //   t.ax, t.ay, t.az, t.gx, t.gy, t.gz, pitch, roll);

  // Serial Plotter用
  Serial.printf("%.3f,%.3f,%.3f,%.2f,%.2f,%.2f\n", t.ax, t.ay, t.az, t.gx, t.gy, t.gz);

  drawTiltLED(pitch, roll);
}

void setup(){
  M5.begin(true, false, true);   // Serial, I2C, Display
  Serial.begin(115200);
  delay(200);

  // Matrix判定（簡易：LED数があればMatrixとして扱う）
  isMatrix = true; // Atom Matrix 前提にする場合は true 固定でもOK
  if(isMatrix){ M5.dis.setBrightness(20); }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  WiFi.setSleep(false);
  esp_wifi_set_channel(CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.printf("My MAC (STA): %s\n", WiFi.macAddress().c_str());
  Serial.printf("Channel     : %d\n", CHANNEL);

  ESP_ERROR_CHECK(esp_now_init());
  esp_now_register_recv_cb(onDataRecv);
}

void loop(){

}
