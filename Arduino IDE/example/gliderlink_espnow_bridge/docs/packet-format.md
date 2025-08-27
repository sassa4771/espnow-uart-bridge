# Packet & Line Format

## 1. UART Line Protocol (機体マイコン → 親機ESP)

**行単位（\n 終端）**で、最初に **HDR**、以降は **DAT** 行を送ります。カンマ区切り。

### HDR 行（必須・最初に1回以上）
```
HDR,1,GLDR,fields=ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp,rate=50
```
- `HDR` : 固定語
- `1`   : バージョン
- `GLDR`: 任意のシステムID
- `fields=`: **データ列の順番**（カンマ区切り）。この並びから **schema_id(16bit)** を生成します。
- 任意パラメータ（例：`rate=50`）は無視してOK。

### DAT 行（連続）
```
DAT,<src_seq>,<t_ms>,<values...>
例) DAT,12345,1740.12,-9.801,0.031,0.020,-0.12,0.45,-0.03,1520,1480,1500,11.92,36.5
```
- `src_seq` : 機体側のシーケンス（32bit推奨）
- `t_ms`    : 発生時刻 [ms]（float/整数どちらでも）
- `<values>`: `fields=`の並びに対応する数値（float/int）。**カンマのみ**、改行で終了。

> 文字コードは UTF-8。数値にカンマや改行を含めないこと。NaN/Inf は出さない。

---

## 2. ESP-NOW NowFrame (親機ESP → 子機ESP)

### ヘッダ
```c
struct __attribute__((packed)) NowFrameHdr {
  uint8_t  ver;         // =1
  uint8_t  type;        // 1=HDR, 2=DATA
  uint16_t node_id;     // 機体/親機ID（任意）
  uint32_t tx_seq;      // 親機の送信シーケンス
  uint16_t payload_len; // payload のバイト数
  uint16_t crc16;       // CCITT(0x1021), init=0xFFFF, hdr+payload全体
  // payload follows
};
```

### ペイロード
- `type=HDR`:
  ```c
  uint16_t schema_id;
  uint16_t fields_len;     // bytes
  char     fields[fields_len]; // "ax,ay,az,gx,gy,..." (NULなし)
  ```
- `type=DATA`:
  ```c
  uint16_t schema_id;
  uint32_t src_seq;
  uint32_t t_ms;           // 送信側で丸め（floatでも可）
  uint16_t value_count;
  float    values[value_count];
  ```

> 断片化は本最小実装では省略。列が多く 200B を超える場合は、fragment拡張を追加してください。

---

## 3. 子機のPC出力（CSV）
子機は受信した HDR/DATA を**CSV行**としてPCへそのまま出力します。
- HDR 例:
  ```
  HDR,1,GLDR,fields=ax,ay,az,gx,gy,gz,ail,elv,rud,batt,temp
  ```
- DAT 例:
  ```
  DAT,12345,1740,-9.801,0.031,0.020,-0.12,0.45,-0.03,1520,1480,1500,11.92,36.5
  ```
