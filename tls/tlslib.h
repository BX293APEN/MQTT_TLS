/*
 * tlslib.h  —  MQTT over TLS ポータブル TLS 抽象化レイヤ
 *
 * 目的:
 *   picotls (minicrypto バックエンド) を使い、外部依存ゼロで
 *   TLS 1.3 クライアント機能を提供する。
 *   TCP ソケット操作は pen_xxxx() 経由で行う (penlib.h 集約済み)。
 *
 * 名前規則:
 *   標準ライブラリ抽象: pen_xxxx()   (penlib.h)
 *   TLS 操作          : tls_xxxx()   (このヘッダ)
 *   MQTT 操作         : mqtt_xxxx()  (mqtt_client.h)
 *
 * 提供 API:
 *   tls_connect()          TCP 接続 + TLS ハンドシェイク
 *   tls_send()             TLS レコードとして送信
 *   tls_recv()             TLS レコードとして受信 (タイムアウト付き)
 *   tls_close()            TLS close_notify + TCP クローズ
 *   tls_is_connected()     接続状態確認
 */

#ifndef TLSLIB_H
#define TLSLIB_H

/* すべての標準型・OS API は penlib.h 経由で提供する */
#include <penlib.h>

/* picotls コアと minicrypto バックエンド */
#include "../picotls/include/picotls.h"
#include "../picotls/include/picotls/minicrypto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * ソケット型 (pen_xxxx と同じプラットフォーム抽象)
 * ========================================================================== */

#ifdef _WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
typedef SOCKET tls_socket_t;
#  define TLS_INVALID_SOCKET  INVALID_SOCKET
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <poll.h>
typedef int    tls_socket_t;
#  define TLS_INVALID_SOCKET  ((tls_socket_t)(-1))
#endif

/* ==========================================================================
 * TLS セッション構造体
 * ========================================================================== */

/*
 * tls_session_t: 確立済み TLS 接続の状態を保持する。
 *
 * 使用例:
 *   tls_session_t sess = TLS_SESSION_INIT;
 *   tls_connect("broker.example.com", 8883, "broker.example.com", &sess);
 *   tls_send(&sess, buf, len);
 *   tls_close(&sess);
 */
typedef struct {
    tls_socket_t    sock;       /* TCP ソケット */
    ptls_t         *tls;        /* picotls ハンドル */
    ptls_context_t  ctx;        /* picotls コンテキスト (セッション内保持) */

    /* minicrypto に必要な静的テーブル */
    ptls_key_exchange_algorithm_t  *key_exchanges[2];
    ptls_cipher_suite_t            *cipher_suites[4];
} tls_session_t;

#define TLS_SESSION_INIT { TLS_INVALID_SOCKET, NULL, {0}, {NULL,NULL}, {NULL,NULL,NULL,NULL} }

/* ==========================================================================
 * 接続オプション
 * ========================================================================== */

typedef struct {
    int  verify_cert;    /* 1=証明書検証を行う (デフォルト: 0=スキップ) */
    int  timeout_ms;     /* 接続タイムアウト ms (0=5000ms) */
    int  debug;          /* 1=デバッグ出力有効 */
} tls_connect_options_t;

#define TLS_CONNECT_OPTIONS_DEFAULT { 0, 5000, 0 }

/* ==========================================================================
 * 公開 API
 * ========================================================================== */

/*
 * tls_connect() — TCP 接続 + TLS ハンドシェイクを実行する
 *
 * @param[in]  host      サーバーホスト名 または IP アドレス
 * @param[in]  port      ポート番号 (MQTT over TLS 標準: 8883)
 * @param[in]  sni       TLS SNI (NULL のとき host を使用)
 * @param[in]  opts      接続オプション (NULL のときデフォルト)
 * @param[out] session   確立したセッションの格納先
 * @return 0=成功, 負=エラー
 */
int tls_connect(
    const char                *host,
    int                        port,
    const char                *sni,
    const tls_connect_options_t *opts,
    tls_session_t             *session
);

/*
 * tls_send() — TLS レコードとして len バイト送信する
 *
 * @param[in] session  確立済みセッション
 * @param[in] buf      送信バッファ
 * @param[in] len      送信バイト数
 * @return 0=成功, 負=エラー
 */
int tls_send(tls_session_t *session, const uint8_t *buf, size_t len);

/*
 * tls_recv() — TLS レコードから最大 buf_max バイト受信する
 *
 * @param[in]  session    確立済みセッション
 * @param[out] buf        受信バッファ
 * @param[in]  buf_max    バッファサイズ
 * @param[out] out_len    実際に受信したバイト数
 * @param[in]  timeout_ms タイムアウト ms (0=無制限)
 * @return 0=成功, 1=タイムアウト, -1=エラー/切断
 */
int tls_recv(
    tls_session_t *session,
    uint8_t       *buf,
    size_t         buf_max,
    size_t        *out_len,
    int            timeout_ms
);

/*
 * tls_recv_exact() — TLS レコードからちょうど len バイト受信する
 *
 * @param[in]  session    確立済みセッション
 * @param[out] buf        受信バッファ
 * @param[in]  len        受信するバイト数
 * @param[in]  timeout_ms タイムアウト ms (0=無制限)
 * @return 0=成功, 1=タイムアウト, -1=エラー/切断
 */
int tls_recv_exact(
    tls_session_t *session,
    uint8_t       *buf,
    size_t         len,
    int            timeout_ms
);

/*
 * tls_close() — TLS close_notify を送り TCP ソケットを閉じる
 *
 * @param[in] session  閉じるセッション
 */
void tls_close(tls_session_t *session);

/*
 * tls_is_connected() — セッションが接続中かどうかを返す
 *
 * @param[in] session  確認するセッション
 * @return 1=接続中, 0=未接続
 */
int tls_is_connected(const tls_session_t *session);

#ifdef __cplusplus
}
#endif

#endif /* TLSLIB_H */
