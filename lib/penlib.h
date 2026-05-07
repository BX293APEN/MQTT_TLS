/*
 * penlib.h  —  MQTT-TLS ポータブル標準ライブラリ抽象化レイヤ
 *
 * 目的:
 *   Windows / Linux / macOS / IoT(組み込み) など様々な環境で動作する
 *   MQTT-TLS プロジェクト全体の標準ライブラリ・OS API 呼び出しを
 *   このヘッダ一本に集約し、移植時の差し替えコストを最小化する。
 *
 *   各ソースファイルは <string.h> / <stdlib.h> / <stdio.h> 等を直接
 *   include せず、このヘッダのみを include する。
 *   実装の差し替えは lib/penlib.c だけ行えばよい。
 *
 * 対象:
 *   メモリ操作  : pen_memcpy / pen_memmove / pen_memset /
 *                 pen_memcmp / pen_memchr
 *   文字列操作  : pen_strlen / pen_strnlen / pen_strncpy /
 *                 pen_strncmp / pen_strcmp / pen_strchr / pen_strdup
 *   動的メモリ  : pen_malloc / pen_calloc / pen_realloc / pen_free
 *   フォーマット: pen_snprintf / pen_vsnprintf / pen_sprintf
 *   ログ出力    : pen_fprintf / pen_printf
 *                 (組み込みでは no-op または独自バックエンドに差し替え)
 *   時刻        : pen_gettimeofday / pen_clock_gettime
 *                 (組み込みでは RTOS タイマ等に差し替え)
 *   プロセス制御: pen_abort / pen_exit
 *                 (組み込みでは watchdog リセット等に差し替え)
 *   UTF-8 デコード: decode_utf8
 */

#ifndef PENLIB_H
#define PENLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>   /* va_list — コンパイラ組み込みのため除外不可 */

/* -----------------------------------------------------------------------
 * 文字列・メモリ・動的確保・数値変換 に必要な型・関数宣言の提供
 *
 * string.h : memcpy / memmove / memset / memcmp / memchr /
 *            strlen / strncpy / strcmp / strchr / strdup 等
 * stdlib.h : malloc / calloc / realloc / free / abort / exit /
 *            atoi / getenv / aligned_alloc / posix_memalign 等
 *
 * IoT / 組み込み移植時:
 *   これらのヘッダが存在しない RTOS 環境では、このブロックを
 *   RTOS 対応の代替ヘッダに差し替えること。
 *   例: FreeRTOS + pvPortMalloc/vPortFree を使う場合は
 *       stdlib.h を除外し FreeRTOS.h + portable.h に差し替える。
 * ----------------------------------------------------------------------- */
#include <string.h>   /* memcpy / memmove / memset / memcmp / strlen 等 */
#include <stdlib.h>   /* malloc / calloc / realloc / free / abort 等 */

/* -----------------------------------------------------------------------
 * 時刻・出力 API に必要な型の提供
 *
 * Windows:
 *   winsock2.h  → struct timeval
 *   time.h      → struct timespec (VS2015+), CLOCK_* 相当は penlib.c で定義
 *   stdio.h     → FILE*
 *
 * POSIX (Linux / macOS / 組み込み POSIX):
 *   sys/time.h  → struct timeval, gettimeofday
 *   time.h      → struct timespec, clock_gettime, CLOCK_REALTIME, CLOCK_MONOTONIC
 *   stdio.h     → FILE*
 *
 * 組み込み (POSIX 非対応):
 *   これらのヘッダが存在しない場合は penlib.c の実装ごと差し替えること。
 * ----------------------------------------------------------------------- */
#ifdef _WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>   /* struct timeval */
#  include <time.h>       /* struct timespec (VS2015+) */
   /* CLOCK_* が未定義の場合のフォールバック (MinGW 旧版等) */
