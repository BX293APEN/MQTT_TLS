/*
 * mqtt_message_create.h
 *
 * MQTT 5.0 パケット生成ライブラリ (TCP 版)
 *
 * 責務: MQTTパケットのバイト列生成のみ。
 *       送受信・接続管理は mqtt_client.c が担当する。
 *
 * 対応パケット:
 *   - CONNECT
 *   - PUBLISH  (QoS 0 / 1 / 2)
 *   - SUBSCRIBE
 *   - DISCONNECT
 */

#ifndef MQTT_MESSAGE_CREATE_H
#define MQTT_MESSAGE_CREATE_H

/* すべての標準型・OS API は penlib.h 経由で提供する */
#include <penlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 定数 ===== */

#define MQTT_MAX_PACKET_SIZE 4096
#define MQTT_MAX_TOPIC_LEN   256
#define MQTT_MAX_MSG_LEN     1024
#define MQTT_MAX_CLIENT_LEN  64

/* ===== MQTTパケット保持構造体 ===== */

/**
 * MQTTパケットのバイト列と有効長を保持する構造体。
 * mqtt_publish_message() / mqtt_subscribe_message() / mqtt_connect_message()
 * の出力として使用する。
 */
typedef struct {
    uint8_t  data[MQTT_MAX_PACKET_SIZE]; /* パケットのバイト列 */
    size_t   length;                     /* パケットの有効バイト数 */
} mqtt_packet_t;

/* ===== CONNECT オプション ===== */

/**
 * CONNECT パケット生成オプション。
 */
typedef struct {
    uint16_t keep_alive;   /* Keep Alive 秒数 (0=デフォルト60秒) */
    int      clean_start;  /* 1=Clean Start フラグ有効 (デフォルト1) */
    int      debug;        /* 1=デバッグ出力有効 */
} mqtt_connect_options_t;

#define MQTT_CONNECT_OPTIONS_DEFAULT { 60, 1, 0 }

/* ===== PUBLISH オプション ===== */

/**
 * PUBLISH パケット生成オプション。
 */
typedef struct {
    int      qos;        /* QoSレベル 0/1/2 (デフォルト0) */
    int      retain;     /* 1=RETAINフラグ有効 (デフォルト0) */
    uint16_t packet_id;  /* パケットID (QoS 1/2のみ使用。0=自動で1を使用) */
    int      debug;      /* 1=デバッグ出力有効 */
} mqtt_publish_options_t;

#define MQTT_PUBLISH_OPTIONS_DEFAULT { 0, 0, 0, 0 }

/* ===== SUBSCRIBE オプション ===== */

/**
 * SUBSCRIBE パケット生成オプション。
 */
typedef struct {
    int      qos;        /* QoSレベル 0/1/2 (デフォルト0) */
    uint16_t packet_id;  /* パケットID (0=自動で1を使用) */
    int      debug;      /* 1=デバッグ出力有効 */
} mqtt_subscribe_options_t;

#define MQTT_SUBSCRIBE_OPTIONS_DEFAULT { 0, 0, 0 }

/* ===== API ===== */

/**
 * MQTT CONNECT パケットを生成する (MQTT 5.0)
 *
 * @param[out] pkt       生成したパケットの格納先
 * @param[in]  client_id MQTTクライアントID (必須、空文字列不可)
 * @param[in]  opts      生成オプション (NULLのときデフォルト値を使用)
 * @return 0=成功, -1=エラー
 */
int mqtt_connect_message(
    mqtt_packet_t              *pkt,
    const char                 *client_id,
    const mqtt_connect_options_t *opts
);

/**
 * MQTT PUBLISH パケットを生成する (MQTT 5.0)
 *
 * @param[out] pkt     生成したパケットの格納先
 * @param[in]  topic   パブリッシュ先トピック名
 * @param[in]  payload ペイロード文字列
 * @param[in]  opts    生成オプション (NULLのときデフォルト値を使用)
 * @return 0=成功, -1=エラー
 */
int mqtt_publish_message(
    mqtt_packet_t               *pkt,
    const char                  *topic,
    const char                  *payload,
    const mqtt_publish_options_t *opts
);

/**
 * MQTT SUBSCRIBE パケットを生成する (MQTT 5.0)
 *
 * @param[out] pkt   生成したパケットの格納先
 * @param[in]  topic 購読するトピック名
 * @param[in]  opts  生成オプション (NULLのときデフォルト値を使用)
 * @return 0=成功, -1=エラー
 */
int mqtt_subscribe_message(
    mqtt_packet_t                 *pkt,
    const char                    *topic,
    const mqtt_subscribe_options_t *opts
);

/**
 * MQTT DISCONNECT パケットを生成する (MQTT 5.0)
 *
 * @param[out] pkt 生成したパケットの格納先
 * @return 0=成功, -1=エラー
 */
int mqtt_disconnect_message(mqtt_packet_t *pkt);

/**
 * パケット内容をHEXダンプ表示するユーティリティ
 *
 * @param[in] pkt   ダンプ対象パケット
 * @param[in] label 表示ラベル (NULLのとき "MQTT" を使用)
 */
void mqtt_packet_dump(const mqtt_packet_t *pkt, const char *label);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_MESSAGE_CREATE_H */
