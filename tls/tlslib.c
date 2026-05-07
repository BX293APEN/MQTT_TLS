/*
 * tlslib.c  —  MQTT over TLS ポータブル TLS 抽象化レイヤ実装
 *
 * picotls (minicrypto バックエンド) を使い、外部依存ゼロで
 * TLS 1.3 クライアント機能を提供する。
 *
 * TCP 層:
 *   getaddrinfo / socket / connect / send / recv / poll を直接使用。
 *   pen_xxxx() ラッパーはメモリ・文字列操作に使用する。
 *
 * TLS 層:
 *   ptls_client_new()  → TLS ハンドル生成
 *   ptls_handshake()   → TLS ハンドシェイク (ALPN を含む)
 *   ptls_send()        → アプリデータ暗号化送信
 *   ptls_receive()     → 受信データ復号
 *   ptls_free()        → ハンドル解放
 *
 * 受信バッファ管理:
 *   TLS レコードは複数の TCP セグメントにまたがることがある。
 *   内部リングバッファ (tls_rbuf_t) で未処理の復号済みデータを保持し、
 *   tls_recv_exact() が要求バイト数を揃えるまでループする。
 */

#include "tlslib.h"

/* ==========================================================================
 * 定数
 * ========================================================================== */

#define TLS_TCP_CONNECT_TIMEOUT_MS  5000
#define TLS_RAW_BUF_SIZE            16384   /* TCP 受信生バッファ */
#define TLS_PLAIN_BUF_SIZE          65536   /* 復号済みデータバッファ */
#define TLS_HANDSHAKE_BUF_SIZE      16384   /* ハンドシェイク送信バッファ */

/* ==========================================================================
 * ログマクロ
 * ========================================================================== */

#define TLS_LOG_INFO(...)  pen_fprintf(stderr, "[tls/info] " __VA_ARGS__)
#define TLS_LOG_ERR(...)   pen_fprintf(stderr, "[tls/err ] " __VA_ARGS__)
#define TLS_LOG_DBG(debug, ...) \
    do { if (debug) { pen_fprintf(stderr, "[tls/dbg ] " __VA_ARGS__); } } while(0)

/* ==========================================================================
 * 内部: 受信平文リングバッファ
 *
 * picotls は ptls_receive() で復号した平文を ptls_buffer_t に追記する。
 * 複数回の ptls_receive() 呼び出しで平文が蓄積されるため、
 * 呼び出し側が要求するバイト数ぶんを切り出せるよう内部バッファを持つ。
 * ========================================================================== */

typedef struct {
    uint8_t  data[TLS_PLAIN_BUF_SIZE];
    size_t   head;   /* 読み出し位置 */
    size_t   tail;   /* 書き込み位置 */
} tls_rbuf_t;

/* セッションごとの受信バッファ (静的確保で外部依存ゼロ) */
/* NOTE: tls_session_t は複数インスタンス不可の簡易実装。
 *       複数同時セッションが必要な場合は tls_session_t に rbuf を埋め込むこと。 */
static tls_rbuf_t g_rbuf;   /* 単一セッション用 */

static void rbuf_reset(tls_rbuf_t *rb)
{
    rb->head = rb->tail = 0;
}

static size_t rbuf_avail(const tls_rbuf_t *rb)
{
    return rb->tail - rb->head;
}

static int rbuf_push(tls_rbuf_t *rb, const uint8_t *src, size_t len)
{
    if (rb->tail + len > TLS_PLAIN_BUF_SIZE) {
        /* 先頭へ詰め直す */
        size_t used = rbuf_avail(rb);
        if (used + len > TLS_PLAIN_BUF_SIZE) return -1;
        pen_memmove(rb->data, rb->data + rb->head, used);
        rb->head = 0;
        rb->tail = used;
    }
    pen_memcpy(rb->data + rb->tail, src, len);
    rb->tail += len;
    return 0;
}

static size_t rbuf_pop(tls_rbuf_t *rb, uint8_t *dst, size_t want)
{
    size_t avail = rbuf_avail(rb);
    size_t take  = (avail < want) ? avail : want;
    pen_memcpy(dst, rb->data + rb->head, take);
    rb->head += take;
    return take;
}

/* ==========================================================================
 * 内部: ソケット操作ヘルパー
 * ========================================================================== */

