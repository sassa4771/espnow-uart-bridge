# 用語集（espnow-flight-telemetry）

このドキュメントは、`espnow-flight-telemetry` リポジトリで頻出する用語・概念を**実装者目線**で簡潔にまとめたものです。学生が作る機体側MCUからのUART出力、ESP-NOWでの無線伝送、PCでのログ/可視化まで一連の流れで迷わないように整理しています。

> 参考動画（ESP-NOW 概要）: https://www.youtube.com/watch?v=bEKjCDDUPaU

---

## A–Z / あいうえお順

### ACK / 再送（Acknowledgement / Retransmission）
**何**: 受信確認（ACK）を返して、届かなかったフレームを**再送**する仕組み。  
**使いどころ**: ログ欠損を限りなくゼロにしたいとき。ただし往復遅延↑/実装負荷↑。  
**本プロジェクト**: 既定は**無ACK**＋**二重送信**などで実用上の堅牢性を確保。必要に応じて拡張で追加可能。

---

### CSV（Comma-Separated Values）
**何**: **カンマ区切り＋改行終端**のテキストフォーマット。  
**使いどころ**: 機体MCU→親機ESPのUART入出力、子機ESP→PCログ。  
**注意**: NaN/Infを出さない・小数点は`.`・値にカンマを含めない。

---

### ESP-IDF v5系コールバック
**何**: Arduino Core v3.x（内部ESP-IDF v5系）でのESP-NOW受信コールバック。  
```cpp
void onDataRecv(const esp_now_recv_info* info,
                const uint8_t* data, int len);
```
**注意**: 旧式（`const uint8_t* mac`）とは**シグネチャが異なる**。移植時に要確認。

---

### ESP-NOW（エスピーナウ）
**何**: Espressifの**低遅延・省電力**な**Wi‑Fi直結**通信プロトコル（インフラ不要）。  
**特徴**: ペアリングしたMAC間の**ユニキャスト**／ブロードキャスト。到達率は**ベストエフォート**。  
**参考**: YouTube解説 → https://www.youtube.com/watch?v=bEKjCDDUPaU

---

### FNV-1a 縮約（FNV-1a reduce to 16-bit）
**何**: 軽量ハッシュ **FNV‑1a(32bit)** を回して、**上位16bit XOR 下位16bit**で**16bit化**。  
**用途**: `fields=`（列名の並び）を短い **Schema ID** に変換。  
```cpp
uint16_t fnv1a16(const String& s){
  uint32_t h=2166136261u;
  for (auto c: s){ h ^= (uint8_t)c; h *= 16777619u; }
  return (uint16_t)((h>>16) ^ (h&0xFFFF));
}
```

---

### HDR / DAT（UART行プロトコル）
**HDR**: 最初に**列の並び**を宣言する行。  
```
HDR,1,GLDR,fields=ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50
```
**DAT**: 以降の**データ行**。`src_seq` と `t_ms` に続いて、**fieldsの順**で値を並べる。  
```
DAT,<src_seq>,<t_ms>,<values...>
例) DAT,12345,1740.12,-9.801,0.031,0.020,-0.12,0.45,-0.03,1520,1480,1500,11.92,36.5
```
**利点**: 学生が `Serial.println()` だけで実装しやすい。列の追加/削除は **HDR** で表明。

---

### IMU（Inertial Measurement Unit）
**何**: 加速度（ax, ay, az）・角速度（gx, gy, gz）等を計測するセンサ。  
**本プロジェクト**: 代表的な列名として `ax,ay,az,gx,gy,gz` を使用。単位は **SI（m/s², rad/s）**。

---

### LRモード（Long Range）
**何**: 受信感度を高めて**到達距離を延ばす**Wi‑Fi拡張（DSSS系）。  
**代償**: スループット低下（~0.25–0.5 Mbps相当）。  
**注意**: **両端で同時に**有効化する。チャネル設定と併せて運用。

---

### MCU（Microcontroller Unit）
**何**: 機体に搭載する学生側のマイコン。IMU/舵面などを読み、**HDR→DAT** を **UART** で親機へ送る。

---

### NowFrame（軽量バイナリフレーム）
**何**: 親機→子機（ESP‑NOW）の**送信用バイナリ枠**。  
**目的**: テキストより**短い**・**検査可能（CRC）**・拡張しやすい。  
```cpp
struct __attribute__((packed)) NowFrameHdr {
  uint8_t  ver;      // =1
  uint8_t  type;     // 1=HDR, 2=DATA
  uint16_t node_id;  // 機体/親機ID
  uint32_t tx_seq;   // 親機送信シーケンス
  uint16_t payload_len;
  uint16_t crc16;    // ヘッダ+payloadにCRC16-CCITT
};
// type=HDR payload:  schema_id, fields_len, char fields[]
// type=DATA payload: schema_id, src_seq, t_ms, value_count, float values[]
```
**目安**: **~200–220B/フレーム**に収めると安定（相互運用・ドロップ耐性の観点）。

