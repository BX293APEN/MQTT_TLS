/*
 * mqtt_message_create.c
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

#include <penlib.h>
#include "../mqtt/mqtt_message_create.h"

/* ==========================================================================
 * 内部ヘルパー
 * ========================================================================== */

/**
 * MQTT Variable Byte Integer エンコード
 *
 * @param[in]  value  エンコードする値 (0 〜 268,435,455)
 * @param[out] buf    出力バッファ (最低4バイト確保すること)
 * @return エンコード後のバイト数 (1〜4)
 */
static int encode_variable_byte_integer(uint32_t value, uint8_t *buf)
{
    int len = 0;
    do {
        uint8_t byte = (uint8_t)(value % 128);
        value /= 128;
        if (value > 0) byte |= 0x80;
        buf[len++] = byte;
    } while (value > 0);
    return len;
}

/* ==========================================================================
 * mqtt_connect_message
 * ========================================================================== */

int mqtt_connect_message(
    mqtt_packet_t                *pkt,
    const char                   *client_id,
    const mqtt_connect_options_t *opts
){
    if (!pkt || !client_id || client_id[0] == '\0') return -1;

    mqtt_connect_options_t resolved = MQTT_CONNECT_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    uint16_t keep_alive = (resolved.keep_alive > 0) ? resolved.keep_alive : 60;
    uint8_t  conn_flags = resolved.clean_start ? 0x02 : 0x00;

    pen_memset(pkt, 0, sizeof(mqtt_packet_t));

    size_t client_id_len = pen_strlen(client_id);

    /* --- 可変ヘッダー + ペイロードを仮バッファに構築 --- */
    uint8_t body[MQTT_MAX_PACKET_SIZE];
    size_t  body_pos = 0;

    /* Protocol Name: "MQTT" (長さ付き) */
    const uint8_t proto_name[] = { 0x00, 0x04, 'M', 'Q', 'T', 'T' };
    pen_memcpy(body + body_pos, proto_name, sizeof(proto_name));
    body_pos += sizeof(proto_name);

    /* Protocol Version: 5 */
    body[body_pos++] = 0x05;

    /* Connect Flags */
    body[body_pos++] = conn_flags;

    /* Keep Alive (Big Endian) */
    body[body_pos++] = (uint8_t)(keep_alive >> 8);
    body[body_pos++] = (uint8_t)(keep_alive & 0xFF);

    /* MQTT 5.0 Properties Length = 0 (プロパティなし) */
    body[body_pos++] = 0x00;

    /* Payload: Client ID (長さ付き文字列) */
    body[body_pos++] = (uint8_t)(client_id_len >> 8);
    body[body_pos++] = (uint8_t)(client_id_len & 0xFF);
    pen_memcpy(body + body_pos, client_id, client_id_len);
    body_pos += client_id_len;

    /* --- 固定ヘッダー + 可変長整数 remaining_length を組み立て --- */
    size_t  out_pos = 0;
    uint8_t vbi[4];
    int     vbi_len = encode_variable_byte_integer((uint32_t)body_pos, vbi);

    /* Fixed Header: CONNECT (0x10) */
    pkt->data[out_pos++] = 0x10;
    pen_memcpy(pkt->data + out_pos, vbi, (size_t)vbi_len);
    out_pos += (size_t)vbi_len;

    if (out_pos + body_pos > MQTT_MAX_PACKET_SIZE) return -1;
    pen_memcpy(pkt->data + out_pos, body, body_pos);
    out_pos += body_pos;

    pkt->length = out_pos;

    if (resolved.debug) {
        mqtt_packet_dump(pkt, "CONNECT");
    }
    return 0;
}

/* ==========================================================================
 * mqtt_publish_message
 * ========================================================================== */

int mqtt_publish_message(
    mqtt_packet_t                *pkt,
    const char                   *topic,
    const char                   *payload,
    const mqtt_publish_options_t *opts
){
    if (!pkt || !topic || !payload) return -1;

    mqtt_publish_options_t resolved = MQTT_PUBLISH_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    int      qos      = (resolved.qos < 0 || resolved.qos > 2) ? 0 : resolved.qos;
    int      retain   = resolved.retain ? 1 : 0;
    uint16_t pkt_id   = (qos > 0) ? (resolved.packet_id ? resolved.packet_id : 1) : 0;

    size_t topic_len   = pen_strlen(topic);
    size_t payload_len = pen_strlen(payload);

    pen_memset(pkt, 0, sizeof(mqtt_packet_t));

    uint8_t body[MQTT_MAX_PACKET_SIZE];
    size_t  body_pos = 0;

    /* Topic Name (長さ付き文字列) */
    body[body_pos++] = (uint8_t)(topic_len >> 8);
    body[body_pos++] = (uint8_t)(topic_len & 0xFF);
    if (body_pos + topic_len > MQTT_MAX_PACKET_SIZE) return -1;
    pen_memcpy(body + body_pos, topic, topic_len);
    body_pos += topic_len;

    /* Packet Identifier (QoS 1/2 のみ) */
    if (qos > 0) {
        body[body_pos++] = (uint8_t)(pkt_id >> 8);
        body[body_pos++] = (uint8_t)(pkt_id & 0xFF);
    }

    /* MQTT 5.0 Properties Length = 0 */
    body[body_pos++] = 0x00;

    /* Payload */
    if (body_pos + payload_len > MQTT_MAX_PACKET_SIZE) return -1;
    pen_memcpy(body + body_pos, payload, payload_len);
    body_pos += payload_len;

    /* Fixed Header */
    uint8_t fixed_header = (uint8_t)(0x30 | (qos << 1) | retain);
    size_t  out_pos = 0;
    uint8_t vbi[4];
    int     vbi_len = encode_variable_byte_integer((uint32_t)body_pos, vbi);

    pkt->data[out_pos++] = fixed_header;
    pen_memcpy(pkt->data + out_pos, vbi, (size_t)vbi_len);
    out_pos += (size_t)vbi_len;

    if (out_pos + body_pos > MQTT_MAX_PACKET_SIZE) return -1;
    pen_memcpy(pkt->data + out_pos, body, body_pos);
    out_pos += body_pos;

    pkt->length = out_pos;

    if (resolved.debug) {
        mqtt_packet_dump(pkt, "PUBLISH");
    }
    return 0;
}