static int sock_set_nonblocking(tls_socket_t sock, int enable)
{
#ifdef _WINDOWS
    u_long mode = enable ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) return -1;
    flags = enable ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return fcntl(sock, F_SETFL, flags) == 0 ? 0 : -1;
#endif
}

static int sock_wait_writable(tls_socket_t sock, int timeout_ms)
{
#ifdef _WINDOWS
    fd_set wfds, efds;
    FD_ZERO(&wfds); FD_SET(sock, &wfds);
    FD_ZERO(&efds); FD_SET(sock, &efds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int r = select(0, NULL, &wfds, &efds, timeout_ms > 0 ? &tv : NULL);
    if (r <= 0 || FD_ISSET(sock, &efds)) return -1;
    int err = 0; int errlen = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&err, &errlen);
    return err == 0 ? 0 : -1;
#else
    struct pollfd pfd = { sock, POLLOUT, 0 };
    int r = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : -1);
    if (r <= 0 || (pfd.revents & (POLLERR | POLLHUP))) return -1;
    int err = 0; socklen_t errlen = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen);
    return err == 0 ? 0 : -1;
#endif
}

static int sock_wait_readable(tls_socket_t sock, int timeout_ms)
{
#ifdef _WINDOWS
    fd_set rfds;
    FD_ZERO(&rfds); FD_SET(sock, &rfds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int r = select(0, &rfds, NULL, NULL, timeout_ms > 0 ? &tv : NULL);
    if (r == 0) return 1;   /* タイムアウト */
    if (r < 0)  return -1;
    return 0;
#else
    struct pollfd pfd = { sock, POLLIN, 0 };
    int r = poll(&pfd, 1, timeout_ms > 0 ? timeout_ms : -1);
    if (r == 0) return 1;   /* タイムアウト */
    if (r < 0)  return -1;
    if (pfd.revents & (POLLERR | POLLHUP)) return -1;
    return 0;
#endif
}

/* ==========================================================================
 * 内部: ソケットクローズ
 * ========================================================================== */

static void sock_close(tls_socket_t sock)
{
    if (sock == TLS_INVALID_SOCKET) return;
#ifdef _WINDOWS
    closesocket(sock);
#else
    close(sock);
#endif
}

/* ==========================================================================
 * 内部: TCP 接続試行 (1アドレス分)
 *
 * 成功: ブロッキングモードのソケットを返す
 * 失敗: TLS_INVALID_SOCKET を返す (ソケットは内部でクローズ済み)
 * ========================================================================== */

static tls_socket_t tcp_try_connect(struct addrinfo *p, int timeout_ms)
{
#ifdef _WINDOWS
    tls_socket_t sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == TLS_INVALID_SOCKET) return TLS_INVALID_SOCKET;

    if (sock_set_nonblocking(sock, 1) != 0) {
        sock_close(sock);
        return TLS_INVALID_SOCKET;
    }

    int r        = connect(sock, p->ai_addr, (int)p->ai_addrlen);
    int in_prog  = (r == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK);
#else
    tls_socket_t sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sock == TLS_INVALID_SOCKET) return TLS_INVALID_SOCKET;

    if (sock_set_nonblocking(sock, 1) != 0) {
        sock_close(sock);
        return TLS_INVALID_SOCKET;
    }

    int r       = connect(sock, p->ai_addr, p->ai_addrlen);
    int in_prog = (r < 0 && errno == EINPROGRESS);
#endif

    if (r != 0 && !in_prog) {
        sock_close(sock);
        return TLS_INVALID_SOCKET;
    }

    if (in_prog && sock_wait_writable(sock, timeout_ms) != 0) {
        sock_close(sock);
        return TLS_INVALID_SOCKET;
    }

    sock_set_nonblocking(sock, 0);
    return sock;
}

/* ==========================================================================
 * 内部: TCP 接続 (タイムアウト付き、複数アドレス対応)
 * ========================================================================== */

static tls_socket_t tcp_connect(const char *host, int port, int timeout_ms)
{
    char port_str[8];
    pen_snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    pen_memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return TLS_INVALID_SOCKET;

    tls_socket_t sock = TLS_INVALID_SOCKET;

    for (struct addrinfo *p = res; p != NULL && sock == TLS_INVALID_SOCKET; p = p->ai_next) {
        if (timeout_ms > 0) {
            sock = tcp_try_connect(p, timeout_ms);
        } else {
#ifdef _WINDOWS
            tls_socket_t s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s != TLS_INVALID_SOCKET) {
                if (connect(s, p->ai_addr, (int)p->ai_addrlen) == 0)
                    sock = s;
                else
                    sock_close(s);
            }
#else
            tls_socket_t s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (s != TLS_INVALID_SOCKET) {
                if (connect(s, p->ai_addr, p->ai_addrlen) == 0)
                    sock = s;
                else
                    sock_close(s);
            }
#endif
        }
    }

    freeaddrinfo(res);
    return sock;
}

