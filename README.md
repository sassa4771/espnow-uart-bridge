# EUB：espnow-uart-bridge

[日本語版 README はこちら / Japanese README](README_ja.md)

> [!NOTE]
> This project is designed for **one-way telemetry over ESP-NOW (parent → child)**.
> CSV data (HDR/DAT) sent from an MCU to the parent via UART is transmitted by the parent over ESP-NOW,
> received by the child, and output to a PC as CSV. ※ Uplink/reply functionality is not yet implemented.

Robust **UART → ESP-NOW** telemetry bridge for ESP32 (XIAO ESP32-C3 / ESP32-WROOM-32).
- HDR persistence (NVS) & periodic HDR resend  
- Send queue & inflight limit (NO_MEM avoidance)  
- Auto re-init on stall + heartbeat (HB)  
- CRC16 check, LED indicators, CSV output (child)  
- Optional transparent repeater for multi-hop

> **Main programs:** `parent_uart_bridge` (parent) / `child_uart_bridge` (child)  
> Other sketches under `examples/` & `tools/` are for education and testing.

---

## Why this project? (Strengths)

This project is a production-ready ESP-NOW bridge designed to **relay UART output from general-purpose MCUs to a PC with high speed and low latency**. It emphasizes "no crashes, no stalls, immediate diagnostics" for research, education, and competitive environments.

### 1) Reliability & Recovery
- **HDR persistence + periodic resend**: Schema (column names) is saved to NVS, enabling automatic synchronization even when **either parent or child recovers later**. Resilient to power loss and resets.
- **Send queue + inflight control**: Absorbs the speed difference between UART supply and wireless processing, suppressing `ESP_ERR_ESPNOW_NO_MEM`.
- **Auto re-initialization ([REINIT])**: When wireless stalls, automatically executes `esp_now_deinit → wifi stop/start → set_channel → esp_now_init → add_peer` to self-heal the link.
- **CRC16-CCITT**: Detects errors in transmission and discards corrupted frames to guarantee **clean CSV**.
- **Heartbeat (HB) + STAT**: Check **throughput and drops** at a glance via serial `[STAT]`.

### 2) Simple & Interoperable
- **Text CSV agreement, efficient binary over-the-air**: MCU ↔ parent uses readable `HDR/DAT` (CSV), while over-the-air uses `NowFrame` (binary) for efficiency.
- **Schema-agnostic**: The **order of column names in `fields=`** is the specification. Any data configuration (IMU, steering, battery, etc.) can be transported in the same container.
- **Portability**: Verified to work on XIAO ESP32-C3 / ESP32-WROOM-32 and other **Arduino-ESP32 v3 / ESP-IDF v5 generation** devices.

### 3) Performance & Operational Ease
- **Low latency & practical throughput**: DAT is stably transported at continuous 50 Hz (example). **Backpressure** design adapted to the number of fields and bitrate.
- **ESP-NOW LR support (optional)**: Switch to Long Range for long-distance/low-rate scenarios (enable on both sides simultaneously). **Stable link** with fixed channel operation.

### 4) Developer Experience
- **Instant debugging**: `yourMCU_uart_demo` outputs headers and dummy data **immediately**. Easy setup with wiring diagram (`docs/assets/Wiring_Diagram.png`) and demo video.
- **Live visualization tool (Python)**: `host/python/apps/viewer.py`  
  - Auto HDR tracking, CSV logging, **auto Y-axis scaling (margin/smoothing/include-zero toggle)**, serial auto-reconnect.
  - Minimal dependencies (`pyserial` + `matplotlib`), with `requirements.txt` provided.

### 5) Extensibility
- **Easy to add/replace data definitions**: Just agree on column names in `fields=`, giving students' MCUs **high implementation freedom**.
- **Safe evolution**: Extension points for ACK/retransmission, compression, encryption, time synchronization (e.g., time distribution from parent) are **explicitly separated**.

> In short: **Hard to break, easy to fix, visible**. And a telemetry foundation that's **easy to adapt to anyone's MCU**.


## Wiring

![Wiring Diagram](docs/assets/Wiring_Diagram.png)

> [!IMPORTANT]
> **Sender**: Your main MCU → **Parent**(ESP32) via UART
> 
> **Link**: Parent → **Child** via ESP-NOW (same channel)
> 
> **Receiver**: Child → PC via USB (CSV)

---

## Demo (video)

![esp_now_demo](docs/assets/esp_now_demo.gif)

> [!NOTE]
> *Shown:* Serial output behavior (Arduino IDE) of `examples/yourMCU_uart_demo/yourMCU_uart_demo.ino` → `firmware/bridge/parent_uart_bridge` → `firmware/bridge/child_uart_bridge`.

---

## Repository layout

