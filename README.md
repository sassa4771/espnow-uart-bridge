# espnow-uart-bridge

Robust **UART → ESP-NOW** telemetry bridge for ESP32 (XIAO ESP32-C3 / ESP32-WROOM-32).
- HDR persistence (NVS) & periodic HDR resend  
- Send queue & inflight limit (NO_MEM 回避)  
- Auto re-init on stall + heartbeat (HB)  
- CRC16 check, LED indicators, CSV 出力（子機）  
- Optional transparent repeater for multi-hop

> **Main programs:** `parent_uart_bridge`（親機） / `child_uart_bridge`（子機）  
> Other sketches under `examples/` & `firmware/tools/` are for education and testing.

---

## Wiring

![Wiring Diagram](docs/assets/Wiring_Diagram.png)

**Sender**: Your main MCU → **Parent**(ESP32) via UART  
**Link**: Parent → **Child** via ESP-NOW (same channel)  
**Receiver**: Child → PC via USB (CSV)

---

## Demo (video)

> GitHub では動画はインライン再生されないことがあります。直接ファイルを開くかダウンロードしてご覧ください。  
> `docs/assets/esp_now_demo.mp4`

*Shown:* `examples/student_uart_demo/student_uart_demo.ino` → `firmware/bridge/parent_uart_bridge` → `firmware/bridge/child_uart_bridge` のシリアル出力挙動（Arduino IDE）。

---

## Repository layout

```
docs/
  assets/
    Wiring_Diagram.png
    esp_now_demo.mp4

examples/
  student_uart_demo/
    student_uart_demo.ino
    README.md

firmware/
  bridge/
    child_uart_bridge/
      child_uart_bridge.ino
    parent_uart_bridge/
      parent_uart_bridge.ino

  demos/
    IMU_Send_Demo/
      firmware/
        imu_receiver_m5atom.ino
        imu_sender_m5stickc.ino
      README.md

  tools/
    MAC_Check/
      mac_check/
        mac_check.ino
    ping-pong/
      firmware/
        ping_pong_resender/
          ping_pong_resender.ino
        ping_pong_initiator/
          ping_pong_initiator.ino
      README.md

host/
  python/
    (planned: apps/viewer.py, requirements.txt)
```


---

## Quick start

1. **Child (receiver)**  
   - Flash `firmware/bridge/child_uart_bridge/child_uart_bridge.ino`  
   - Open Serial @ **115200** → CSVが流れたらOK

2. **Parent (bridge)**  
   - Flash `firmware/bridge/parent_uart_bridge/parent_uart_bridge.ino`  
   - 設定するもの：
     - `peerMac[]` → **子機の STA MAC**（`WiFi.macAddress()`）
     - `CHANNEL` → 子機と同じ（例: `1`）
     - XIAO ESP32-C3 の UART: **RX=D7(GPIO20)**、TXは未使用でOK  
   - Serial @ **115200** で `[STAT]` や `[REINIT]` ログを確認

3. **Sender (your MCU or demo)**  
   - すぐ試すなら `examples/student_uart_demo/student_uart_demo.ino` を別のESP32に書き込み  
     - 出力例  
       - `HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50`（起動時）  
       - `DAT,<seq>,<t_ms>,<dt_ms>,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp`（50Hz）  
   - 実機MCUからは同じ CSV 形式で UART 送信すれば、そのまま橋渡しされます

> **全ノード同一チャネル必須。** LR を使う場合は **親/子/中継**すべてで `USE_WIFI_LR` を同時に有効化。

---

## Configuration (parent / child)

**parent_uart_bridge**
```cpp
// Channel must match child
const int CHANNEL = 1;

// Use child STA MAC (not AP MAC)
const uint8_t peerMac[6] = { 0x58, 0xCF, 0x79, 0xF3, 0xC8, 0xB4 };

// Queue / rate control
#define Q_CAP           256      // send queue depth
#define MAX_INFLIGHT    6        // concurrent sends (avoid NO_MEM)
#define PAYLOAD_MAX     220      // safe payload (ESP-NOW ~250B max)
#define HDR_PERIOD_MS   5000     // periodic HDR resend for self-sync

// Optional LR (enable on both ends)
// #define USE_WIFI_LR
```

**child_uart_bridge**
```cpp
#define SERIAL_BAUD 115200
const int CHANNEL = 1;     // must match parent
// #define USE_WIFI_LR
// Require HDR before DATA? (0=accept DATA anyway / 1=HDR required)
#define REQUIRE_HDR 0
```

**student_uart_demo**（デモ）
```cpp
// Outgoing UART to parent
HardwareSerial& OUT = Serial1;
const int OUT_TX = 23;     // change to your board pin
const unsigned OUT_BAUD = 115200;

// Sampling rate
// (rename HZ→SAMPLE_RATE_HZ if your toolchain defines HZ macro)
const uint32_t HZ = 50;
```

---

## Protocol (brief)

- **UART (text)**  
  - `HDR,1,GLDR,fields=<comma-separated-names>[,rate=…]`  
  - `DAT,<src_seq>,<t_ms>,<values...>`（デモでは `<dt_ms>` を含む）
- **Over the air (ESP-NOW, binary)**  
  - `NowFrameHdr{ ver=1, type(1=HDR/2=DATA/3=HB), node_id, tx_seq, payload_len, crc16 }`  
  - `HDR payload{ schema_id(FNV-1a 16bit), fields_len, fields[] }`  
  - `DATA payload{ schema_id, src_seq, t_ms, value_count, float[value_count] }`  
  - CRC16-CCITT(0x1021)

> 子機は CRC を検証し、CSVへ復元して `Serial.println`。`REQUIRE_HDR` で厳格運用も可能。

---

## Reading the logs

- 親機 `[STAT]`:  
  `UART=<B/s>  Q=<depth>/<cap> drop=<n>  HDR=<n> DAT=<n>  TX ok=<n> fail=<n> inflight=<n>`
  - `TX ok=1` のみ増えるときは **HBだけ**通っている状態（子機はHBを表示しません）
- 親機 `[REINIT] restarting WiFi/ESP-NOW...`：  
  **リンク詰まり自動復旧**。`esp_now_deinit→wifi stop/start→set_channel→esp_now_init→add_peer` を実行

---

## Troubleshooting

- 子機に何も出ない → 親機が **HDR未取得**の可能性  
  - デモは5秒おきに HDR 再送（数秒で復帰）  
  - 親機は NVS の HDR を起動直後に再送する設計  
- MAC が `00:00:...` → Wi-Fi 初期化順序を確認  
  `mode → wifi_start → set_channel → (LR) → esp_now_init → add_peer`
- `ESP_ERR_ESPNOW_NO_MEM` 多発 → `MAX_INFLIGHT` を下げる、レート/項目数の見直し
- `peerMac` は **子機の STA MAC** を使う（AP MAC ではない）
- 配線：TX→RX、**GND 共通**、115200bps、行末は `println`

---

## Host-side (planned)

`host/python/apps/viewer.py`（今後追加）：  
- UART からの CSV をリアルタイム可視化・記録（`pyserial`, `pandas`, `matplotlib/pyqtgraph`, `rich`, `typer`）  
- ポート選択、保存、プロット対象フィールド、ダウンサンプリング、HDR追従

---

## License

MIT

## Acknowledgements

Includes the “gliderlink” reference implementation under `firmware/bridge/`.  
Demos and educational sketches are preserved under `examples/` and `firmware/tools/`.