#  ifndef CLOCK_REALTIME
#    define CLOCK_REALTIME  0
#  endif
#  ifndef CLOCK_MONOTONIC
#    define CLOCK_MONOTONIC 1
#  endif
   /* SetThreadDescription は Windows 10 / Server 2016 以降で利用可能。
    * MinGW の processthreadsapi.h は _WIN32_WINNT が古いと宣言を露出しない。
    * pen_thread_setname 内で使用するため、未定義時は独自宣言でフォールバックする。
    * HRESULT 型は winsock2.h → windows.h 経由で定義済み。               */
#  include <processthreadsapi.h>
#  ifndef HAVE_SET_THREAD_DESCRIPTION
#    if defined(__MINGW32__) || defined(__MINGW64__)
       /* MinGW: _WIN32_WINNT が 0x0A00 未満では宣言が露出しないため補完する */
       WINBASEAPI HRESULT WINAPI SetThreadDescription(HANDLE hThread, PCWSTR lpThreadDescription);
#    endif
#    define HAVE_SET_THREAD_DESCRIPTION 1
#  endif
#else
#  include <sys/time.h>   /* struct timeval, gettimeofday */
#  include <time.h>       /* struct timespec, clock_gettime, CLOCK_* */
#endif

/* inttypes.h: PRIu64 等のフォーマットマクロと intmax_t 型を提供。
 * MinGW では __CRT_INLINE な imaxabs が各 TU に展開されリンク時に多重定義が起きる。
 * __CRT__NO_INLINE を事前定義して inline 展開を抑制し、
 * imaxabs の実体は penlib.c に一本化する。                                  */
#if defined(__MINGW32__) || defined(__MINGW64__)
#  ifndef __CRT__NO_INLINE
#    define __CRT__NO_INLINE
#  endif
#endif
#include <inttypes.h>

/* pen_imaxabs: MinGW の imaxabs 多重定義問題を回避するラッパー。
 * 他のプラットフォームでも同一 API で使用可能。                             */
intmax_t pen_imaxabs(intmax_t j);

/* -----------------------------------------------------------------------
 * 【移植方針まとめ】
 *
 * 標準ライブラリ・OS API の完全分離について:
 *
 *  ■ penlib.h が集約するヘッダ (全ソースはこのファイルのみ include する)
 *      stdint.h / stddef.h / stdarg.h / inttypes.h / stdio.h / time.h
 *      string.h / stdlib.h
 *      sys/time.h / errno.h
 *      _WINDOWS: winsock2.h / ws2def.h / ws2tcpip.h / ws2ipdef.h /
 *                mswsock.h / iphlpapi.h / windows.h /
 *                bcrypt.h (通常) / wincrypt.h (_WINDOWS_XP 時)
 *      POSIX   : sys/socket.h / sys/types.h / arpa/inet.h / netinet/in.h /
 *                netinet/udp.h / netdb.h / unistd.h / fcntl.h /
 *                sys/select.h / poll.h / pthread.h / signal.h
 *      Linux   : sys/prctl.h / sys/syscall.h
 *
 *  ■ 例外 (意図的に直接 include を残す箇所)
 *      picotls/deps/cifra/src/ (各ヘッダ)  独立サードパーティ暗号ライブラリ。
 *      picotls/deps/micro-ecc/uECC.h   penlib.h を include しない独立 API ヘッダ
 *                                      であり、stdint.h/stddef.h は型定義に必須。
 *                                      これらは必ず penlib.h を include した .c
 *                                      からのみ include されるため二重定義は生じない。
 *
 *      picotls/lib/picotls.c           __APPLE__ の AvailabilityMacros.h は
 *      (AvailabilityMacros.h)          macOS SDK 固有であり penlib.h からは
 *                                      include 不可。そのまま残す。
 * ----------------------------------------------------------------------- */

/* FILE* — pen_fprintf / pen_printf のシグネチャに必要 */
#include <stdio.h>