```
docs/ — Documentation (wiring diagrams, demo videos, etc.)
  assets/
    esp_now_demo.gif
    python_graph.gif
    Wiring diagram.pptx
    Wiring_Diagram.png
  glossary.md
  overview.md
  packet-format.md

examples/ — Demo programs to flash to your MCU
  yourMCU_uart_demo/
    yourMCU_uart_demo.ino
  README.md

firmware/ — Main: ESP-NOW UART bridge (espnow-uart-bridge) programs
  bridge/ — Parent/child bridge sketches
    child_uart_bridge/
      child_uart_bridge.ino
    parent_uart_bridge/
      parent_uart_bridge.ino

host/ — Python host software and demo programs (visualization, data analysis)
  python/
    apps/
       viewer.py — Real-time viewer for sensor data
       requirements.txt — List of dependencies

tools/ — Operation verification tools (MAC address check, etc.)
  MAC_Check/ — MAC address verification program
    mac_check/
      mac_check.ino

README.md
```


---

## Quick start

1. **Child (receiver)**  
   - Flash `firmware/bridge/child_uart_bridge/child_uart_bridge.ino`  
   - Open Serial @ **115200** → CSV should flow

2. **Parent (bridge)**  
   - Flash `firmware/bridge/parent_uart_bridge/parent_uart_bridge.ino`  
   - Configure:
     - `peerMac[]` → Child's STA MAC (`WiFi.macAddress()`)
     - `CHANNEL` → Same as child (e.g., `1`)
     - XIAO ESP32-C3 UART: **RX=D7(GPIO20)**, TX can be unused  
   - Check `[STAT]` and `[REINIT]` logs via Serial @ **115200**

3. **Sender (your MCU or demo)**  
   - For quick testing, flash `examples/yourMCU_uart_demo/yourMCU_uart_demo.ino` to another ESP32  
     - Output example  
      - `HDR,1,GLDR,fields=dt_ms,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50` (at startup)  
      - `DAT,<seq>,<t_ms>,<dt_ms>,ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp` (50Hz)  
   - From your actual MCU, send the same CSV format via UART and it will be bridged as-is

> [!IMPORTANT]
> **All nodes must use the same channel.** If using LR, enable `USE_WIFI_LR` on **all parent/child/repeater** nodes simultaneously.

> [!TIP]
> Parent: firmware/bridge/parent_uart_bridge
> 
> Child: firmware/bridge/child_uart_bridge
> 
> Glider MCU: examples/yourMCU_uart_demo
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

**yourMCU_uart_demo** (demo)
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
  - `DAT,<src_seq>,<t_ms>,<values...>` (demo includes `<dt_ms>`)
- **Over the air (ESP-NOW, binary)**  
  - `NowFrameHdr{ ver=1, type(1=HDR/2=DATA/3=HB), node_id, tx_seq, payload_len, crc16 }`  
  - `HDR payload{ schema_id(FNV-1a 16bit), fields_len, fields[] }`  
  - `DATA payload{ schema_id, src_seq, t_ms, value_count, float[value_count] }`  
  - CRC16-CCITT(0x1021)

> Child verifies CRC, reconstructs to CSV, and outputs via `Serial.println`. Strict operation possible with `REQUIRE_HDR`.

---

## Reading the logs

- Parent `[STAT]`:  
  `UART=<B/s>  Q=<depth>/<cap> drop=<n>  HDR=<n> DAT=<n>  TX ok=<n> fail=<n> inflight=<n>`
  - When only `TX ok=1` increments: **Only HB** is getting through (child doesn't display HB)
- Parent `[REINIT] restarting WiFi/ESP-NOW...`:  
  **Auto link recovery from stall**. Executes `esp_now_deinit→wifi stop/start→set_channel→esp_now_init→add_peer`

---

## Troubleshooting

- Nothing appears on child → Parent may not have **acquired HDR**  
  - Demo resends HDR every 5 seconds (recovers in a few seconds)  
  - Parent is designed to resend NVS HDR immediately after startup  
- MAC shows `00:00:...` → Check Wi-Fi initialization order  
  `mode → wifi_start → set_channel → (LR) → esp_now_init → add_peer`
- Frequent `ESP_ERR_ESPNOW_NO_MEM` → Lower `MAX_INFLIGHT`, review rate/field count
- `peerMac` must use **child's STA MAC** (not AP MAC)
- Wiring: TX→RX, **common GND**, 115200bps, line ending with `println`

---

## Host-side

`host/python/apps/viewer.py`:  
- Real-time visualization and recording of CSV from UART (`pyserial`, `pandas`, `matplotlib/pyqtgraph`, `rich`, `typer`)  
- Port selection, save, plot target fields, downsampling, HDR tracking

![python_graph](docs/assets/python_graph.gif)

---

## License

MIT

## Acknowledgements

- **Espressif Systems** — for ESP-NOW and ESP-IDF / Arduino-ESP32.
- **Arduino-ESP32 core maintainers** — for maintaining the ESP32 Arduino core.
- **Seeed Studio** — for XIAO ESP32-C3 board support and documentation.
- **M5Stack** — for M5StickC / M5Atom libraries and examples (used in IMU demos).
- **References** — ESP-NOW overview video (YouTube): https://www.youtube.com/watch?v=bEKjCDDUPaU
- **Contributors / Testers** — thanks to everyone who helped with experiments and validation.

