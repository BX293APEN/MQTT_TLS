cmake_minimum_required(VERSION 3.15)
project(mqtt_tls C)

# =====================================================================
# 出力ファイル名 (cmake .. -DAPP_NAME=your_name で変更可能)
#   実行ファイルは常に <APP_NAME>.exe として生成される (全OS共通)
# =====================================================================
set(
    APP_NAME
    "mqtt_tls"
    CACHE STRING "Output executable base name (without extension)"
)

# 全プラットフォームで .exe 拡張子に統一する
set(CMAKE_EXECUTABLE_SUFFIX ".exe")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# =====================================================================
# TLS 証明書検証オプション
#   cmake .. -DMQTT_TLS_VERIFY_CERT=ON  → 証明書検証を有効化
#   cmake .. -DMQTT_TLS_VERIFY_CERT=OFF → 証明書検証スキップ (デフォルト)
# =====================================================================
option(MQTT_TLS_VERIFY_CERT "Enable TLS certificate verification" OFF)

# =====================================================================
# デバッグログオプション
#   cmake .. -DMQTT_DEBUG=ON  → MQTT/TLS レイヤのデバッグ出力を有効化
# =====================================================================
option(MQTT_DEBUG "Enable MQTT and TLS debug logging" OFF)

# =====================================================================
# タイムゾーン設定
#   cmake .. -DMQTT_UTC_OFFSET_SEC=32400   → JST (UTC+9) [デフォルト]
#   cmake .. -DMQTT_UTC_OFFSET_SEC=0       → UTC
# =====================================================================
set(MQTT_UTC_OFFSET_SEC "" CACHE STRING
    "UTC offset in seconds for local time display (default: 32400 = JST)")
option(MQTT_TZ_FROM_OS
    "Use OS timezone setting via localtime() instead of fixed offset" OFF)

# =====================================================================
# アプリケーションソース
# =====================================================================
set(APP_SOURCES
    main.c
)

# =====================================================================
# penlib (標準ライブラリ抽象化)
# =====================================================================
set(PENLIB_SOURCES
    lib/penlib.c
)

set(PENLIB_INCLUDE
    ${CMAKE_SOURCE_DIR}/lib
)

# =====================================================================
# TLS 抽象化レイヤ (tlslib = TCP + picotls minicrypto)
# =====================================================================
set(TLS_SOURCES
    tls/tlslib.c
)

set(TLS_INCLUDE
    ${CMAKE_SOURCE_DIR}/tls
)

# =====================================================================
# MQTT パケット生成・クライアントレイヤ
# =====================================================================
set(MQTT_SOURCES
    mqtt/mqtt_message_create.c
    mqtt/mqtt_client.c
)

set(MQTT_INCLUDE
    ${CMAKE_SOURCE_DIR}/mqtt
)

# =====================================================================
# picotls コアソース (外部依存ゼロの純 C TLS 1.3 実装)
# =====================================================================
set(PICOTLS_INCLUDE
    ${CMAKE_SOURCE_DIR}/picotls/include
    ${CMAKE_SOURCE_DIR}/picotls/lib
)

set(PICOTLS_CORE_SOURCES
    picotls/lib/picotls.c
    picotls/lib/pembase64.c
    picotls/lib/hpke.c
    picotls/lib/ffx.c
    picotls/lib/asn1.c
)

# picotls minicrypto バックエンド (OpenSSL/mbedtls 不要)
set(PICOTLS_MINICRYPTO_SOURCES
    picotls/lib/cifra.c
    picotls/lib/uecc.c
    picotls/lib/minicrypto-pem.c
    picotls/lib/cifra/aes128.c
    picotls/lib/cifra/aes256.c
    picotls/lib/cifra/chacha20.c
    picotls/lib/cifra/random.c
    picotls/lib/cifra/x25519.c
    picotls/deps/cifra/src/aes.c
    picotls/deps/cifra/src/blockwise.c
    picotls/deps/cifra/src/chacha20.c
    picotls/deps/cifra/src/chash.c
    picotls/deps/cifra/src/curve25519.c
    picotls/deps/cifra/src/drbg.c
    picotls/deps/cifra/src/hmac.c
    picotls/deps/cifra/src/gcm.c
    picotls/deps/cifra/src/gf128.c
    picotls/deps/cifra/src/modes.c
    picotls/deps/cifra/src/poly1305.c
    picotls/deps/cifra/src/sha256.c
    picotls/deps/cifra/src/sha512.c
    picotls/deps/micro-ecc/uECC.c
)

# =====================================================================
# 実行ファイルのビルド
# =====================================================================
add_executable(${APP_NAME}
    ${APP_SOURCES}
    ${PENLIB_SOURCES}
    ${TLS_SOURCES}
    ${MQTT_SOURCES}
    ${PICOTLS_CORE_SOURCES}
    ${PICOTLS_MINICRYPTO_SOURCES}
)

