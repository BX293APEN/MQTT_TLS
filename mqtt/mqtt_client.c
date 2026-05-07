/*
 * mqtt_client.c
 *
 * MQTT over TLS クライアント — 送受信・接続制御レイヤ
 *
 * 責務: TLSセッションを使ったMQTT over TLSのデータ送受信。
 *       MQTTパケットのバイト列生成は mqtt_message_create.c に委譲する。
 *       TLS 操作は tls/tlslib.h/c で抽象化している。
 *       標準ライブラリ操作は pen_xxxx() 経由で行う。
 *
 * TLS上のMQTTパケット受信方式:
 *   MQTTパケットは可変長のため、以下の手順で読み出す。
 *     1) Fixed Header の 1バイト目 (パケット種別) を受信
 *     2) Variable Byte Integer (Remaining Length) を受信 (最大4バイト)
 *     3) Remaining Length の分だけ残りのバイト列を受信
 *   この実装では内部バッファ (MQTT_TLS_BUF_SIZE) にパケット全体を格納してから
 *   パーサーへ渡す。tls_recv_exact() でバイト単位の正確な受信を行う。
 *
 * [修正履歴]
 *   - MQTT_LOG_INFO / MQTT_LOG_ERR: カンマ演算子による二文展開を
 *     do { } while(0) に統一。if/else 単文での誤動作を防ぐ。
 *   - mqtt_connection / mqtt_data_send / mqtt_subscribe / mqtt_recv_message:
 *     スタック上の 64KB バッファ (uint8_t buf[MQTT_TLS_BUF_SIZE]) を
 *     ヒープ確保に変更。Windows デフォルトスタック 1MB では TLS ハンドシェイク
 *     バッファと合算してスタックオーバーフローが発生していた。
 *     全リターンパスで pen_free(buf) が呼ばれるよう goto err パターンを採用。
 */

#include <penlib.h>
#include "mqtt_client.h"
#include "mqtt_message_create.h"

/* ==========================================================================
 * 定数
 * ========================================================================== */

#define MQTT_TLS_BUF_SIZE       65536   /* 受信バッファ最大サイズ */

#define MQTT_PUB_DEFAULT_TOPIC      "mqttTest"
#define MQTT_PUB_DEFAULT_MESSAGE    "Hello from MQTT/TLS client!"
#define MQTT_PUB_DEFAULT_CLIENT_ID  "mqttTlsPublisher"
#define MQTT_SUB_DEFAULT_TOPIC      "mqttTest"
#define MQTT_SUB_DEFAULT_CLIENT_ID  "mqttTlsSubscriber"

/* ==========================================================================
 * ログマクロ
 *
 * [FIX] MQTT_LOG_INFO / MQTT_LOG_ERR をカンマ演算子から do { } while(0) に変更。
 *   旧実装はカンマ演算子で2つの pen_fprintf を連結していたため、
 *   if (cond) MQTT_LOG_INFO(...); else ... のような文脈で else が
 *   2番目の pen_fprintf に対応してしまう潜在的バグがあった。
 * ========================================================================== */

#define MQTT_LOG_INFO(tag, ...) \
    do { pen_fprintf(stderr, "[mqtt/%s] ", tag); \
         pen_fprintf(stderr, __VA_ARGS__); } while(0)
#define MQTT_LOG_ERR(tag, ...) \
    do { pen_fprintf(stderr, "[mqtt/%s][ERR] ", tag); \
         pen_fprintf(stderr, __VA_ARGS__); } while(0)
#define MQTT_LOG_DBG(tag, debug, ...) \
    do { if (debug) { pen_fprintf(stderr, "[mqtt/%s][DBG] ", tag); \
                      pen_fprintf(stderr, __VA_ARGS__); } } while(0)

/* ==========================================================================
 * 内部ヘルパー: Variable Byte Integer デコード
 * ========================================================================== */

static int decode_variable_byte_integer(
    const uint8_t *data,
    size_t         data_len,
    size_t        *offset,
    uint32_t      *out_val
){
    uint32_t value      = 0;
    uint32_t multiplier = 1;

    do {
        if (*offset >= data_len) return -1;
        uint8_t byte = data[(*offset)++];
        value       += (byte & 0x7F) * multiplier;
        multiplier  *= 128;
        if (multiplier > 128 * 128 * 128) return -1;
        if ((byte & 0x80) == 0) break;
    } while (1);

    *out_val = value;
    return 0;
}

