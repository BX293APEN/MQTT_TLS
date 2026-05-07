/*
 * penlib.c  —  PENQUIC ポータブル標準ライブラリ抽象化レイヤ 実装
 *
 * 現在はすべて libc / OS API に委譲する実装 (デスクトップ向けデフォルト)。
 * IoT / 組み込み環境への移植時はこのファイルの実装ブロックのみ差し替える。
 *
 * コンパイル時スイッチ:
 *   PENLIB_NO_STDIO   : pen_fprintf / pen_printf を no-op にする
 *                       (stdio が存在しない組み込み環境向け)
 *
 * 対応プラットフォーム:
 *   Windows (MSVC / MinGW)  — #ifdef _WINDOWS / #ifdef _MSC_VER で分岐
 *   Linux / macOS (GCC / Clang) — POSIX パス
 *   その他 POSIX 準拠 OS   — 同上
 */

#include <penlib.h>

/* -----------------------------------------------------------------------
 * 標準ライブラリ・OS API include をこのファイルに集約する
 *
 * 【集約方針】
 *   penlib.h が全ソースから include される唯一の標準ライブラリ窓口となる。
 *   penlib.h では型・定数の提供に必要なヘッダのみを include し、
 *   実装に必要な追加ヘッダはここ (penlib.c) にまとめる。
 *   他のソースファイルは以下のヘッダを直接 include しない:
 *     string.h / stdlib.h / stdio.h / time.h / sys/time.h
 *     sys/socket.h / arpa/inet.h / netdb.h / unistd.h
 *     pthread.h / signal.h / errno.h / poll.h / fcntl.h 等
 *
 * 【移植時の差し替え手順】
 *   このブロックのヘッダを RTOS / LWIP 等の対応 API ヘッダに差し替え、
 *   各関数の実装ブロックを対応する RTOS/BSP API に書き換える。
 * ----------------------------------------------------------------------- */

/* --- 文字列・メモリ・数値変換 ---
 * string.h / stdlib.h / stdio.h は penlib.h が集約済み。
 * このファイルでは直接 include しない。
 *
 *   string.h : memcpy / memmove / memset / memcmp / memchr
 *              strlen / strncpy / strcmp / strchr / strdup 等
 *   stdlib.h : malloc / calloc / realloc / free / abort / exit
 *              atoi / getenv / aligned_alloc / posix_memalign 等
 *   stdio.h  : fopen / fclose / fread / fwrite / fgets /
 *              fprintf / printf / perror 等
 *
 * 移植時はこれらの代替ヘッダを penlib.h 側で差し替えること。
 * ----------------------------------------------------------------- */

/* -----------------------------------------------------------------------
 * 時刻・スレッド・ソケット・ネットワーク API
 *
 * すべて penlib.h が集約済みのため、penlib.c では直接 include しない。
 *
 *   _WINDOWS:
 *     windows.h    — QueryPerformanceCounter / GetSystemTimeAsFileTime /
 *                    CRITICAL_SECTION / CONDITION_VARIABLE 等
 *     time.h       — struct timespec (VS2015+) / time() / localtime_s 等
 *     winsock2.h   — SOCKET / WSAStartup 等 (penlib.h の上部で集約済み)
 *   POSIX:
 *     sys/time.h   — gettimeofday / struct timeval
 *     time.h       — clock_gettime / struct timespec / gmtime_r 等
 *     pthread.h    — pthread_t / pthread_mutex_t / pthread_create 等
 *     signal.h     — SIGTERM / pthread_kill 等
 *     sys/socket.h — socket / bind / connect / send / recv 等
 *     arpa/inet.h  — inet_ntop / inet_pton / htons 等
 *     netinet/in.h — struct sockaddr_in6 / IPPROTO_* 等
 *     netdb.h      — getaddrinfo / freeaddrinfo 等
 *     unistd.h     — close / read / write / getpid 等
 *     errno.h      — errno / EAGAIN / EWOULDBLOCK 等
 *     fcntl.h      — fcntl / O_NONBLOCK 等
 *   Linux 追加:
 *     poll.h / sys/select.h / sys/prctl.h / sys/syscall.h
 *
 * CLOCK_REALTIME / CLOCK_MONOTONIC の Windows フォールバック定義も
 * penlib.h で行っている。
 *
 * 移植時は penlib.h の該当ブロックを RTOS / LWIP 等のヘッダに差し替えること。
 * ----------------------------------------------------------------------- */