/* ==========================================================================
 * 内部: TCP 全送信
 * ========================================================================== */

static int tcp_send_all(tls_socket_t sock, const uint8_t *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
#ifdef _WINDOWS
        int n = send(sock, (const char *)(buf + sent), (int)(len - sent), 0);
        if (n == SOCKET_ERROR) return -1;
#else
        ssize_t n = send(sock, buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return -1;
#endif
        sent += (size_t)n;
    }
    return 0;
}

/* ==========================================================================
 * 内部: TCP 受信 (最大 buf_max バイト、タイムアウト付き)
 * ========================================================================== */

static int tcp_recv_some(tls_socket_t sock, uint8_t *buf, size_t buf_max,
                          size_t *out_len, int timeout_ms)
{
    if (timeout_ms > 0) {
        int r = sock_wait_readable(sock, timeout_ms);
        if (r != 0) return r; /* 1=timeout, -1=error */
    }
#ifdef _WINDOWS
    int n = recv(sock, (char *)buf, (int)buf_max, 0);
    if (n <= 0) return -1;
#else
    ssize_t n = recv(sock, buf, buf_max, 0);
    if (n <= 0) return -1;
#endif
    *out_len = (size_t)n;
    return 0;
}

/* ==========================================================================
 * 内部: picotls コンテキスト初期化
 * ========================================================================== */

static void tls_ctx_init(tls_session_t *sess, int verify_cert, int debug)
{
    (void)debug;

    pen_memset(&sess->ctx, 0, sizeof(sess->ctx));

    /* minicrypto バックエンドの鍵交換アルゴリズムを設定 */
    sess->key_exchanges[0] = &ptls_minicrypto_x25519;
    sess->key_exchanges[1] = NULL;
    sess->ctx.key_exchanges = sess->key_exchanges;

    /* 暗号スイートを設定 (TLS 1.3 only) */
    sess->cipher_suites[0] = &ptls_minicrypto_aes128gcmsha256;
    sess->cipher_suites[1] = &ptls_minicrypto_aes256gcmsha384;
    sess->cipher_suites[2] = &ptls_minicrypto_chacha20poly1305sha256;
    sess->cipher_suites[3] = NULL;
    sess->ctx.cipher_suites = sess->cipher_suites;

    /* 乱数生成 */
    sess->ctx.random_bytes = ptls_minicrypto_random_bytes;

    /* 証明書検証: verify_cert=0 のときスキップ (IoT 向け簡易設定) */
    if (!verify_cert) {
        sess->ctx.verify_certificate = NULL;
    }
}

/* ==========================================================================
 * 内部: TLS ハンドシェイク (1往復分)
 *
 * in/in_len : サーバーからの受信データ (最初の呼び出しは 0 バイト)
 * 戻り値    : ptls_handshake() の戻り値をそのまま返す
 * ========================================================================== */

static int tls_handshake_step(tls_session_t *session,
                               ptls_handshake_properties_t *props,
                               tls_socket_t sock,
                               const uint8_t *in, size_t in_len,
                               int debug)
{
    uint8_t hs_buf[TLS_HANDSHAKE_BUF_SIZE];
    ptls_buffer_t sendbuf;
    ptls_buffer_init(&sendbuf, hs_buf, sizeof(hs_buf));

    size_t consumed = in_len;
    int ret = ptls_handshake(session->tls, &sendbuf, in, &consumed, props);

    if (sendbuf.off > 0) {
        int send_ret = tcp_send_all(sock, sendbuf.base, sendbuf.off);
        TLS_LOG_DBG(debug, "handshake sent %zu bytes\n", sendbuf.off);
        ptls_buffer_dispose(&sendbuf);
        if (send_ret != 0) {
            TLS_LOG_ERR("handshake send failed\n");
            return PTLS_ERROR_NO_MEMORY; /* 送信失敗を示す適当なエラー */
        }
    } else {
        ptls_buffer_dispose(&sendbuf);
    }

    return ret;
}