/* ==========================================================================
 * 内部ヘルパー: TLSセッションからMQTTパケット1個を受信する
 *
 * Fixed Header + Remaining Length フィールドをバイト単位で受信し、
 * その後 Remaining Length ぶんのペイロードを受信してバッファに格納する。
 *
 * 戻り値: 受信バイト数 (>0), 0=タイムアウト, -1=エラー/切断
 * ========================================================================== */

static int recv_mqtt_packet_tls(
    tls_session_t *tls_sess,
    uint8_t       *buf,
    size_t         buf_max,
    int            timeout_ms
){
    /* Step 1: Fixed Header 1バイト目 */
    int r = tls_recv_exact(tls_sess, buf, 1, timeout_ms);
    if (r != 0) return (r == 1) ? 0 : -1;

    /* Step 2: Variable Byte Integer (Remaining Length) を逐次受信 */
    size_t   pos        = 1;
    uint32_t remaining  = 0;
    uint32_t multiplier = 1;

    for (int i = 0; i < 4; i++) {
        if (pos >= buf_max) return -1;
        r = tls_recv_exact(tls_sess, buf + pos, 1, timeout_ms);
        if (r != 0) return (r == 1) ? 0 : -1;
        uint8_t byte = buf[pos++];
        remaining   += (byte & 0x7F) * multiplier;
        multiplier  *= 128;
        if ((byte & 0x80) == 0) break;
    }

    /* Step 3: Remaining Length 分のバイト列を受信 */
    if (remaining == 0) return (int)pos;
    if (pos + remaining > buf_max) return -1;

    r = tls_recv_exact(tls_sess, buf + pos, (size_t)remaining, timeout_ms);
    if (r != 0) return (r == 1) ? 0 : -1;

    return (int)(pos + remaining);
}

/* ==========================================================================
 * パーサー実装
 * ========================================================================== */

int mqtt_parse_connack(const uint8_t *data, size_t data_len, mqtt_connack_t *out)
{
    if (!data || !out)       return -1;
    if (data_len < 4)        return -1;
    if (data[0] != 0x20)     return -1;

    size_t   offset = 1;
    uint32_t remaining;
    if (decode_variable_byte_integer(data, data_len, &offset, &remaining) != 0) return -1;
    if (offset + remaining > data_len) return -1;

    out->session_present = data[offset] & 0x01;
    out->reason_code     = data[offset + 1];
    return 0;
}

int mqtt_parse_suback(const uint8_t *data, size_t data_len, mqtt_suback_t *out)
{
    if (!data || !out)   return -1;
    if (data_len < 5)    return -1;
    if (data[0] != 0x90) return -1;

    size_t   offset = 1;
    uint32_t remaining;
    if (decode_variable_byte_integer(data, data_len, &offset, &remaining) != 0) return -1;
    if (offset + remaining > data_len) return -1;

    out->packet_id = (uint16_t)((data[offset] << 8) | data[offset + 1]);
    offset += 2;

    uint32_t prop_len;
    if (decode_variable_byte_integer(data, data_len, &offset, &prop_len) != 0) return -1;
    offset += prop_len;

    if (offset >= data_len) return -1;
    out->reason_code = data[offset];
    return 0;
}

