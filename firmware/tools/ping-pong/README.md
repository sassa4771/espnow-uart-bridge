# ESP-NOW Ping-Pong Demo

ESP32 同士で ESP-NOW を使い、**PING→PONG** の往復遅延（RTT）をシリアル(115200 bps)に表示するデモです。  
デモは 2 本のスケッチで構成されています。

- `ping-pong_initiator.ino` …… 定期的に **PING** を送信し、返ってきた **PONG** で **RTT** と **ロス** を計測・表示
- `ping_pong_resender.ino` …… 受信した **PING** に **即座に PONG** を返す（“反射板/リピーター” 役）

> 将来的に **M5StickC の IMU/操舵テレメトリ送信**に発展させる前段の接続確認・遅延測定用です。

---

## 動作要件

- **Arduino IDE**（ESP32 ボードマネージャ導入済み）
- **ESP32 系ボード**（ESP32 / ESP32-S3 / M5StickC など）
- ESP32 Arduino Core **v3.x（ESP-IDF v5系）**  
  ※ 受信コールバックの関数シグネチャが v5 系仕様（`esp_now_recv_info*`）です

---

## ファイル構成

```
/ (任意のスケッチフォルダ)
├─ ping-pong_initiator.ino   # PING 送信 & RTT計測
└─ ping_pong_resender.ino    # PING を受けて PONG 返答
```

---

## クイックスタート

1. **ボードを2台**用意し、それぞれに以下のスケッチを書き込みます。  
   - 送信＆計測側：`ping-pong_initiator.ino`  
   - 応答（反射）側：`ping_pong_resender.ino`

2. **シリアルモニタを 115200 bps** で開き、各デバイスの **STA MAC アドレス** を確認します。  
   （スケッチ先頭のログに `My MAC (STA)` が表示されます）

3. 両方のスケッチの `peerMac[]` を **相手側の STA MAC** に設定して書き込み直します。  
   - **注意**：SoftAP MAC ではなく **STA MAC** を指定してください。

4. （任意）チャネル `CHANNEL` を **両端で同じ値** にします（既定は 1）。  
   混雑環境なら 1/6/11 を試してください。

5. （任意）長距離重視なら **LRモード** を **両端とも**有効にします。  
   - `#define USE_WIFI_LR` を有効化（速度は低下します）

6. 送受ともに起動後、**Initiator 側シリアル**に以下のような表示が出れば成功です。

```
[PING]  seq=12
[SEND-OK]   seq=12
[PONG]  seq=12  RTT=2480 us  (ok=10, lost=2)
```

---

## それぞれの役割

### `ping-pong_initiator.ino`
- 一定間隔（例：500 ms）で **PING** フレームを送信
- **PONG** を受け取ると `micros()` から **RTT(µs)** を計算
- 送信成功/失敗、タイムアウト、OK/Lost の統計を表示
- 文字 `r` をシリアルから送ると **統計をリセット**（実装している場合）

### `ping_pong_resender.ino`
- **PING を受信**したら、同じペイロードで **宛先を送信元にして即時 PONG 応答**
- 送信元 MAC は **コールバック引数 `info->src_addr`** から取得
- 相手が変わっても自動で返せるように、受信 MAC を **動的にピア登録**してから返信（実装版）

---

## 主な設定ポイント

コード上部の定数を編集します。

```cpp
const uint8_t peerMac[6] = {0x24,0x6F,0x28,0xAA,0xBB,0xCC}; // 相手の STA MAC
const int CHANNEL = 1;                   // 両端一致が必須
// #define USE_WIFI_LR                   // 長距離モード（両端ONが必須）
const uint32_t PING_INTERVAL_MS = 500;   // PING 周期（Initiator）
```

- **送信出力の上限**を上げる場合（地域規制内）：
  ```cpp
  esp_wifi_set_max_tx_power(80); // 20 dBm（単位は 0.25 dBm）
  ```

---

## 期待されるシリアル出力（例）

**Initiator**
```
My MAC (STA)  : 94:B9:7E:93:3B:50
Peer MAC (STA): 4C:75:25:CD:B3:A0
Channel: 1
[PING]  seq=1
[SEND-OK]   seq=1
[PONG]  seq=1  RTT=2530 us  (ok=1, lost=0)
```

**Resender**
```
My MAC (STA)  : 4C:75:25:CD:B3:A0
[PING→PONG] from 94:B9:7E:93:3B:50  seq=1
```

---

## うまくいかない時のチェックリスト

- **役割**：片側が `Initiator`、もう片側が `Resender` になっているか  
  （両方 Initiator だと PING し合うだけで PONG が返らず TIMEOUT 連発）
- **MAC**：`peerMac[]` は **相手の STA MAC** か（AP MAC を入れていないか）
- **チャネル**：`CHANNEL` が **両端で一致**しているか（`Channel now : X` を確認）
- **LR**：`USE_WIFI_LR` の有効/無効が **両端で同じ**か
- **距離/向き**：アンテナの向き・距離を調整（必要に応じて LR/送信電力↑）
- **電源**：USB ケーブルや給電の品質（不安定電源は TX 失敗の原因に）

---

## 今後の拡張（予定）

- `examples/m5stickc-imu-stream/`：M5StickC の IMU（加速度・ジャイロ・姿勢）やエルロン位置を **ESP-NOW ストリーム**で送信
- `gateway/`：受信→**CSV/JSON ログ**化、簡易ビューア（Python/Plotly など）

---

## ライセンス

MIT

---

## 謝辞

M5Stack / M5StickC は各社の商標です。ESP-NOW は Espressif Systems の技術です。