/* ==========================================================================
 * tls_connect
 * ========================================================================== */

int tls_connect(
    const char                  *host,
    int                          port,
    const char                  *sni,
    const tls_connect_options_t *opts,
    tls_session_t               *session
){
    if (!host || port <= 0 || !session) return -1;

    tls_connect_options_t resolved = TLS_CONNECT_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    int timeout_ms = (resolved.timeout_ms > 0) ? resolved.timeout_ms
                                                : TLS_TCP_CONNECT_TIMEOUT_MS;

    /* --- TCP 接続 --- */
    tls_socket_t sock = tcp_connect(host, port, timeout_ms);
    if (sock == TLS_INVALID_SOCKET) {
        TLS_LOG_ERR("TCP connect failed: %s:%d\n", host, port);
        return -1;
    }
    TLS_LOG_DBG(resolved.debug, "TCP connected: %s:%d\n", host, port);

    /* --- picotls コンテキスト初期化 --- */
    tls_ctx_init(session, resolved.verify_cert, resolved.debug);

    /* --- TLS ハンドル生成 --- */
    session->tls = ptls_client_new(&session->ctx);
    if (!session->tls) {
        TLS_LOG_ERR("ptls_client_new failed\n");
        sock_close(sock);
        return -1;
    }

    /* SNI 設定 */
    const char *sni_name = sni ? sni : host;
    if (ptls_set_server_name(session->tls, sni_name, pen_strlen(sni_name)) != 0) {
        TLS_LOG_ERR("ptls_set_server_name failed\n");
        ptls_free(session->tls);
        session->tls = NULL;
        sock_close(sock);
        return -1;
    }

    session->sock = sock;
    rbuf_reset(&g_rbuf);

    /* --- ALPN 設定 ---
     * opts->alpn に文字列が指定されている場合のみ ClientHello に ALPN 拡張を付加する。
     * MQTT over TLS: "mqtt"  (RFC 8422 / IANA 登録済み)
     * ALPN なし     : NULL のまま (デフォルト)
     */
    ptls_handshake_properties_t props;
    pen_memset(&props, 0, sizeof(props));

    ptls_iovec_t alpn_proto;
    if (resolved.alpn != NULL) {
        alpn_proto.base = (uint8_t *)(uintptr_t)resolved.alpn;
        alpn_proto.len  = pen_strlen(resolved.alpn);
        props.client.negotiated_protocols.list  = &alpn_proto;
        props.client.negotiated_protocols.count = 1;
        TLS_LOG_DBG(resolved.debug, "ALPN: \"%s\"\n", resolved.alpn);
    }

    /* --- TLS ハンドシェイク --- */
    uint8_t raw_in[TLS_RAW_BUF_SIZE];

    /* 1回目: 入力なしで ClientHello を生成・送信 */
    int ret = tls_handshake_step(session, &props, sock, raw_in, 0, resolved.debug);

    while (ret == PTLS_ERROR_IN_PROGRESS) {
        /* サーバーからデータを受信して続行 */
        size_t nread = 0;
        int r = tcp_recv_some(sock, raw_in, sizeof(raw_in), &nread, timeout_ms);
        if (r != 0) {
            TLS_LOG_ERR("handshake recv failed (r=%d)\n", r);
            tls_close(session);
            return -1;
        }

        ret = tls_handshake_step(session, &props, sock, raw_in, nread, resolved.debug);
    }

    if (ret != 0) {
        TLS_LOG_ERR("ptls_handshake error: %d\n", ret);
        tls_close(session);
        return -1;
    }

    TLS_LOG_INFO("TLS handshake complete: %s:%d\n", host, port);
    return 0;
}

/* ==========================================================================
 * tls_send
 * ========================================================================== */

int tls_send(tls_session_t *session, const uint8_t *buf, size_t len)
{
    if (!session || !session->tls || session->sock == TLS_INVALID_SOCKET) return -1;

    uint8_t enc_buf[TLS_RAW_BUF_SIZE + 512];
    ptls_buffer_t sendbuf;
    ptls_buffer_init(&sendbuf, enc_buf, sizeof(enc_buf));

    int ret = ptls_send(session->tls, &sendbuf, buf, len);
    if (ret != 0) {
        TLS_LOG_ERR("ptls_send error: %d\n", ret);
        ptls_buffer_dispose(&sendbuf);
        return -1;
    }

    int r = tcp_send_all(session->sock, sendbuf.base, sendbuf.off);
    ptls_buffer_dispose(&sendbuf);

    if (r != 0) {
        TLS_LOG_ERR("TCP send failed after ptls_send\n");
        return -1;
    }
    return 0;
}

