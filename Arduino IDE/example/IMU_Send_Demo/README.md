# ESP-NOW IMU Streaming Example (M5StickC → M5Atom)

**目的**  
M5StickC の内蔵 IMU（加速度・ジャイロ）を **ESP-NOW** で送信し、M5Atom 側で **リアルタイム表示**（シリアル出力＋簡易LED表示）する最小構成のサンプルです。  
Arduino Core v3（ESP-IDF v5系）準拠の **新しい受信コールバック** シグネチャに対応しています。

---

## 構成

- **送信側（Node）**：`imu_sender_m5stickc/imu_sender_m5stickc.ino`  
  M5StickC の IMU を **50Hz** でサンプリングし、ESP-NOW ユニキャスト送信。
- **受信側（Gateway）**：`imu_receiver_m5atom/imu_receiver_m5atom.ino`  
  受信したテレメトリを **115200bps** でシリアル出力。  
  M5Atom Matrix を使う場合は 5×5 LED に傾きを簡易表示（ピッチ/ロール）。

> 片方が **M5StickC（送信）**、もう片方が **M5Atom（受信）** を想定。Atom Lite の場合は LED 表示はスキップされ、シリアルのみ。

---

## 使い方（クイックスタート）

1. **受信側（M5Atom）を書き込み → MAC確認**  
   `imu_receiver_m5atom.ino` を書き込んで、シリアル(115200)で  
   `My MAC (STA) : xx:xx:xx:xx:xx:xx` を控える（STA MAC）。

2. **送信側（M5StickC）に相手のMACを設定**  
   `imu_sender_m5stickc.ino` の `peerMac[]` に **受信側の STA MAC** を設定して書き込み。

3. **チャネル合わせ**  
   両スケッチの `CHANNEL` を同じ値に（既定 `1`）。混雑環境なら 1/6/11 を試す。

4. **動作確認**  
   受信側シリアルに IMU 値が流れ、Matrix なら傾きに応じて LED が動けばOK。

---

## よくあるつまずき

- `peerMac` は **相手の STA MAC** を使う（AP MAC を入れない）。  
- `CHANNEL` を両端で一致させる。  
- Atom Lite は 5×5 LED がないため、LED表示は無効（シリアル出力のみ）。  
- 距離や遮蔽物で取りこぼす場合は、1/6/11 の **混雑が少ないチャネル**、`esp_wifi_set_max_tx_power(80)`、必要なら **LRモード**（両端有効）を検討。

---

## ライセンス
MIT / Apache-2.0 等お好みで。