/* =====================================================================
 * メモリ操作
 * ===================================================================== */

void* pen_memcpy(void* dst, const void* src, size_t n)
{
    return memcpy(dst, src, n);
}

void* pen_memmove(void* dst, const void* src, size_t n)
{
    return memmove(dst, src, n);
}

void* pen_memset(void* dst, int c, size_t n)
{
    return memset(dst, c, n);
}

int pen_memcmp(const void* a, const void* b, size_t n)
{
    return memcmp(a, b, n);
}

void* pen_memchr(const void* s, int c, size_t n)
{
    return memchr(s, c, n);
}

/* =====================================================================
 * 文字列操作
 * ===================================================================== */

size_t pen_strlen(const char* s)
{
    return strlen(s);
}

size_t pen_strnlen(const char* s, size_t maxlen)
{
    /*
     * strnlen の可用性:
     *   MSVC (VS2012+)      : <string.h> で提供
     *   GCC/Clang + glibc   : _GNU_SOURCE で提供
     *   その他 POSIX        : POSIX.1-2008 で提供
     *   古い環境 / 組み込み  : memchr フォールバックを使用
     */
#if defined(_MSC_VER) || defined(__GLIBC__) || \
    (defined(_POSIX_C_SOURCE) && _POSIX_C_SOURCE >= 200809L)
    return strnlen(s, maxlen);
#else
    const char* end = (const char*)memchr(s, '\0', maxlen);
    return (end != NULL) ? (size_t)(end - s) : maxlen;
#endif
}

char* pen_strncpy(char* dst, const char* src, size_t n)
{
    return strncpy(dst, src, n);
}

int pen_strncmp(const char* a, const char* b, size_t n)
{
    return strncmp(a, b, n);
}

int pen_strcmp(const char* a, const char* b)
{
    return strcmp(a, b);
}

char* pen_strchr(const char* s, int c)
{
    return strchr(s, c);
}

char* pen_strdup(const char* s)
{
    /*
     * strdup の可用性:
     *   MSVC          : _strdup (POSIX 版 strdup は非推奨)
     *   POSIX / GCC   : strdup
     *   C23           : 標準化済み
     */
#if defined(_MSC_VER)
    return _strdup(s);
#else
    return strdup(s);
#endif
}

char* pen_getenv(const char* name)
{
    /*
     * 環境変数を取得する。
     * MSVC では getenv は非推奨 (_dupenv_s 推奨) だが、
     * ここでは単純な読み取り専用用途のため getenv で十分。
     * 組み込み環境では常に NULL を返す実装に差し替えること。
     */
#if defined(_MSC_VER)
    /* MSVC でも getenv は利用可能 (非推奨警告を抑制) */
#  pragma warning(suppress: 4996)
    return getenv(name);
#else
    return getenv(name);
#endif
}

int pen_atoi(const char* s)
{
    if (s == NULL) return 0;
    return atoi(s);
}

/* =====================================================================
 * ネットワークアドレス変換
 * ===================================================================== */

const char* pen_inet_ntop(int af, const void* src, char* dst, size_t size)
{
    if (src == NULL || dst == NULL || size == 0) return NULL;
#if defined(_WINDOWS)
    /*
     * Windows: InetNtopA (Vista 以降, ws2tcpip.h 経由) を使用。
     * MSVC では inet_ntop も同じ関数に解決されるが、
     * MinGW 旧版では宣言がない場合があるため InetNtopA を明示する。
     */
    return InetNtopA(af, src, dst, (DWORD)size);
#else
    return inet_ntop(af, src, dst, (socklen_t)size);
#endif
}

/* =====================================================================
 * アラインメント付きメモリ確保
 * ===================================================================== */
void* pen_aligned_alloc(size_t align, size_t size)
{
#if defined(_WINDOWS)
    /* MSVC / MinGW: _aligned_malloc(size, align) — 引数順が逆なので注意 */
    return _aligned_malloc(size, align);
#elif defined(__APPLE__) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
    /* C11 aligned_alloc: align は size の約数でなければならない */
    return aligned_alloc(align, size);
#else
    /* POSIX */
    void* ptr = NULL;
    if (posix_memalign(&ptr, align, size) != 0)
        return NULL;
    return ptr;
#endif
}