# =====================================================================
# インクルードパス
# =====================================================================
target_include_directories(${APP_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PENLIB_INCLUDE}
    ${TLS_INCLUDE}
    ${MQTT_INCLUDE}
    ${PICOTLS_INCLUDE}
    ${CMAKE_SOURCE_DIR}/picotls/deps/cifra/src
    ${CMAKE_SOURCE_DIR}/picotls/deps/cifra/src/ext
    ${CMAKE_SOURCE_DIR}/picotls/deps/micro-ecc
)

# =====================================================================
# コンパイル定義
# =====================================================================
target_compile_definitions(${APP_NAME} PRIVATE
    PTLS_WITHOUT_OPENSSL        # OpenSSL 完全排除
    PTLS_WITHOUT_FUSION         # AVX2/AES-NI 無効
    DISABLE_DEBUG_PRINTF
    $<$<BOOL:${MQTT_TLS_VERIFY_CERT}>:MQTT_TLS_VERIFY_CERT_ENABLE>
    $<$<BOOL:${MQTT_DEBUG}>:MQTT_DEBUG_ENABLE>
    $<$<BOOL:${MQTT_DEBUG}>:TLS_DEBUG_ENABLE>
    $<$<NOT:$<STREQUAL:${MQTT_UTC_OFFSET_SEC},>>:PENQUIC_UTC_OFFSET_SEC=${MQTT_UTC_OFFSET_SEC}>
    $<$<BOOL:${MQTT_TZ_FROM_OS}>:PENQUIC_TZ_FROM_OS>
    # Windows: _WINDOWS を定義
    # Linux/macOS: _GNU_SOURCE で POSIX 拡張 (strnlen 等) を有効化
    $<$<PLATFORM_ID:Windows>:_WINDOWS>
    $<$<NOT:$<PLATFORM_ID:Windows>>:_GNU_SOURCE>
)

# =====================================================================
# コンパイルオプション
# =====================================================================
target_compile_options(${APP_NAME} PRIVATE
    $<$<C_COMPILER_ID:GNU>:    -O2 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations>
    $<$<C_COMPILER_ID:Clang>:  -O2 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations>
    $<$<C_COMPILER_ID:AppleClang>: -O2 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations>
    # MSVC: /utf-8 でソース・実行文字セットを UTF-8 に統一
    #       /wd4996 で strncpy/strdup の POSIX 非推奨警告を抑制
    $<$<C_COMPILER_ID:MSVC>: /W3 /wd4996 /wd4244 /wd4267 /wd4018 /utf-8>
    $<$<AND:$<C_COMPILER_ID:MSVC>,$<CONFIG:Release>>: /O2>
)

# =====================================================================
# リンクライブラリ
# =====================================================================
target_link_libraries(${APP_NAME} PRIVATE
    $<$<PLATFORM_ID:Linux>:dl>
    # Windows: WinSock2 と BCrypt (乱数生成用)
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:bcrypt>
    $<$<PLATFORM_ID:Windows>:crypt32>
    $<$<PLATFORM_ID:Windows>:iphlpapi>
)

# MinGW: inline 関数の多重定義を許可
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_link_options(${APP_NAME} PRIVATE -Wl,--allow-multiple-definition)
endif()

# =====================================================================
# ビルド設定サマリ
# =====================================================================
message(STATUS "")
message(STATUS "=== mqtt_tls build configuration ===")
message(STATUS "  Output name   : ${APP_NAME}.exe")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Crypto        : picotls minicrypto (no OpenSSL / no mbedtls)")
message(STATUS "  TLS version   : TLS 1.3 (QUIC-grade cipher suites)")
message(STATUS "  Cert verify   : ${MQTT_TLS_VERIFY_CERT}")
message(STATUS "  Debug log     : ${MQTT_DEBUG}")
message(STATUS "  UTC offset    : ${MQTT_UTC_OFFSET_SEC} (empty=JST +32400)")
message(STATUS "  TZ from OS    : ${MQTT_TZ_FROM_OS}")
message(STATUS "=====================================")
message(STATUS "")
message(STATUS "Build:              mkdir build && cd build && cmake .. && cmake --build .")
message(STATUS "Debug build:        cmake .. -DCMAKE_BUILD_TYPE=Debug -DMQTT_DEBUG=ON")
message(STATUS "With cert verify:   cmake .. -DMQTT_TLS_VERIFY_CERT=ON")
message(STATUS "Timezone UTC:       cmake .. -DMQTT_UTC_OFFSET_SEC=0")
message(STATUS "Timezone JST:       cmake .. -DMQTT_UTC_OFFSET_SEC=32400 (default)")
message(STATUS "")