mqtt_message_t *mqtt_parse_publish(const uint8_t *data, size_t data_len)
{
    if (!data || data_len < 3)          return NULL;
    if ((data[0] & 0xF0) != 0x30)       return NULL;

    int    qos    = (data[0] >> 1) & 0x03;
    size_t offset = 1;

    uint32_t remaining;
    if (decode_variable_byte_integer(data, data_len, &offset, &remaining) != 0) return NULL;
    if (offset + remaining > data_len)  return NULL;

    size_t end = offset + remaining;

    if (offset + 2 > end) return NULL;
    uint16_t topic_len = (uint16_t)((data[offset] << 8) | data[offset + 1]);
    offset += 2;
    if (offset + topic_len > end) return NULL;

    mqtt_message_t *msg = (mqtt_message_t *)pen_calloc(1, sizeof(mqtt_message_t));
    if (!msg) return NULL;

    size_t copy_len = (topic_len < sizeof(msg->topic) - 1) ? topic_len : sizeof(msg->topic) - 1;
    pen_memcpy(msg->topic, data + offset, copy_len);
    msg->topic[copy_len] = '\0';
    offset  += topic_len;
    msg->qos = qos;

    if (qos > 0) {
        if (offset + 2 > end) { pen_free(msg); return NULL; }
        msg->packet_id = (uint16_t)((data[offset] << 8) | data[offset + 1]);
        offset += 2;
    }

    uint32_t prop_len;
    if (decode_variable_byte_integer(data, data_len, &offset, &prop_len) != 0) {
        pen_free(msg); return NULL;
    }
    offset += prop_len;

    if (offset > end) { pen_free(msg); return NULL; }

    msg->payload_len = end - offset;
    if (msg->payload_len > 0) {
        msg->payload = (uint8_t *)pen_malloc(msg->payload_len + 1);
        if (!msg->payload) { pen_free(msg); return NULL; }
        pen_memcpy(msg->payload, data + offset, msg->payload_len);
        msg->payload[msg->payload_len] = '\0';
    }

    return msg;
}

void mqtt_message_free(mqtt_message_t *msg)
{
    if (!msg) return;
    pen_free(msg->payload);
    pen_free(msg);
}

/* ==========================================================================
 * mqtt_connection  —  TLS接続 + CONNECT → CONNACK
 *
 * [FIX] CONNACK 受信バッファ (64KB) をスタックからヒープ確保に変更。
 * ========================================================================== */

int mqtt_connection(
    const char                      *host,
    int                              port,
    const char                      *client_id,
    const mqtt_connection_options_t *opts,
    mqtt_session_t                  *session
){
    if (!host || !client_id || !session) return -1;

    mqtt_connection_options_t resolved = MQTT_CONNECTION_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    /* TLS 接続オプション構築 */
    tls_connect_options_t tls_opts = {
        .verify_cert = resolved.verify_cert,
        .timeout_ms  = MQTT_CLIENT_CONNECT_TIMEOUT_MS,
        .debug       = resolved.debug
    };

    /* TLS 接続 (TCP + TLS ハンドシェイク) */
    int r = tls_connect(host, port, resolved.sni, &tls_opts, &session->tls);
    if (r != 0) {
        MQTT_LOG_ERR("connect", "TLS connect failed: %s:%d\n", host, port);
        return -1;
    }
    MQTT_LOG_DBG("connect", resolved.debug,
                 "TLS connected: %s:%d\n", host, port);

    /* CONNECT パケット生成 */
    mqtt_connect_options_t build_opts = {
        .keep_alive  = (uint16_t)(resolved.keep_alive > 0 ? resolved.keep_alive : 60),
        .clean_start = resolved.clean_start,
        .debug       = resolved.debug
    };
    mqtt_packet_t pkt;
    if (mqtt_connect_message(&pkt, client_id, &build_opts) != 0) {
        MQTT_LOG_ERR("connect", "CONNECT packet build failed.\n");
        tls_close(&session->tls);
        return -1;
    }

    /* CONNECT 送信 (TLS 経由) */
    if (tls_send(&session->tls, pkt.data, pkt.length) != 0) {
        MQTT_LOG_ERR("connect", "CONNECT send failed.\n");
        tls_close(&session->tls);
        return -1;
    }
    MQTT_LOG_INFO("connect", "CONNECT sent: client_id=%s\n", client_id);

    /* [FIX] CONNACK 受信バッファをヒープ確保 */
    uint8_t *buf = (uint8_t *)pen_malloc(MQTT_TLS_BUF_SIZE);
    if (!buf) {
        MQTT_LOG_ERR("connect", "malloc failed for recv buf.\n");
        tls_close(&session->tls);
        return -1;
    }

    int n = recv_mqtt_packet_tls(&session->tls, buf, MQTT_TLS_BUF_SIZE,
                                 MQTT_CLIENT_RECV_TIMEOUT_MS);
    if (n <= 0) {
        MQTT_LOG_ERR("connect", "CONNACK timeout or error.\n");
        pen_free(buf);
        tls_close(&session->tls);
        return -1;
    }

    mqtt_connack_t connack;
    if (mqtt_parse_connack(buf, (size_t)n, &connack) != 0) {
        MQTT_LOG_ERR("connect", "CONNACK parse failed.\n");
        pen_free(buf);
        tls_close(&session->tls);
        return -1;
    }
    pen_free(buf);

    if (connack.reason_code != 0x00) {
        MQTT_LOG_ERR("connect", "CONNACK rejected (reason=0x%02X)\n",
                     connack.reason_code);
        tls_close(&session->tls);
        return -1;
    }

    MQTT_LOG_INFO("connect", "CONNACK: session_present=%d\n",
                  connack.session_present);
    return 0;
}