void pen_aligned_free(void* ptr)
{
#if defined(_WINDOWS)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/* =====================================================================
 * ファイル I/O ラッパー
 * ===================================================================== */
FILE* pen_fopen(const char* path, const char* mode)
{
#if defined(_MSC_VER)
    FILE* fp = NULL;
    fopen_s(&fp, path, mode);
    return fp;
#else
    return fopen(path, mode);
#endif
}

int pen_fclose(FILE* fp)
{
    if (fp == NULL) return 0;
    return fclose(fp);
}

size_t pen_fread(void* buf, size_t size, size_t nmemb, FILE* fp)
{
    return fread(buf, size, nmemb, fp);
}

size_t pen_fwrite(const void* buf, size_t size, size_t nmemb, FILE* fp)
{
    return fwrite(buf, size, nmemb, fp);
}

char* pen_fgets(char* buf, int size, FILE* fp)
{
    return fgets(buf, size, fp);
}

int pen_fseek(FILE* fp, long offset, int whence)
{
    return fseek(fp, offset, whence);
}

long pen_ftell(FILE* fp)
{
    return ftell(fp);
}

int pen_fflush(FILE* fp)
{
    return fflush(fp);
}

int pen_fputc(int c, FILE* fp)
{
    return fputc(c, fp);
}

int pen_putc(int c, FILE* fp)
{
    return putc(c, fp);
}

/* =====================================================================
 * 時刻 (追加分)
 * ===================================================================== */
time_t pen_time(time_t* t)
{
    return time(t);
}

/*
 * pen_get_timestamp — 現在のローカル時刻を "yyyy/mm/dd hh:mm:ss" 形式で返す。
 *
 * タイムゾーンは pen_get_utc_offset_sec() を使用し、OS 依存の localtime() に
 * 頼らずポータブルに変換する（組み込み環境での移植性確保）。
 *
 * buf_size には PEN_TIMESTAMP_LEN (20) 以上を渡すこと。
 * エラー時は "0000/00/00 00:00:00" を書き込み buf を返す。
 */
char* pen_get_timestamp(char* buf, size_t buf_size)
{
    static const char fallback[] = "0000/00/00 00:00:00";

    if (buf == NULL || buf_size < PEN_TIMESTAMP_LEN) {
        return buf;
    }

    struct timeval tv;
    if (pen_gettimeofday(&tv, NULL) != 0) {
        pen_memcpy(buf, fallback, sizeof(fallback));
        return buf;
    }

    /* UTC エポック秒にローカルオフセットを加算してローカル時刻を求める */
    time_t local_sec = (time_t)tv.tv_sec + (time_t)pen_get_utc_offset_sec();

    /*
     * gmtime_r / gmtime_s でカレンダー分解する。
     * ここでは既に UTC + offset 済みの値を渡しているため、
     * OS の TZ 設定に依存せずローカル時刻として扱える。
     */
    struct tm tm_buf;

#if defined(_MSC_VER)
    if (gmtime_s(&tm_buf, &local_sec) != 0) {
        pen_memcpy(buf, fallback, sizeof(fallback));
        return buf;
    }
#elif defined(__MINGW32__) || defined(__MINGW64__)
    struct tm* p = gmtime(&local_sec);
    if (p == NULL) {
        pen_memcpy(buf, fallback, sizeof(fallback));
        return buf;
    }
    tm_buf = *p;
#else
    if (gmtime_r(&local_sec, &tm_buf) == NULL) {
        pen_memcpy(buf, fallback, sizeof(fallback));
        return buf;
    }
#endif

    pen_snprintf(buf, buf_size,
        "%04d/%02d/%02d %02d:%02d:%02d",
        tm_buf.tm_year + 1900,
        tm_buf.tm_mon  + 1,
        tm_buf.tm_mday,
        tm_buf.tm_hour,
        tm_buf.tm_min,
        tm_buf.tm_sec);

    return buf;
}

/*
 * pen_log — 統合ログ出力関数
 *
 * 出力形式: [protocol] [state] [action] yyyy/mm/dd hh:mm:ss 詳細情報\n
 *
 * PENLIB_LOG_CUSTOM_BACKEND が定義されている場合は
 * stream を無視し pen_log_write(line) に委譲する。
 */
void pen_log(FILE*       stream,
             const char* protocol,
             const char* state,
             const char* action,
             const char* fmt, ...)
{
    char ts[PEN_TIMESTAMP_LEN];
    char body[384];
    va_list ap;

    va_start(ap, fmt);
    pen_vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    pen_get_timestamp(ts, sizeof(ts));

#ifdef PENLIB_LOG_CUSTOM_BACKEND
    {
        char line[512];
        pen_snprintf(line, sizeof(line),
            "[%s] [%s] [%s] %s %s",
            protocol, state, action, ts, body);
        pen_log_write(line);
    }
#else
    pen_fprintf(stream,
        "[%s] [%s] [%s] %s %s",
        protocol, state, action, ts, body);
#endif
}


/* =====================================================================
 * 動的メモリ
 * ===================================================================== */

void* pen_malloc(size_t size)
{
    return malloc(size);
}

void* pen_calloc(size_t nmemb, size_t size)
{
    return calloc(nmemb, size);
}

void* pen_realloc(void* ptr, size_t size)
{
    return realloc(ptr, size);
}

void pen_free(void* ptr)
{
    free(ptr);
}

/* =====================================================================
 * フォーマット出力
 * ===================================================================== */

int pen_vsnprintf(char* buf, size_t size, const char* fmt, va_list ap)
{
    if (buf == NULL || size == 0) return -1;
#if defined(_MSC_VER)
    int ret = _vsnprintf_s(buf, size, _TRUNCATE, fmt, ap);
    if (ret < 0) {
        buf[size - 1] = '\0';
        ret = (int)(size - 1);
    }
    return ret;
#else
    return vsnprintf(buf, size, fmt, ap);
#endif
}

int pen_snprintf(char* buf, size_t size, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = pen_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int pen_sprintf(char* buf, const char* fmt, ...)
{
    /*
     * 安全でない sprintf のラッパー。
     * バッファサイズが静的に保証される箇所 (picotls tid 等) 専用。
     * 組み込み環境では pen_snprintf に置き換えることを推奨。
     */
    va_list ap;
    va_start(ap, fmt);
#if defined(_MSC_VER)
    /* MSVC: sprintf は非推奨。vsprintf_s を使う (バッファ不足時は abort) */
    int ret = vsprintf_s(buf, 4096, fmt, ap);
#else
    int ret = vsprintf(buf, fmt, ap);
#endif
    va_end(ap);
    return ret;
}

#if defined(PENLIB_NO_STDIO)

/* 組み込み環境: stdio が使えない場合は no-op */
int pen_fprintf (FILE* stream, const char* fmt, ...) { (void)stream; (void)fmt; return 0; }
int pen_vfprintf(FILE* stream, const char* fmt, va_list ap) { (void)stream; (void)fmt; (void)ap; return 0; }
int pen_printf  (const char* fmt, ...)               { (void)fmt; return 0; }
void pen_perror (const char* msg)                    { (void)msg; }

#else

int pen_fprintf(FILE* stream, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

int pen_vfprintf(FILE* stream, const char* fmt, va_list ap)
{
    /*
     * va_list 版 fprintf。picoquic の内部ログ関数 (debug_printf 等) から呼ばれる。
     * MSVC では vfprintf_s に委譲してバッファオーバーラン検出を有効にする。
     * 組み込み環境では独自の出力バックエンド (UART / RTT 等) に差し替えること。
     */
#if defined(_MSC_VER)
    return vfprintf_s(stream, fmt, ap);
#else
    return vfprintf(stream, fmt, ap);
#endif
}

int pen_printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

void pen_perror(const char* msg)
{
    /*
     * errno に対応するエラーメッセージを stderr に出力する。
     * POSIX perror(3) と同等。
     * 組み込み環境では errno が存在しない場合があるため、
     * pen_fprintf(stderr, "%s\n", msg) 等に差し替えること。
     */
    perror(msg);
}

#endif /* PENLIB_NO_STDIO */

/* =====================================================================
 * 時刻 API
 * ===================================================================== */

int pen_gettimeofday(struct timeval* tv, void* tz)
{
#ifdef _WINDOWS
    /*
     * Windows 版 gettimeofday 実装。
     * GetSystemTimeAsFileTime で 100ns 精度の UTC 時刻を取得し
     * struct timeval (sec + usec) に変換する。
     * wincompat.h / wintimeofday には依存しない。
     */
    (void)tz;
    if (tv == NULL) return -1;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    /* FILETIME: 100ns単位、1601/1/1 起点 */
    ULONGLONG t = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
    /* UNIX エポック (1970/1/1) への変換: 差分 = 116444736000000000 * 100ns */
    t -= 116444736000000000ULL;
    /* 100ns → usec */
    tv->tv_sec  = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t % 10000000ULL) / 10ULL);
    return 0;
#else
    return gettimeofday(tv, (struct timezone*)tz);
#endif
}

int pen_clock_gettime(int clk_id, struct timespec* ts)
{
#ifdef _WINDOWS
    if (ts == NULL) return -1;

    if (clk_id == CLOCK_REALTIME) {
        /*
         * CLOCK_REALTIME: GetSystemTimeAsFileTime ベース (ms 精度)
         * VS2015+ では timespec_get(TIME_UTC) が利用可能だが、
         * MinGW との互換性のため FILETIME を直接使う。
         */
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        ULONGLONG t = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime);
        t -= 116444736000000000ULL;  /* UNIX エポックへの変換 */
        ts->tv_sec  = (time_t)(t / 10000000ULL);
        ts->tv_nsec = (long)((t % 10000000ULL) * 100ULL);
        return 0;
    } else {
        /*
         * CLOCK_MONOTONIC (その他): QueryPerformanceCounter ベース
         * 高分解能だが絶対時刻ではない点に注意。
         */
        LARGE_INTEGER freq, cnt;
        if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&cnt)) {
            return -1;
        }
        ts->tv_sec  = (time_t)(cnt.QuadPart / freq.QuadPart);
        ts->tv_nsec = (long)(((cnt.QuadPart % freq.QuadPart) * 1000000000LL)
                             / freq.QuadPart);
        return 0;
    }