/* -----------------------------------------------------------------------
 * ネットワーク / ソケット API に必要な型・定数の提供
 *
 * Windows:
 *   winsock2.h / ws2tcpip.h / ws2def.h / mswsock.h / iphlpapi.h
 *   → SOCKET, struct sockaddr_in6, socklen_t, inet_ntop, getaddrinfo 等
 *
 * POSIX (Linux / macOS):
 *   sys/socket.h  → socket / bind / connect / send / recv 等
 *   sys/types.h   → ssize_t / pid_t 等
 *   arpa/inet.h   → inet_ntop / inet_pton / htons 等
 *   netinet/in.h  → struct sockaddr_in / sockaddr_in6 / IPPROTO_* 等
 *   netdb.h       → getaddrinfo / freeaddrinfo / struct addrinfo 等
 *   unistd.h      → close / read / write / getpid 等
 *   errno.h       → errno / EAGAIN / EWOULDBLOCK 等
 *   sys/select.h  → select / fd_set / FD_SET 等
 *   poll.h        → poll / struct pollfd / POLLIN 等 (Linux/macOS)
 *   fcntl.h       → fcntl / O_NONBLOCK 等
 *   netinet/udp.h → struct udphdr / UDP_GRO 等
 *   sys/prctl.h   → prctl / PR_SET_NAME 等 (Linux のみ)
 *
 * 組み込み (POSIX 非対応):
 *   これらのヘッダが存在しない場合は LWIP / FreeRTOS+TCP 等の
 *   ソケット互換 API ヘッダに差し替えること。
 * ----------------------------------------------------------------------- */
#ifdef _WINDOWS
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>    /* SOCKET, WSAStartup, WSAGetLastError 等 */
#  include <ws2def.h>      /* AF_*, SOCK_*, IPPROTO_* 等 */
#  include <ws2tcpip.h>    /* getaddrinfo, inet_ntop, struct sockaddr_in6 等 */
#  include <mswsock.h>     /* WSARecvMsg, GUID, WSAID_WSARECVMSG 等 */
#  include <iphlpapi.h>    /* GetAdaptersAddresses 等 */
   /* Windows の errno 相当は WSAGetLastError() を使う。
    * errno.h 自体は MSVC でも存在するが POSIX エラーコードとは異なる。 */
#  include <errno.h>
   /* Windows 暗号乱数 API
    *   _WINDOWS_XP : 旧 CryptAPI (Vista 未満の互換向け)
    *   通常        : BCrypt API (Vista 以降、推奨)
    * picotls/lib/cifra/random.c はここで集約済みのため
    * 各ソースで直接 include しないこと。                              */
#  ifdef _WINDOWS_XP
#    include <wincrypt.h>  /* CryptAcquireContext / CryptGenRandom 等 */
#  else
#    include <bcrypt.h>    /* BCryptOpenAlgorithmProvider / BCryptGenRandom 等 */
#  endif
#  ifndef EWOULDBLOCK
#    define EWOULDBLOCK WSAEWOULDBLOCK
#  endif
#  ifndef EAGAIN
#    define EAGAIN       WSAEWOULDBLOCK
#  endif
   /* -----------------------------------------------------------------------
    * POSIX 互換型・定数 (MSVC / Windows SDK では未定義のもの)
    *
    * ssize_t    : 符号付きサイズ型。picotls.c / tlslib.c 等で使用。
    *              Windows SDK 提供の SSIZE_T (BaseTsd.h) を利用する。
    *              BaseTsd.h は winsock2.h → windows.h 経由で展開済み。
    * socklen_t  : winsock2.h / ws2tcpip.h で定義されているが、
    *              MinGW 旧版で露出しない場合があるため念のため補完。
    * MSG_NOSIGNAL: POSIX の send() フラグ。Windows の send() は
    *              SIGPIPE を送出しないため 0 で代替する。
    * ----------------------------------------------------------------------- */
#  ifndef ssize_t
     typedef SSIZE_T ssize_t;
#  endif
#  ifndef socklen_t
     typedef int socklen_t;
#  endif
#  ifndef MSG_NOSIGNAL
#    define MSG_NOSIGNAL 0
#  endif
   /* __attribute__((format(...))) 等の GCC/Clang 拡張は MSVC が解釈できない。
    * 空マクロで無効化することで picotls.h / picotls.c のコンパイルエラーを回避する。
    * MSVC は __declspec(...) で同等機能を提供するが、printf 書式チェックは
    * SAL アノテーション (_Printf_format_string_) で行う。本プロジェクトでは
    * 書式チェックが必須ではないため、単純に無効化する。 */