/* ==========================================================================
 * mqtt_data_send  —  PUBLISH 送信
 *
 * [FIX] QoS 1/2 の PUBACK 受信バッファをヒープ確保に変更。
 * ========================================================================== */

int mqtt_data_send(
    mqtt_session_t            *session,
    const char                *topic,
    const char                *payload,
    const mqtt_send_options_t *opts
){
    if (!session || !topic || !payload) return -1;
    if (!tls_is_connected(&session->tls)) {
        MQTT_LOG_ERR("send", "no active TLS session (call mqtt_connection first).\n");
        return -1;
    }

    mqtt_send_options_t resolved = MQTT_SEND_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    mqtt_publish_options_t build_opts = {
        .qos       = resolved.qos,
        .retain    = resolved.retain,
        .packet_id = 0,
        .debug     = resolved.debug
    };
    mqtt_packet_t pkt;
    if (mqtt_publish_message(&pkt, topic, payload, &build_opts) != 0) {
        MQTT_LOG_ERR("send", "PUBLISH packet build failed.\n");
        return -1;
    }

    if (tls_send(&session->tls, pkt.data, pkt.length) != 0) {
        MQTT_LOG_ERR("send", "PUBLISH send failed.\n");
        return -1;
    }
    MQTT_LOG_INFO("send", "PUBLISH: topic=%s\n", topic);

    /* QoS 1/2: PUBACK / PUBREC を受信する */
    if (resolved.qos > 0) {
        /* [FIX] バッファをヒープ確保 */
        uint8_t *buf = (uint8_t *)pen_malloc(MQTT_TLS_BUF_SIZE);
        if (!buf) {
            MQTT_LOG_ERR("send", "malloc failed for PUBACK buf.\n");
            return -1;
        }
        int n = recv_mqtt_packet_tls(&session->tls, buf, MQTT_TLS_BUF_SIZE,
                                     MQTT_CLIENT_RECV_TIMEOUT_MS);
        if (n <= 0) {
            MQTT_LOG_ERR("send", "PUBACK/PUBREC timeout.\n");
            pen_free(buf);
            return -1;
        }
        MQTT_LOG_INFO("send", "PUBACK/PUBREC received: type=0x%02X\n", buf[0]);
        pen_free(buf);
    }

    return 0;
}

/* ==========================================================================
 * mqtt_subscribe  —  SUBSCRIBE 送信 + SUBACK 受信
 *
 * [FIX] SUBACK 受信バッファをヒープ確保に変更。
 * ========================================================================== */