/* ==========================================================================
 * mqtt_subscribe_message
 * ========================================================================== */

int mqtt_subscribe_message(
    mqtt_packet_t                  *pkt,
    const char                     *topic,
    const mqtt_subscribe_options_t *opts
){
    if (!pkt || !topic) return -1;

    mqtt_subscribe_options_t resolved = MQTT_SUBSCRIBE_OPTIONS_DEFAULT;
    if (opts) resolved = *opts;

    int      qos    = (resolved.qos < 0 || resolved.qos > 2) ? 0 : resolved.qos;
    uint16_t pkt_id = resolved.packet_id ? resolved.packet_id : 1;

    size_t topic_len = pen_strlen(topic);

    pen_memset(pkt, 0, sizeof(mqtt_packet_t));

    uint8_t body[MQTT_MAX_PACKET_SIZE];
    size_t  body_pos = 0;

    /* Packet Identifier */
    body[body_pos++] = (uint8_t)(pkt_id >> 8);
    body[body_pos++] = (uint8_t)(pkt_id & 0xFF);

    /* MQTT 5.0 Properties Length = 0 */
    body[body_pos++] = 0x00;

    /* Topic Filter (長さ付き文字列) + Subscription Options */
    body[body_pos++] = (uint8_t)(topic_len >> 8);
    body[body_pos++] = (uint8_t)(topic_len & 0xFF);
    if (body_pos + topic_len + 1 > MQTT_MAX_PACKET_SIZE) return -1;
    pen_memcpy(body + body_pos, topic, topic_len);
    body_pos += topic_len;
    body[body_pos++] = (uint8_t)(qos & 0x03); /* Subscription Options: QoS bits */

    /* Fixed Header: SUBSCRIBE (0x82) */
    size_t  out_pos = 0;
    uint8_t vbi[4];
    int     vbi_len = encode_variable_byte_integer((uint32_t)body_pos, vbi);

    pkt->data[out_pos++] = 0x82;
    pen_memcpy(pkt->data + out_pos, vbi, (size_t)vbi_len);
    out_pos += (size_t)vbi_len;

    if (out_pos + body_pos > MQTT_MAX_PACKET_SIZE) return -1;
    pen_memcpy(pkt->data + out_pos, body, body_pos);
    out_pos += body_pos;

    pkt->length = out_pos;

    if (resolved.debug) {
        mqtt_packet_dump(pkt, "SUBSCRIBE");
    }
    return 0;
}

/* ==========================================================================
 * mqtt_disconnect_message
 * ========================================================================== */

int mqtt_disconnect_message(mqtt_packet_t *pkt)
{
    if (!pkt) return -1;
    pen_memset(pkt, 0, sizeof(mqtt_packet_t));
    /* MQTT 5.0 DISCONNECT: 固定ヘッダ (0xE0) + remaining=2 + reason=0 + prop_len=0 */
    pkt->data[0] = 0xE0;
    pkt->data[1] = 0x02;
    pkt->data[2] = 0x00; /* Reason Code: Normal Disconnection */
    pkt->data[3] = 0x00; /* Properties Length = 0 */
    pkt->length  = 4;
    return 0;
}

/* ==========================================================================
 * mqtt_packet_dump
 * ========================================================================== */

void mqtt_packet_dump(const mqtt_packet_t *pkt, const char *label)
{
    if (!pkt) return;
    const char *lbl = label ? label : "MQTT";

    /* ダンプ文字列を一時バッファに組み立ててから pen_log で出力する */
    char hexbuf[256];
    size_t pos = 0;
    for (size_t i = 0; i < pkt->length && i < 64; i++) {
        pos += (size_t)pen_snprintf(hexbuf + pos, sizeof(hexbuf) - pos,
                                    " %02X", pkt->data[i]);
        if (pos >= sizeof(hexbuf) - 4) break;
    }
    if (pkt->length > 64 && pos < sizeof(hexbuf) - 4) {
        pen_snprintf(hexbuf + pos, sizeof(hexbuf) - pos, " ...");
    }

    pen_log(stderr, "MQTT", "DEBUG", lbl, "len=%zu bytes:%s\n",
            pkt->length, hexbuf);
}
