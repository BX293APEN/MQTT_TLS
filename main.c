/*
 * main.c
 *
 * MQTT over TLS クライアント — エントリポイント
 *
 * 使い方:
 *   pub: ./mqtt_tls pub <topic> <message> [client_id]
 *   sub: ./mqtt_tls sub <topic> [client_id]
 *
 * デフォルト接続先: localhost:8883 (MQTT over TLS 標準ポート)
 *
 * 移植:
 *   Windows: _WINDOWS が自動定義される (CMakeLists.txt 参照)。
 *            ws2_32.lib / bcrypt.lib のリンクが必要。
 *   Linux / macOS: 追加リンクなし (pthread 使用なし)。
 *
 * TLS 証明書検証:
 *   デフォルトは verify_cert=0 (検証スキップ)。
 *   本番環境では mqtt_publisher_options_t.verify_cert = 1 に変更すること。
 */

#include <penlib.h>
#include "mqtt/mqtt_client.h"

int main(int argc, char **argv)
{
#ifdef _WINDOWS
    WSADATA wsaData = { 0 };
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

    if (argc < 3) {
        pen_fprintf(stderr,
            "Usage:\n"
            "  %s pub <topic> <message> [client_id]\n"
            "  %s sub <topic> [client_id]\n",
            argv[0], argv[0]);
        return 1;
    }

    const char *host   = "192.168.10.64"; /* 接続先ブローカーアドレス */
    int         port   = 8883;            /* MQTT over TLS 標準ポート */
    const char *mode   = argv[1];
    const char *topic  = argv[2];
    int         debug  = 0;
    int         ret    = 1;

    if (pen_strcmp(mode, "pub") == 0 && argc >= 4) {
        const char *message   = argv[3];
        const char *client_id = (argc >= 5) ? argv[4] : "mqttTlsPub";

        mqtt_publisher_options_t pub_opts = MQTT_PUBLISHER_OPTIONS_DEFAULT;
        pub_opts.topic       = topic;
        pub_opts.message     = message;
        pub_opts.client_id   = client_id;
        pub_opts.qos         = 0;
        pub_opts.retain      = 1;
        pub_opts.verify_cert = 0;  /* 本番では 1 に変更すること */
        pub_opts.sni         = NULL;

        ret = mqtt_publisher(host, port, debug, &pub_opts);

    } else if (pen_strcmp(mode, "sub") == 0) {
        const char *client_id = (argc >= 4) ? argv[3] : "mqttTlsSub";
        int         loop      = 1;

        mqtt_subscriber_options_t sub_opts = MQTT_SUBSCRIBER_OPTIONS_DEFAULT;
        sub_opts.topic       = topic;
        sub_opts.client_id   = client_id;
        sub_opts.qos         = 0;
        sub_opts.timeout_ms  = 30000;
        sub_opts.verify_cert = 0;  /* 本番では 1 に変更すること */
        sub_opts.sni         = NULL;

        pen_fprintf(stderr,
            "[main] sub server=%s:%d topic=%s loop=%d\n",
            host, port, topic, loop);

        mqtt_message_t *msg = NULL;
        ret = mqtt_subscriber(host, port, debug, loop, &sub_opts, &msg);

        if (msg) {
            pen_printf("%s : %.*s\n",
                       msg->topic,
                       (int)msg->payload_len,
                       msg->payload ? (char *)msg->payload : "");
            mqtt_message_free(msg);
        }

    } else {
        pen_fprintf(stderr,
            "[main] Unknown mode: \"%s\". Use \"pub\" or \"sub\".\n", mode);
        ret = 1;
    }

#ifdef _WINDOWS
    WSACleanup();
#endif
    return ret;
}