#else
    return clock_gettime(clk_id, ts);
#endif
}

/* =====================================================================
 * プロセス制御
 * ===================================================================== */

void pen_abort(void)
{
    /*
     * 組み込み環境では watchdog リセット等に差し替えること。
     * 例: while(1) {} または NVIC_SystemReset() 等
     */
    abort();
}

void pen_exit(int status)
{
    /*
     * 組み込み環境では通常 exit() が使えない。
     * 差し替え例: while(1) {} または安全なシャットダウン処理
     */
    exit(status);
}

/* =====================================================================
 * スレッド補助 API
 * ===================================================================== */

void pen_thread_exit(void* retval)
{
    /*
     * 実行中スレッドを終了する。
     *
     * POSIX: pthread_exit(retval) — スレッド固有の終了処理
     *   スタックアンワインドと TLS デストラクタが実行される。
     *
     * Windows: ExitThread が相当するが、C++ デストラクタが呼ばれない
     *   ため通常はスレッド関数から return するほうが安全。
     *   ここでは互換性のため ExitThread(0) を呼ぶ。
     *   retval は Windows では意味を持たない (DWORD に収まらないため)。
     *
     * 組み込み (FreeRTOS 等):
     *   vTaskDelete(NULL) 等に差し替えること。
     */
#ifdef _WINDOWS
    (void)retval;
    ExitThread(0);
#else
    pthread_exit(retval);
#endif
}