#  ifndef __attribute__
#    define __attribute__(x)
#  endif
#else  /* POSIX */
#  include <sys/socket.h>  /* socket / bind / connect / send / recv 等 */
#  include <sys/types.h>   /* ssize_t / pid_t 等 */
#  include <arpa/inet.h>   /* inet_ntop / inet_pton / htons / htonl 等 */
#  include <netinet/in.h>  /* struct sockaddr_in / sockaddr_in6 / IPPROTO_* 等 */
#  include <netinet/udp.h> /* struct udphdr / UDP_GRO 等 */
#  include <netdb.h>       /* getaddrinfo / freeaddrinfo / struct addrinfo 等 */
#  include <unistd.h>      /* close / read / write / getpid 等 */
#  include <errno.h>       /* errno / EAGAIN / EWOULDBLOCK / EINTR 等 */
#  include <fcntl.h>       /* fcntl / O_NONBLOCK 等 */
#  ifdef __linux__
#    include <sys/select.h>  /* select / fd_set / FD_SET 等 */
#    include <poll.h>        /* poll / struct pollfd / POLLIN 等 */
#    ifdef PR_SET_NAME
       /* 既に linux/prctl.h 経由で定義済みの場合は重複 include を防ぐ */
#    else
#      include <sys/prctl.h> /* prctl / PR_SET_NAME 等 */
#    endif
#    include <sys/syscall.h> /* syscall(SYS_gettid) 等 Linux 固有システムコール */
#  else  /* macOS / BSD */
#    include <sys/select.h>
#    include <poll.h>
#  endif
#  include <ifaddrs.h>  /* getifaddrs / freeifaddrs — NIC アドレス列挙 (Connection Migration 用) */
#  include <net/if.h>   /* IFF_UP / IFF_RUNNING / IFF_LOOPBACK 等のインターフェースフラグ */
#endif  /* _WINDOWS / POSIX */

/* -----------------------------------------------------------------------
 * スレッド・同期プリミティブ に必要な型・定数の提供
 *
 * Windows:
 *   windows.h に含まれる CRITICAL_SECTION / CONDITION_VARIABLE /
 *   CreateThread / WaitForSingleObject / HANDLE 等を使用。
 *   penlib.h の上部で既に winsock2.h → windows.h がインクルード済み
 *   (WIN32_LEAN_AND_MEAN 経由) なので追加 include は不要。
 *
 * POSIX:
 *   pthread.h → pthread_t / pthread_mutex_t / pthread_cond_t /
 *               pthread_create / pthread_join / pthread_cancel 等
 *   signal.h  → SIGTERM / pthread_kill 等 (Android 向け pthread_kill 使用箇所)
 *
 * 組み込み (POSIX 非対応):
 *   FreeRTOS の xSemaphoreCreateMutex 等に差し替えること。
 * ----------------------------------------------------------------------- */
#ifndef _WINDOWS
#  include <pthread.h>   /* pthread_t / pthread_mutex_t / pthread_cond_t 等 */
#  include <signal.h>    /* SIGTERM / pthread_kill (Android 向け) 等 */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 * メモリ操作
 * ===================================================================== */

void*  pen_memcpy (void* dst, const void* src, size_t n);
void*  pen_memmove(void* dst, const void* src, size_t n);
void*  pen_memset (void* dst, int c, size_t n);
int    pen_memcmp (const void* a, const void* b, size_t n);
void*  pen_memchr (const void* s, int c, size_t n);

/* =====================================================================
 * 文字列操作
 * ===================================================================== */

size_t pen_strlen (const char* s);
size_t pen_strnlen(const char* s, size_t maxlen);
char*  pen_strncpy(char* dst, const char* src, size_t n);
int    pen_strncmp(const char* a, const char* b, size_t n);
int    pen_strcmp (const char* a, const char* b);
char*  pen_strchr (const char* s, int c);

