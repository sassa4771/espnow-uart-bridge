# student_uart_demo

**目的**  
このスケッチは「メインMCU側のサンプル」として、**UART 経由で HDR/DAT（CSV）** を出力する送信デモである。  
親機（`parent_uart_bridge`）が本データを受信し **ESP-NOW** で子機へ転送、子機（`child_uart_bridge`）は **PC に CSV を出力**する。

---

## システム構成（役割）

1. **student_uart_demo**（本スケッチ）  
   UART で HDR（スキーマ）と DAT（データ行）を送信
2. **parent_uart_bridge**（親機）  
   UART 受信 → スキーマ（HDR）から `schema_id` を算出 → ESP-NOW でブリッジ送信
3. **child_uart_bridge**（子機）  
   ESP-NOW 受信 → PC へ CSV 出力（シリアル）

**配線図**: `docs/assets/Wiring_Diagram.png`  
**デモ動画**: `docs/assets/esp_now_demo.mp4`

---

## 出力仕様（本スケッチ）

### 1) HDR（スキーマ宣言／起動直後に 1 回送信）
```
HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50
```
- `fields=` 以降が **列名の並び**。親機はこれを読み、**FNV-1a 16bit** で `schema_id` を算出する。  
- `GLDR` は任意ラベル（解析には未使用）。  
- `rate=50` はメタ情報（ログ用途）。親機の処理には影響しない。

### 2) DAT（データ行／既定 50 Hz）
```
DAT,<seq>,<t_ms>,<dt_ms>,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp
```
- `seq` : 送信ごとの連番  
- `t_ms`: `millis()` の時刻（ms）  
- `dt_ms`: 直前送信からの **実測** 間隔（ms）

**USB 側出力例**
```
HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50
DAT,1,210,20.000,-9.806,0.000,0.020,0.000,0.300,0.000,0,1500,11.80,35.20
DAT,2,230,20.000,-9.805,0.002,0.019,0.001,0.299,0.001,5,1504,11.80,35.21
...
```

> **備考**  
> このデモでは **IMU 6 軸 + サーボ 3ch + 電圧 + 温度** をダミー生成している。  
> 実機 MCU では、この部分を実センサ／コマンド値の読み出しに置き換えること。

---

## 配線

- **本スケッチ（送信側） TX** → **親機（受信側） RX**
- 既定 TX ピン: `GPIO23`（変更可）
- 親機が **Seeed XIAO ESP32-C3** の場合: RX は **D7(GPIO20)**
- **GND 共通**（必須）
- 電圧レベルは **3.3V TTL 同士**のためレベル変換は不要

> 親機の受信ピンは、`parent_uart_bridge` の **`UART_RX_PIN`** 定義に合わせること。

---

## シリアル設定

- **USB デバッグ（PC 表示）** … `Serial` @ **115200 bps**  
- **外部 UART 出力（親機へ）** … `Serial1`（TX のみ使用、RX 未接続で可）

---

## ビルド & 実行

1. ボード例：**ESP32-WROOM-32 DevKit** を選択  
2. `student_uart_demo.ino` を開く  
3. 「設定」セクションで **TX ピン** や **送信レート** を必要に応じて変更  
4. 書き込み → シリアルモニタ（115200）で `HDR` と連続する `DAT` を確認  
5. 親機・子機を起動し、**子機のシリアル**にも CSV が出力されることを確認

---

## 主な設定（コード抜粋）

```cpp
// 外部機器向け UART（親機へ出す）
HardwareSerial& OUT = Serial1;
const int OUT_TX = 23;        // ★ TX ピン（基板に合わせて変更）
const int OUT_RX = -1;        // 受信不要なら -1 のまま
const unsigned OUT_BAUD = 115200;

// USB デバッグ
const unsigned DBG_BAUD = 115200;

// 送信レート
constexpr uint32_t SAMPLE_RATE_HZ = 50;     // ★ ここを変えると 10/50/100 Hz などに
constexpr uint32_t PERIOD_MS      = 1000 / SAMPLE_RATE_HZ;
```

> **注意（コンパイルエラー対策）**  
> 一部環境では `HZ` が他ヘッダのマクロと衝突する。  
> その場合は上記のように **`SAMPLE_RATE_HZ`** へリネームすること。

---

## オプション：HDR の定期再送

このスケッチは **起動時に 1 回だけ** HDR を送信する。  
途中参加／再起動からの復帰時間を短くしたい場合は、定期再送を追加できる。

```cpp
// 追加：グローバル
unsigned long last_hdr_ms = 0;
const uint32_t HDR_PERIOD_MS = 5000;

// setup() の末尾などで初期化
last_hdr_ms = millis();

// loop() の末尾に追加
if (millis() - last_hdr_ms > HDR_PERIOD_MS) {
  OUT.println("HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50");
  last_hdr_ms = millis();
}
```

> 親機が **NVS** に前回 HDR を保存し、**起動直後に即再送** する実装であれば、  
> 定期再送なしでも多くのケースで復帰は短時間である。現場要件に合わせて選択すること。  
> なお、子機が `REQUIRE_HDR=1` の場合、**HDR 到着まで DAT を破棄**する。

---

## トラブルシュート

- **親機が受信しない**
  - 親機側 **RX ピン** と **ボーレート（115200）** を確認
  - 送信は `println` で **改行** を入れているか
  - **GND 共通**になっているか
- **子機に何も出ない**
  - 親機が **HDR を保持していない** 可能性（初回／NVS 空）  
    → このデモを再起動して HDR を送る
  - 親機の `peerMac` が **子機の STA MAC** になっているか確認
  - 子機が `REQUIRE_HDR=1` の場合、HDR が来るまで DAT を捨てる
- **`expected unqualified-id before numeric constant`**
  - 上述のとおり `HZ` を **`SAMPLE_RATE_HZ`** にリネーム

---

## ライセンス

このフォルダ内のコードは本リポジトリの **LICENSE**に従う。