void pen_thread_setname(const char* name)
{
    /*
     * 現在のスレッドにデバッグ名を設定する。
     * デバッガやプロファイラでのスレッド識別に使用。
     * 失敗しても動作に影響しないため戻り値は無視する。
     *
     * Windows: SetThreadDescription (Vista 以降、UNICODE 変換が必要)
     * macOS  : pthread_setname_np(name)  — 引数 1 個版
     * Linux  : prctl(PR_SET_NAME, ...)   — 15 バイト制限あり
     *
     * 組み込み:
     *   空実装のまま (デバッグ不要) か RTOS の対応 API に差し替えること。
     */
#ifdef _WINDOWS
    /* Windows: char* → wchar_t* 変換して SetThreadDescription を呼ぶ */
    wchar_t wname[257];
    wname[0] = 0;
    if (swprintf(wname, 256, L"%S", name) >= 0) {
        /* SetThreadDescription は戻り値 HRESULT — 失敗しても続行 */
        SetThreadDescription(GetCurrentThread(), wname);
    }
#elif defined(__APPLE__)
    pthread_setname_np(name);
#elif defined(__linux__)
    prctl(PR_SET_NAME, name, 0, 0, 0);
#else
    (void)name;  /* その他: 空実装 */
#endif
}

/* =====================================================================
 * タイムゾーン
 *
 * pen_get_utc_offset_sec():
 *   UTC からのオフセット秒数を返す。東 = 正、西 = 負。
 *
 *   優先順位:
 *     1. PENQUIC_TZ_FROM_OS が定義されている場合:
 *        OS の localtime() から動的に取得する。
 *     2. PENQUIC_UTC_OFFSET_SEC が定義されている場合:
 *        そのコンパイル時定数を返す。
 *     3. いずれも未定義の場合:
 *        デフォルト JST (UTC+9 = 32400 秒) を返す。
 * ===================================================================== */