/*
 * pen_strdup — malloc で確保した複製を返す。不要になったら pen_free すること。
 * Windows (_MSC_VER) では _strdup、POSIX では strdup を使用。
 */
char*  pen_strdup (const char* s);

/*
 * pen_getenv — 環境変数の値を返す。
 * スレッドセーフではない点は標準の getenv と同様。
 * 組み込み環境では NULL を返す実装に差し替えること。
 */
char*  pen_getenv (const char* name);

/*
 * pen_atoi — 文字列を int に変換する。
 */
int    pen_atoi   (const char* s);

/*
 * pen_inet_ntop — inet_ntop のポータブルラッパー
 *
 *   ネットワークアドレス (struct in_addr / struct in6_addr) を
 *   文字列表現に変換する。inet_ntop(3) と同じシグネチャ・戻り値。
 *
 *   戻り値: 成功時は dst、失敗時は NULL
 *
 *   Windows: ws2tcpip.h の InetNtopA (Vista 以降) に委譲。
 *   POSIX  : arpa/inet.h の inet_ntop に委譲。
 *
 *   組み込み環境では lwIP の ipaddr_ntoa 等に差し替えること。
 */
const char* pen_inet_ntop(int af, const void* src, char* dst, size_t size);

/* =====================================================================
 * アラインメント付きメモリ確保
 *
 * pen_aligned_alloc(align, size):
 *   POSIX : posix_memalign
 *   Windows (MSVC/MinGW) : _aligned_malloc
 *   解放は必ず pen_aligned_free を使うこと (_aligned_free と free は互換なし)
 * ===================================================================== */
void*  pen_aligned_alloc(size_t align, size_t size);
void   pen_aligned_free (void* ptr);

/* =====================================================================
 * ファイル I/O ラッパー
 *
 * ログ・設定ファイル読み書きに使用。
 * 組み込み環境でファイルシステムが存在しない場合は
 * NULL を返す / 何もしない実装に差し替えること。
 * FILE* 型は penlib.h 上部の <stdio.h> include で提供済み。
 * ===================================================================== */
FILE*  pen_fopen  (const char* path, const char* mode);
int    pen_fclose (FILE* fp);
size_t pen_fread  (void* buf, size_t size, size_t nmemb, FILE* fp);
size_t pen_fwrite (const void* buf, size_t size, size_t nmemb, FILE* fp);
char*  pen_fgets  (char* buf, int size, FILE* fp);
int    pen_fseek  (FILE* fp, long offset, int whence);
long   pen_ftell  (FILE* fp);
int    pen_fflush (FILE* fp);
int    pen_fputc  (int c, FILE* fp);
int    pen_putc   (int c, FILE* fp);

/* =====================================================================
 * スレッド補助 API
 *
 * pen_thread_exit(retval):
 *   POSIX: pthread_exit(retval) に委譲。
 *   Windows: ExitThread(0) に委譲（スレッド関数の return で十分なため通常不要）。
 *   組み込み: FreeRTOS なら vTaskDelete(NULL) 等に差し替えること。
 *
 * pen_thread_setname(name):
 *   実行中スレッドに名前を設定するデバッグ用 API。
 *   Windows: SetThreadDescription (Vista 以降)
 *   macOS  : pthread_setname_np(name)         (引数1個版)
 *   Linux  : prctl(PR_SET_NAME, name, ...)
 *   組み込み: RTOS の vTaskSetApplicationTaskTag 等に差し替えるか、
 *             デバッグ不要なら空実装のままでよい。
 * ===================================================================== */
void pen_thread_exit  (void* retval);
void pen_thread_setname(const char* name);

/* =====================================================================
 * assert ラッパー
 *
 * pen_assert(expr):
 *   assert.h を経由せず pen_abort() を直接呼ぶ実装。
 *
 *   理由:
 *     penlib.h が <assert.h> を include して assert マクロを定義した後に
 *     cifra/tassert.h が assert を再定義するため「assert redefined」警告が出る。
 *     penlib.h 側で assert.h を引かず pen_abort() ベースで統一することで
 *     tassert.h の定義と一致させ警告を解消する。
 *
 *   NDEBUG 対応:
 *     NDEBUG が定義されている場合は標準 assert と同様に無効化する。
 *
 *   組み込み向け差し替え:
 *     pen_abort() を差し替えることで assert 失敗時の動作を変更できる。
 * ===================================================================== */