int mqtt_subscribe(
    mqtt_session_t            *session,
    const char                *topic,
    int                        qos,
    const mqtt_recv_options_t *opts
){
    if (!session || !topic) return -1;
    if (!tls_is_connected(&session->tls)) {
        MQTT_LOG_ERR("subscribe", "no active TLS session.\n");
        return -1;
    }

    mqtt_recv_options_t resolved = MQTT_RECV_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    int timeout_ms = (resolved.timeout_ms > 0)
                   ? resolved.timeout_ms
                   : MQTT_CLIENT_RECV_TIMEOUT_MS;

    mqtt_subscribe_options_t build_opts = {
        .qos       = qos,
        .packet_id = 0,
        .debug     = resolved.debug
    };
    mqtt_packet_t pkt;
    if (mqtt_subscribe_message(&pkt, topic, &build_opts) != 0) {
        MQTT_LOG_ERR("subscribe", "SUBSCRIBE packet build failed.\n");
        return -1;
    }

    if (tls_send(&session->tls, pkt.data, pkt.length) != 0) {
        MQTT_LOG_ERR("subscribe", "SUBSCRIBE send failed.\n");
        return -1;
    }
    MQTT_LOG_INFO("subscribe", "SUBSCRIBE: topic=%s\n", topic);

    /* [FIX] SUBACK 受信バッファをヒープ確保 */
    uint8_t *buf = (uint8_t *)pen_malloc(MQTT_TLS_BUF_SIZE);
    if (!buf) {
        MQTT_LOG_ERR("subscribe", "malloc failed for SUBACK buf.\n");
        return -1;
    }

    int n = recv_mqtt_packet_tls(&session->tls, buf, MQTT_TLS_BUF_SIZE, timeout_ms);
    if (n <= 0) {
        MQTT_LOG_ERR("subscribe", "SUBACK timeout or error.\n");
        pen_free(buf);
        return -1;
    }

    mqtt_suback_t suback;
    if (mqtt_parse_suback(buf, (size_t)n, &suback) != 0) {
        MQTT_LOG_ERR("subscribe", "SUBACK parse failed (type=0x%02X).\n", buf[0]);
        pen_free(buf);
        return -1;
    }
    pen_free(buf);

    MQTT_LOG_INFO("subscribe", "SUBACK: reason=0x%02X\n", suback.reason_code);
    return 0;
}

/* ==========================================================================
 * mqtt_recv_message  —  PUBLISH を1件受信する
 *
 * [FIX] 受信バッファをヒープ確保に変更。
 * ========================================================================== */

int mqtt_recv_message(
    mqtt_session_t            *session,
    mqtt_message_t           **out_msg,
    const mqtt_recv_options_t *opts
){
    if (!session || !out_msg) return -1;
    if (!tls_is_connected(&session->tls)) return -1;

    mqtt_recv_options_t resolved = MQTT_RECV_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    int timeout_ms = (resolved.timeout_ms > 0)
                   ? resolved.timeout_ms
                   : MQTT_CLIENT_RECV_TIMEOUT_MS;

    *out_msg = NULL;

    /* [FIX] 受信バッファをヒープ確保 */
    uint8_t *buf = (uint8_t *)pen_malloc(MQTT_TLS_BUF_SIZE);
    if (!buf) {
        MQTT_LOG_ERR("recv", "malloc failed for recv buf.\n");
        return -1;
    }

    int result = -1;

    for (;;) {
        int n = recv_mqtt_packet_tls(&session->tls, buf, MQTT_TLS_BUF_SIZE, timeout_ms);
        if (n == 0) { result = 1; break; }  /* タイムアウト */
        if (n < 0)  { result = -1; break; } /* 切断 / エラー */

        /* PUBLISH パケット (上位4bit = 0x3) */
        if ((buf[0] & 0xF0) == 0x30) {
            mqtt_message_t *msg = mqtt_parse_publish(buf, (size_t)n);
            if (!msg) {
                MQTT_LOG_ERR("recv", "PUBLISH parse failed.\n");
                result = -1;
                break;
            }
            MQTT_LOG_DBG("recv", resolved.debug,
                         "PUBLISH: topic=%s payload_len=%zu\n",
                         msg->topic, msg->payload_len);
            *out_msg = msg;
            result = 0;
            break;
        }

        /* PINGRESP, SUBACK 等の非 PUBLISH パケットはスキップ */
        MQTT_LOG_DBG("recv", resolved.debug,
                     "non-PUBLISH skipped: type=0x%02X\n", buf[0]);
    }

    pen_free(buf);
    return result;
}

/* ==========================================================================
 * mqtt_disconnect  —  DISCONNECT 送信 + TLSセッションクローズ
 * ========================================================================== */

void mqtt_disconnect(mqtt_session_t *session)
{
    if (!session || !tls_is_connected(&session->tls)) return;

    mqtt_packet_t pkt;
    if (mqtt_disconnect_message(&pkt) == 0) {
        tls_send(&session->tls, pkt.data, pkt.length);
    }
    tls_close(&session->tls);
    MQTT_LOG_INFO("disconnect", "disconnected.\n");
}

/* ==========================================================================
 * mqtt_publisher  —  高レベルAPI: 1件 PUBLISH して終了
 * ========================================================================== */