---

### Parsing（パース）
**何**: テキスト（`HDR`/`DAT` 行）を**構造化データ**（配列・構造体）に分解すること。  
**コツ**: 改行で行切り、カンマで分割、数値化（NaNを出さない）、列数チェック、異常行は破棄。

---

### peer / peerMac（ピア / ピアMAC）
**何**: ESP‑NOWの相手デバイス。  
**設定**: ユニキャスト送信では **相手の STA MAC** を `esp_now_add_peer()` に登録。

---

### RSSI / RTT
**RSSI**: 受信強度指標（電波の強さ）。**大きいほど良い**（単位はdBmで負値）。  
**RTT**: 往復遅延（ping→pongの往復時間）。**小さいほど低遅延**。

---

### Schema ID（スキーマID）
**何**: `fields=` の文字列から作る**16bit識別子**（例：FNV‑1a縮約）。  
**目的**: 無線で長い `fields` を都度送らず、**短いID**で同一スキーマだと伝える。  
**注意**: 空白や末尾カンマが違うだけで**別ID**になる。`trim()`や正規化が大事。

---

### STA MAC（ステーションMAC）
**何**: Wi‑Fi **STA（クライアント）モード**のMAC。  
**注意**: ESP32には**AP用MAC**もある。**ユニキャスト相手はSTA MAC**を使う。  
**取得**: `WiFi.macAddress()`（STA） / `WiFi.softAPmacAddress()`（AP）。

---

### UART（Universal Asynchronous Receiver/Transmitter）
**何**: 非同期シリアル通信。ここでは**115200bps**を標準。  
**使いどころ**: MCU→親機（CSV行）、子機→PC（CSV行）。  
**注意**: 行終端（`\n`）必須、電圧レベルやGND共有、配線ピン（RX/TX）に注意。

---

### バンド幅の概算（帯域計算）
**式**: `必要帯域[bps] ≒ フレームサイズ[byte] × 8 × 送信Hz`  
**例**: 120B × 8 × 50Hz ≒ **48 kbps**。空中オーバヘッド込みでも十分現実的。  
**運用**: 実効の**20–30%以内**に収めると安定しやすい。

---

### フラグメンテーション（断片化）
**何**: 1フレームに収まらない大きなデータを**複数フレーム**に分割して送る。  
**本プロジェクト**: 最小実装は**非対応**。必要時は `frag_idx/frag_cnt` を拡張して導入。

---

### 送信二重化（Duplicate Send）/ バックプレッシャ（Backpressure）
**二重化**: 同じフレームを**2回送る**簡易冗長化。ロス耐性↑。  
**バックプレッシャ**: 輻輳時に**送信Hzを落とす／古いサンプルを間引く**制御。安定運用に有効。

---

### 距離・チャネル・送信出力
**距離**: 見通し屋外で**100m級**（通常モード）。LRでさらに延びやすい（環境依存）。  
**チャネル**: **1/6/11** を試し、混雑の少ないものを選ぶ（両端で**一致**が必須）。  
**送信出力**: `esp_wifi_set_max_tx_power(80); // 20 dBm`（地域規制を遵守）。

---

### データサイズの目安（1フレーム）
**安全域**: ~200–220B/フレーム。  
**float**: 4バイト/値 → 値N個で**約 4N バイト**。`src_seq/t_ms`等の付帯を加味して設計。  
**例**: IMU(6) + 舵面(3) + 電源/温度(2) = 11項目（44B） + 付帯 ≈ **80–120B**/frame（50 Hzなら 40–60 kbps）。

---

### CRC（巡回冗長検査）
**何**: 通信破損を検出する検査値。ここでは **CRC‑16/CCITT** を使用。  
```cpp
uint16_t crc16_ccitt(const uint8_t* p, size_t n){
  uint16_t crc=0xFFFF;
  for(size_t i=0;i<n;i++){
    crc ^= (uint16_t)p[i]<<8;
    for(int b=0;b<8;b++)
      crc=(crc&0x8000)?(crc<<1)^0x1021:(crc<<1);
  }
  return crc;
}
```
**使い方**: 送信側＝ヘッダ+ペイロードに計算して格納、受信側＝再計算で照合。

---

## 関連ファイル（推奨配置）
```
docs/
  └─ glossary.md        # ← 本ドキュメント
  └─ packet-format.md   # UART行 & NowFrame 詳細仕様
firmware/
  ├─ parent_uart_bridge/
  ├─ child_csv_sink/
  └─ student_uart_demo/
README.md
```

---

## 参考リンク
- ESP‑NOW 概要（YouTube）: https://www.youtube.com/watch?v=bEKjCDDUPaU
- Espressif 公式（ESP‑IDF / Arduino Core）: API & Examples