int32_t pen_get_utc_offset_sec(void)
{
#if defined(PENQUIC_TZ_FROM_OS)
    /*
     * OS のタイムゾーン設定を使用する。
     * localtime() で現地時刻を取得し、mktime(gmtime()) との差分で
     * UTC オフセットを計算する。
     * Windows / POSIX 共通で動作する。
     */
    time_t now = time(NULL);
    struct tm gm_tm, local_tm;

#  if defined(_MSC_VER)
    gmtime_s(&gm_tm, &now);
    localtime_s(&local_tm, &now);
#  elif defined(__MINGW32__) || defined(__MINGW64__)
    /* MinGW の gmtime/localtime はスレッドセーフでないが単純利用は問題なし */
    struct tm *p;
    p = gmtime(&now);    if (p) gm_tm    = *p;
    p = localtime(&now); if (p) local_tm = *p;
#  else
    /* POSIX: スレッドセーフ版を使用 */
    gmtime_r(&now, &gm_tm);
    localtime_r(&now, &local_tm);
#  endif

    /* 両者を "構造上の" epoch 起点の秒数に変換して差を取る */
    gm_tm.tm_isdst    = 0;
    local_tm.tm_isdst = 0;
    time_t gm_t    = mktime(&gm_tm);
    time_t local_t = mktime(&local_tm);

    if (gm_t == (time_t)-1 || local_t == (time_t)-1) {
        return 32400; /* フォールバック: JST */
    }
    return (int32_t)(local_t - gm_t);

#elif defined(PENQUIC_UTC_OFFSET_SEC)
    /* コンパイル時定数で指定された固定オフセット */
    return (int32_t)(PENQUIC_UTC_OFFSET_SEC);

#else
    /* デフォルト: 日本標準時 JST (UTC+9) */
    return 32400;
#endif
}

/* =====================================================================
 * pen_imaxabs — intmax_t の絶対値
 *
 * MinGW の <inttypes.h> は imaxabs を __CRT_INLINE で定義するため、
 * 複数の翻訳単位でリンク時に「多重定義」エラーが発生する。
 * penlib.h で __CRT__NO_INLINE を定義して inline 展開を抑制し、
 * 実体をここに一本化することで問題を解消する。
 *
 * 組み込み環境への移植時はここを差し替えること。
 * ===================================================================== */
intmax_t pen_imaxabs(intmax_t j)
{
    return (j >= 0) ? j : -j;
}

/* =====================================================================
 * UTF-8 デコード
 * ===================================================================== */

size_t decode_utf8(
    const uint8_t* data,
    size_t         len,
    size_t         max_len,
    char*          out,
    size_t         out_size)
{
    if (data == NULL || out == NULL || out_size == 0) {
        return (size_t)-1;
    }

    size_t limit = (max_len > 0 && max_len < len) ? max_len : len;
    size_t i = 0;
    size_t o = 0;

    while (i < limit && o < out_size - 1) {
        uint8_t c = data[i];
        int bytes;

        if      (c < 0x80)               bytes = 1;
        else if ((c & 0xE0) == 0xC0)     bytes = 2;
        else if ((c & 0xF0) == 0xE0)     bytes = 3;
        else if ((c & 0xF8) == 0xF0)     bytes = 4;
        else { out[o++] = '.'; i++; continue; }

        if (i + (size_t)bytes > limit) {
            out[o++] = '.'; i++; continue;
        }

        int valid = 1;
        for (int j = 1; j < bytes; j++) {
            if ((data[i + j] & 0xC0) != 0x80) { valid = 0; break; }
        }

        if (valid && o + (size_t)bytes <= out_size - 1) {
            pen_memcpy(&out[o], &data[i], (size_t)bytes);
            o += (size_t)bytes;
        } else {
            out[o++] = '.';
        }
        i += (size_t)bytes;
    }

    out[o] = '\0';
    return o;
}