#ifdef NDEBUG
#  define pen_assert(expr) ((void)(expr))
#else
#  define pen_assert(expr)      do { if (!(expr)) pen_abort(); } while (0)
#endif

/* =====================================================================
 * 時刻 (追加分)
 *
 * pen_time: time(NULL) 相当。UTC エポック秒を返す。
 *
 * pen_get_timestamp:
 *   現在のローカル時刻を "yyyy/mm/dd hh:mm:ss" 形式で buf に書き込む。
 *   タイムゾーンは pen_get_utc_offset_sec() の値を使用する。
 *   buf_size は PEN_TIMESTAMP_LEN 以上を推奨。
 *   戻り値: buf へのポインタ (エラー時は "0000/00/00 00:00:00")。
 *
 *   組み込み移植時:
 *     pen_gettimeofday() を RTOS タイマに差し替えることで対応可能。
 * ===================================================================== */
#include <time.h>   /* time_t, time() の定義 — penlib.h 内で既に include 済み */
time_t pen_time(time_t* t);

/* pen_get_timestamp が書き込む文字列の長さ ("yyyy/mm/dd hh:mm:ss" + NUL) */
#define PEN_TIMESTAMP_LEN  20

char* pen_get_timestamp(char* buf, size_t buf_size);

/* =====================================================================
 * 統合ログ出力
 *
 * pen_log(stream, protocol, state, action, fmt, ...):
 *   プロジェクト全体で統一されたフォーマットでログを1行出力する。
 *
 *   出力形式:
 *     [protocol] [state] [action] yyyy/mm/dd hh:mm:ss 詳細情報
 *
 *   引数:
 *     stream   : 出力先 FILE* (stdout / stderr)
 *                カスタムバックエンド時は無視される。
 *     protocol : プロトコル名文字列 ("MQTT" 等 等)
 *     state    : 状態文字列        ("OK" / "WARN" / "ERROR" / "DEBUG")
 *     action   : 動作文字列        ("connect" / "disconnect" /
 *                                   "send" / "recv" / "change" 等)
 *     fmt, ... : printf 互換の詳細情報フォーマット
 *
 * カスタムバックエンド (組み込み向け):
 *   PENLIB_LOG_CUSTOM_BACKEND を定義し、
 *   pen_log_write(const char *line) をユーザー側で実装すると
 *   stream 引数を無視して pen_log_write に委譲する。
 *
 *   例 (UART出力):
 *     #define PENLIB_LOG_CUSTOM_BACKEND
 *     void pen_log_write(const char *line) { uart_puts(line); }
 * ===================================================================== */

#ifdef PENLIB_LOG_CUSTOM_BACKEND
/* ユーザー側で実装する1行出力関数 */
extern void pen_log_write(const char *line);
#endif

void pen_log(FILE* stream,
             const char* protocol,
             const char* state,
             const char* action,
             const char* fmt, ...);

/* =====================================================================
 * 動的メモリ
 * ===================================================================== */

void*  pen_malloc (size_t size);
void*  pen_calloc (size_t nmemb, size_t size);
void*  pen_realloc(void* ptr, size_t size);
void   pen_free   (void* ptr);

/* =====================================================================
 * フォーマット出力
 *
 * pen_fprintf / pen_printf:
 *   デスクトップ環境では stdio に委譲。
 *   組み込み環境で stdio が使えない場合は penlib.c 内を差し替えるか、
 *   コンパイル時に PENLIB_NO_STDIO を定義して no-op にする。
 *
 * pen_vfprintf:
 *   va_list 版 fprintf。内部ログ関数が使用。
 *   MSVC では vfprintf_s に委譲し、POSIX では vfprintf に委譲する。
 *   組み込み環境では pen_fprintf と同様に差し替えること。
 *
 * pen_snprintf / pen_vsnprintf / pen_sprintf:
 *   バッファへの書き込み。MSVC の非互換差異を吸収する。
 *
 * pen_perror:
 *   errno に対応するエラーメッセージを stderr に出力する。
 *   POSIX の perror(3) に相当。
 *   組み込み環境では PENLIB_NO_STDIO 定義で no-op にするか、
 *   独自のエラーログ出力に差し替えること。
 * ===================================================================== */

