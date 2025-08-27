# GliderLink ESP-NOW Bridge

自立滑空機の**機体マイコン → 親機ESP（UART受信） → ESP-NOW → 子機ESP → PC（CSV出力）**を想定した最小リファレンス実装です。

- `firmware/parent_uart_bridge/parent_uart_bridge.ino`  
  機体マイコンから**UARTで受け取ったCSV**（HDR/DAT）をバイナリ化して**ESP-NOW送信**します。
- `firmware/child_csv_sink/child_csv_sink.ino`  
  ESP-NOW受信データを**CSV行（println）**に復元してPCシリアルへ出力します。
- `firmware/student_uart_demo/student_uart_demo.ino`  
  学生向けの**送信デモ**。機体マイコン側が出すべき**UARTのHDR+DAT**を模擬出力します。
- `docs/packet-format.md`  
  行プロトコル（HDR/DAT）と無線フレーム（NowFrame）仕様の説明。

## 使い方（超概要）
1. **子機**に `child_csv_sink.ino` を書き込み、**子機の STA MAC** を確認。
2. **親機**に `parent_uart_bridge.ino` を書き込み、スケッチ先頭の `peerMac[]` を**子機のSTA MAC**へ設定。`UART_RX_PIN/UART_TX_PIN`、`UART_BAUD` も機体マイコンの配線に合わせる。
3. 機体マイコン（学生作成）からは、`student_uart_demo.ino` のように **HDRを1回 → DATを連続**でUART送信。
4. 子機側のPCシリアル(115200bps)に **HDR 行→DAT 行**が流れたら成功。

詳細は `docs/packet-format.md` を参照。