/* ==========================================================================
 * tls_recv  —  内部バッファから平文を取り出す
 *              内部バッファが空なら TCP から受信して ptls_receive() を呼ぶ
 * ========================================================================== */

int tls_recv(
    tls_session_t *session,
    uint8_t       *buf,
    size_t         buf_max,
    size_t        *out_len,
    int            timeout_ms
){
    if (!session || !session->tls || session->sock == TLS_INVALID_SOCKET) return -1;

    *out_len = 0;

    /* 内部バッファに残りがあれば先に返す */
    if (rbuf_avail(&g_rbuf) > 0) {
        *out_len = rbuf_pop(&g_rbuf, buf, buf_max);
        return 0;
    }

    /* TCP から暗号データを受信して復号する */
    for (;;) {
        uint8_t raw[TLS_RAW_BUF_SIZE];
        size_t nread = 0;
        int r = tcp_recv_some(session->sock, raw, sizeof(raw), &nread, timeout_ms);
        if (r == 1) return 1;  /* タイムアウト */
        if (r < 0)  return -1; /* エラー */

        uint8_t plain[TLS_PLAIN_BUF_SIZE];
        ptls_buffer_t plainbuf;
        ptls_buffer_init(&plainbuf, plain, sizeof(plain));

        size_t consumed = nread;
        int ret = ptls_receive(session->tls, &plainbuf, raw, &consumed);

        if (ret != 0 && ret != PTLS_ERROR_IN_PROGRESS) {
            TLS_LOG_ERR("ptls_receive error: %d\n", ret);
            ptls_buffer_dispose(&plainbuf);
            return -1;
        }

        if (plainbuf.off > 0) {
            size_t take = (plainbuf.off < buf_max) ? plainbuf.off : buf_max;
            pen_memcpy(buf, plainbuf.base, take);
            *out_len = take;

            if (plainbuf.off > take)
                rbuf_push(&g_rbuf, plainbuf.base + take, plainbuf.off - take);

            ptls_buffer_dispose(&plainbuf);
            return 0;
        }
        ptls_buffer_dispose(&plainbuf);

        if (ret == 0) return 1; /* データなし・エラーなし → タイムアウト扱い */
    }
}

/* ==========================================================================
 * tls_recv_exact  —  ちょうど len バイト受信する
 * ========================================================================== */

int tls_recv_exact(
    tls_session_t *session,
    uint8_t       *buf,
    size_t         len,
    int            timeout_ms
){
    size_t recvd = 0;

    while (recvd < len) {
        size_t got = 0;
        int r = tls_recv(session, buf + recvd, len - recvd, &got, timeout_ms);
        if (r == 1) return 1;  /* タイムアウト */
        if (r < 0)  return -1;
        recvd += got;
    }
    return 0;
}

/* ==========================================================================
 * tls_close
 * ========================================================================== */

void tls_close(tls_session_t *session)
{
    if (!session) return;

    if (session->tls && session->sock != TLS_INVALID_SOCKET) {
        uint8_t alert_buf[256];
        ptls_buffer_t sendbuf;
        ptls_buffer_init(&sendbuf, alert_buf, sizeof(alert_buf));
        ptls_send_alert(session->tls, &sendbuf,
                        PTLS_ALERT_LEVEL_WARNING,
                        PTLS_ALERT_CLOSE_NOTIFY);
        if (sendbuf.off > 0)
            tcp_send_all(session->sock, sendbuf.base, sendbuf.off);
        ptls_buffer_dispose(&sendbuf);
    }

    if (session->tls) {
        ptls_free(session->tls);
        session->tls = NULL;
    }

    sock_close(session->sock);
    session->sock = TLS_INVALID_SOCKET;

    rbuf_reset(&g_rbuf);

    TLS_LOG_INFO("TLS connection closed.\n");
}

/* ==========================================================================
 * tls_is_connected
 * ========================================================================== */

int tls_is_connected(const tls_session_t *session)
{
    if (!session) return 0;
    return (session->sock != TLS_INVALID_SOCKET && session->tls != NULL) ? 1 : 0;
}