int pen_fprintf (FILE* stream, const char* fmt, ...);
int pen_vfprintf(FILE* stream, const char* fmt, va_list ap);
int pen_printf  (const char* fmt, ...);
int pen_snprintf (char* buf, size_t size, const char* fmt, ...);
int pen_vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
void pen_perror (const char* msg);

/*
 * pen_sprintf — 固定バッファへの無制限書き込み (安全でないため用途を限定)。
 * picotls の tid 文字列化など、バッファサイズが静的に保証される箇所専用。
 */
int pen_sprintf(char* buf, const char* fmt, ...);

/* =====================================================================
 * 時刻 API
 *
 * pen_gettimeofday:
 *   POSIX: gettimeofday(3) に委譲。
 *   Windows: wincompat.h の wintimeofday に委譲。
 *   組み込み: RTOS タイマや RTC に差し替える。
 *
 * pen_clock_gettime:
 *   POSIX: clock_gettime(3) に委譲。
 *   Windows: timespec_get / QueryPerformanceCounter ベースの実装を使用。
 *   組み込み: RTOS タイマに差し替える。
 *
 * CLOCK_MONOTONIC / CLOCK_REALTIME は <time.h> で定義される。
 * Windows は wincompat.h 経由で提供される。
 * ===================================================================== */

int pen_gettimeofday (struct timeval* tv, void* tz);
int pen_clock_gettime(int clk_id, struct timespec* ts);

/* =====================================================================
 * プロセス制御
 *
 * pen_abort / pen_exit:
 *   デスクトップ環境では abort()/exit() に委譲。
 *   組み込み環境では watchdog リセットや無限ループに差し替える。
 * ===================================================================== */

void pen_abort(void);
void pen_exit (int status);

/* =====================================================================
 * タイムゾーン
 *
 * ログ・チケット時刻は内部的にすべて UTC で扱う。
 * pen_get_utc_offset_sec() はローカル時刻への表示変換にのみ使用する。
 *
 * デフォルト: 日本標準時 JST (UTC+9 = +32400 秒)
 *
 * ビルド時オーバーライド:
 *   cmake .. -DMQTT_UTC_OFFSET_SEC=0       # UTC
 *   cmake .. -DMQTT_UTC_OFFSET_SEC=3600    # CET  (UTC+1)
 *   cmake .. -DMQTT_UTC_OFFSET_SEC=-18000  # EST  (UTC-5)
 *   cmake .. -DMQTT_UTC_OFFSET_SEC=28800   # CST  (UTC+8)
 *   cmake .. -DMQTT_UTC_OFFSET_SEC=32400   # JST  (UTC+9, デフォルト)
 *   cmake .. -DMQTT_UTC_OFFSET_SEC=36000   # AEST (UTC+10)
 *
 * 実行時オーバーライド (OS のタイムゾーン設定を使用する場合):
 *   cmake .. -DMQTT_TZ_FROM_OS=ON
 *   → pen_get_utc_offset_sec() が localtime() で OS 設定を読む。
 *     組み込み環境など OS TZ が使えない場合は OFF のままにすること。
 *
 * 戻り値: UTC からのオフセット秒数 (東 = 正、西 = 負)
 * ===================================================================== */
int32_t pen_get_utc_offset_sec(void);

/* =====================================================================
 * UTF-8 デコード
 * ===================================================================== */

size_t decode_utf8(
    const uint8_t* data,
    size_t         len,
    size_t         max_len,
    char*          out,
    size_t         out_size
);

#ifdef __cplusplus
}
#endif

#endif /* PENLIB_H */
