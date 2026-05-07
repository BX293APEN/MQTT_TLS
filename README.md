# mqtt_tls — MQTT over TLS クライアント

外部依存ゼロ・標準ライブラリ集約の MQTT over TLS クライアント実装。

## 概要

| レイヤ | ソース | 説明 |
|--------|--------|------|
| 標準ライブラリ抽象 | `lib/penlib.h` / `lib/penlib.c` | `pen_xxxx()` — メモリ・文字列・時刻操作の集約点 |
| TLS 抽象化 | `tls/tlslib.h` / `tls/tlslib.c` | `tls_xxxx()` — picotls minicrypto による TLS 1.3 |
| MQTT パケット生成 | `mqtt/mqtt_message_create.h/c` | `mqtt_xxxx()` — MQTT 5.0 パケットのバイト列生成 |
| MQTT 制御 | `mqtt/mqtt_client.h/c` | `mqtt_xxxx()` — CONNECT/PUBLISH/SUBSCRIBE 制御 |
| TLS エンジン | `picotls/` | picotls minicrypto (OpenSSL/mbedtls 不要) |
| エントリポイント | `main.c` | pub/sub コマンドラインツール |

## 関数名前規則

```
pen_xxxx()   標準ライブラリ抽象 (penlib.h)
tls_xxxx()   TLS 操作           (tlslib.h)
mqtt_xxxx()  MQTT 操作          (mqtt_client.h)
```

## 外部依存

**なし** — 標準 C ライブラリ + OS ソケット API のみを使用。

- TLS 暗号処理: picotls の minicrypto バックエンド (同梱)
- 鍵交換: X25519 (micro-ecc + cifra、同梱)
- 暗号スイート: AES-128-GCM-SHA256 / AES-256-GCM-SHA384 / ChaCha20-Poly1305-SHA256

## ビルド方法

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)        # Linux / MinGW
# cmake --build .      # MSVC / クロスプラットフォーム共通
```

### オプション

```bash
# デバッグログ有効
cmake .. -DMQTT_DEBUG=ON

# TLS 証明書検証を有効化 (デフォルト: 無効)
cmake .. -DMQTT_TLS_VERIFY_CERT=ON

# タイムゾーン設定
cmake .. -DMQTT_UTC_OFFSET_SEC=0       # UTC
cmake .. -DMQTT_UTC_OFFSET_SEC=32400   # JST (デフォルト)
cmake .. -DMQTT_TZ_FROM_OS=ON          # OS TZ を使用

# リリースビルド
cmake .. -DCMAKE_BUILD_TYPE=Release
```

## 使い方

```bash
# パブリッシュ
./mqtt_tls.exe pub <topic> <message> [client_id]

# サブスクライブ (タイムアウトまで受信し続ける)
./mqtt_tls.exe sub <topic> [client_id]
```

### 接続先の変更

`main.c` 内の以下の行を編集する:

```c
const char *host = "192.168.10.64"; /* ブローカーアドレス */
int         port = 8883;            /* MQTT over TLS 標準ポート */
```

## アーキテクチャ

```
main.c
  └── mqtt/mqtt_client.h/c      (mqtt_xxxx)
        ├── mqtt/mqtt_message_create.h/c  (MQTTパケット生成)
        ├── tls/tlslib.h/c                (tls_xxxx : TLS 1.3)
        │     └── picotls/               (minicrypto バックエンド)
        └── lib/penlib.h/c               (pen_xxxx : 標準ライブラリ集約)
```

## MQTT TCP 版との差分

| 項目 | MQTT/TCP | MQTT/TLS |
|------|----------|----------|
| デフォルトポート | 1883 | 8883 |
| 暗号化 | なし | TLS 1.3 |
| 標準ライブラリ集約 | `mq_xxxx()` (mqttlib.h) | `pen_xxxx()` (penlib.h) |
| トランスポート抽象 | `mq_tcp_xxxx()` | `tls_xxxx()` (tlslib.h) |
| 外部依存 | なし | なし (picotls 同梱) |

## ライセンス

picotls は MIT License で提供されている。
本プロジェクトのオリジナルコードについてもMIT Licenseを適用する。