int mqtt_publisher(
    const char                     *host,
    int                             port,
    int                             debug,
    const mqtt_publisher_options_t *opts
){
    if (!host) return -1;

    mqtt_publisher_options_t resolved = MQTT_PUBLISHER_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;
    resolved.debug = debug;

    const char *topic     = resolved.topic     ? resolved.topic     : MQTT_PUB_DEFAULT_TOPIC;
    const char *message   = resolved.message   ? resolved.message   : MQTT_PUB_DEFAULT_MESSAGE;
    const char *client_id = resolved.client_id ? resolved.client_id : MQTT_PUB_DEFAULT_CLIENT_ID;

    MQTT_LOG_INFO("pub", "server=%s:%d topic=%s\n", host, port, topic);

    mqtt_session_t sess = MQTT_SESSION_INIT;

    mqtt_connection_options_t conn_opts = MQTT_CONNECTION_OPTIONS_DEFAULT;
    conn_opts.debug       = debug;
    conn_opts.verify_cert = resolved.verify_cert;
    conn_opts.sni         = resolved.sni;

    if (mqtt_connection(host, port, client_id, &conn_opts, &sess) != 0) {
        return -1;
    }

    mqtt_send_options_t send_opts = MQTT_SEND_OPTIONS_DEFAULT;
    send_opts.qos    = resolved.qos;
    send_opts.retain = resolved.retain;
    send_opts.debug  = debug;

    int ret = mqtt_data_send(&sess, topic, message, &send_opts);

    mqtt_disconnect(&sess);
    return ret;
}

/* ==========================================================================
 * mqtt_subscriber  —  高レベルAPI: SUBSCRIBE してメッセージを受信
 * ========================================================================== */

int mqtt_subscriber(
    const char                      *host,
    int                              port,
    int                             debug,
    int                              loop,
    const mqtt_subscriber_options_t *opts,
    mqtt_message_t                 **out_msg
){
    if (!host) return -1;

    mqtt_subscriber_options_t resolved = MQTT_SUBSCRIBER_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;
    resolved.debug = debug;

    const char *topic     = resolved.topic     ? resolved.topic     : MQTT_SUB_DEFAULT_TOPIC;
    const char *client_id = resolved.client_id ? resolved.client_id : MQTT_SUB_DEFAULT_CLIENT_ID;
    int timeout_ms        = (resolved.timeout_ms > 0) ? resolved.timeout_ms : 30000;

    MQTT_LOG_INFO("sub", "server=%s:%d topic=%s loop=%d\n",
                  host, port, topic, loop);

    mqtt_session_t sess = MQTT_SESSION_INIT;

    mqtt_connection_options_t conn_opts = MQTT_CONNECTION_OPTIONS_DEFAULT;
    conn_opts.debug       = debug;
    conn_opts.verify_cert = resolved.verify_cert;
    conn_opts.sni         = resolved.sni;

    if (mqtt_connection(host, port, client_id, &conn_opts, &sess) != 0) {
        return -1;
    }

    mqtt_recv_options_t recv_opts = { timeout_ms, debug };

    if (mqtt_subscribe(&sess, topic, resolved.qos, &recv_opts) != 0) {
        mqtt_disconnect(&sess);
        return -1;
    }

    int ret = 0;

    if (loop) {
        for (;;) {
            mqtt_message_t *msg = NULL;
            int r = mqtt_recv_message(&sess, &msg, &recv_opts);
            if (r == 1) {
                MQTT_LOG_INFO("sub", "receive timeout, exiting loop.\n");
                break;
            }
            if (r < 0) {
                MQTT_LOG_ERR("sub", "receive error / disconnected.\n");
                ret = -1;
                break;
            }
            if (msg) {
                pen_printf("%s : %.*s\n",
                           msg->topic,
                           (int)msg->payload_len,
                           msg->payload ? (char *)msg->payload : "");
                mqtt_message_free(msg);
            }
        }
    } else {
        mqtt_message_t *msg = NULL;
        int r = mqtt_recv_message(&sess, &msg, &recv_opts);
        if (r == 0 && msg) {
            if (out_msg) *out_msg = msg;
            else          mqtt_message_free(msg);
        } else {
            ret = (r == 1) ? 0 : -1;
        }
    }

    mqtt_disconnect(&sess);
    return ret;
}
