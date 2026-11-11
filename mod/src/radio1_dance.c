/*
 * BBC Radio 1 Dance -- a live radio station for Forza Horizon 2 (FH2 Mod Platform mod). A fork of Horizon Most Wanted.
 *
 * The station is data, as in Horizon Most Wanted: <azimuth>\Radio1Dance\overrides\ mirrors the game's media tree and
 * every file in it replaces the game file of the same path (RadioInfo_*.xml with the station added, the HUD xaml, the
 * icon). Its playlist holds two 16-minute "tracks", R1D_Live_A and R1D_Live_B, and silent stinger/DJ placeholders.
 * This DLL:
 *   1. loads those overrides through the Mod Platform,
 *   2. serves the live tracks as an FMOD user stream fed from a ring buffer: every byte is what ffmpeg decodes from the
 *      BBC's HLS stream. ffmpeg (<azimuth>\Radio1Dance\tools\ffmpeg.exe) runs as a child process only while the
 *      station is heard,
 *   3. serves the placeholders (R1D_Silent_*) as a one-second silent WAV from memory,
 *   4. tells the radio module the station is eligible when cycling (a station nobody registered is skipped),
 *   5. reads the BBC's "now playing" feed while the station is heard and shows the song in the radio HUD.
 * The live stream follows FH2 Universal Radio (Trinculo54, github.com/Trinculo54/FH2-Universal-Radio), the FH2 port of
 * FH6 Universal Radio (g0ldyy and contributors, github.com/g0ldyy/fh6-universal-radio): a user stream with no length
 * limit and a small decode buffer, the newest stream alone fed, 16-minute tracks, names written in place.
 *
 * FMOD_CREATESOUNDEXINFO in this build is FMOD 1.06's 216-byte layout: SystemI::createSound (game+0x19b2080) copies 27
 * qwords, takes userdata from +0x68, the sound group from +0xb8, the inclusion list count from +0x30 and dlsname /
 * encryptionkey from +0x50 / +0x58, which puts the PCM read callback at +0x38.
 */
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <winhttp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fh2mod.h"

#define STATION_NAME "BBC Radio 1 Dance"   /* StationName, exactly as in the XML and the HUD trigger */
#define LIVE_PREFIX "R1D_Live"             /* SoundName prefix of the live tracks */
#define SILENT_PREFIX "R1D_Silent"         /* SoundName prefix of the silent stinger / DJ placeholders */
#define MOD_FOLDER L"Radio1Dance"          /* under <azimuth> */
#define MOD_VERSION "1.0.1"

static const char DEFAULT_URL[] = "http://as-hls-ww-live.akamaized.net/pool_62063831/live/ww/bbc_radio_one_dance/"
                                  "bbc_radio_one_dance.isml/bbc_radio_one_dance-audio%3d320000.norewind.m3u8";
static const char DEFAULT_SERVICE[] = "bbc_radio_one_dance";

static const FH2ModApi* A;
static FH2Mod* M;
static wchar_t g_dir[MAX_PATH];      /* <azimuth>\Radio1Dance */
static char g_url[1024];
static char g_service[64];

static void say(const char* fmt, ...) {
    char b[1200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    A->log(M, b);
}

static unsigned char* read_all(const wchar_t* path, size_t* n) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    DWORD sz = GetFileSize(h, NULL), got = 0;
    unsigned char* b = (unsigned char*)malloc((size_t)sz + 1);
    if (!b || !ReadFile(h, b, sz, &got, NULL) || got != sz) { CloseHandle(h); free(b); return NULL; }
    CloseHandle(h); b[sz] = 0; *n = sz; return b;
}

/* ------------------------------------------------------------- overrides */
/* Every file under <dir> replaces the game file at the same relative path. The Mod Platform copies the data. */
static int load_overrides(const wchar_t* dir, const char* rel) {
    int count = 0;
    wchar_t pat[MAX_PATH]; swprintf(pat, MAX_PATH, L"%ls\\*", dir);
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        wchar_t full[MAX_PATH]; swprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName);
        char name[260]; WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL);
        char sub[512]; snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { count += load_overrides(full, sub); continue; }
        size_t n = 0; unsigned char* data = read_all(full, &n);
        if (!data) { say("cannot read override %s", sub); continue; }
        if (A->set_file(M, sub, data, n)) count++; else say("override %s was refused (another mod replaces that file)", sub);
        free(data);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return count;
}

/* ---------------------------------------------------------- station unlock */
#define RVA_ISUNLOCKED 0x668810u   /* RadioModule::IsStationUnlocked(this, const std::string* name): 1 = skip, 0 = eligible */
typedef char (*IsUnlockedFn)(void*, const void*);
static IsUnlockedFn o_isUnlocked;

/* MSVC std::string: 16-byte inline buffer or heap pointer at +0, size at +0x10, capacity at +0x18. */
static int string_is(const void* s, const char* lit) {
    const unsigned char* b = (const unsigned char*)s;
    size_t size = *(const size_t*)(b + 0x10), cap = *(const size_t*)(b + 0x18);
    if (size != strlen(lit) || cap < 15 || cap > 4096) return 0;
    const char* p = cap > 15 ? *(const char* const*)b : (const char*)b;
    return !memcmp(p, lit, size);
}

static char d_isUnlocked(void* mod, const void* name) {
    if (name && string_is(name, STATION_NAME)) return 0;
    return o_isUnlocked(mod, name);
}

/* ------------------------------------------------------------ the live buffer */
/* 48 kHz stereo 16-bit, what ffmpeg is asked to produce and what the FMOD user stream is declared as. */
#define RATE 48000
#define FRAME 4
#define BPS (RATE * FRAME)                      /* bytes per second */
#define RING (BPS * 40)                         /* 40 s of audio */
/* A FIFO with one reader: the newest live stream (see StreamTag). The BBC's HLS comes in 6.4 s segments; ffmpeg starts
   3 segments back (-live_start_index -3), so a run opens with a ~19 s burst, which is the cushion, and then gets one
   segment every 6.4 s. FMOD reads the user stream DECODE_FRAMES at a time and keeps next to nothing read ahead, so a
   reopened track (a song change, the radio's 5 s clock check) loses next to nothing. (A WAV file served through FMOD's
   file callbacks is read 1.4 s at a time with up to 4 s kept ahead, and every reopen skipped that much and drained
   the buffer, 2026-11-10.) This is how FH2 Universal Radio (Trinculo54) feeds its stations. A new listening period
   (after the station was not heard, e.g. the pause menu) plays only a run started after it began: back to live,
   nothing stale played (user's choice). */
#define PREROLL (BPS * 3 / 2)                   /* start, and start again after running dry, once this much is in */
#define TARGET_LAG (BPS * 16)                   /* where a move forward puts the listener, behind the newest audio */
#define MAX_LAG (BPS * 30)                      /* more than this buffered: move forward to TARGET_LAG */
#define DECODE_FRAMES 2048                      /* FMOD's stream buffer for the user stream: ~43 ms per read */
/* Nothing is downloaded unless the station is being heard, and "heard" means FMOD is reading the newest live stream
   (it keeps doing so while the track plays; it stops when the station is switched away, the radio is off or the game
   pauses the radio). ffmpeg starts once the station has been heard for HEARD_MS, so scrolling past it starts nothing,
   and stops IDLE_MS after the last read. The now-playing feed is polled only in the same periods. */
#define HEARD_MS 1000
#define IDLE_MS 3000

static unsigned char* g_ring;
static unsigned long long g_wr, g_rd;           /* bytes written / read since start, under g_ring_cs */
static unsigned long long g_run_start;          /* where the current ffmpeg run's audio begins in the ring */
static CRITICAL_SECTION g_ring_cs;
static int g_primed;
static LONG g_run_id, g_need_run;               /* runs so far; the first run the listening period may play (under g_ring_cs) */
static unsigned g_underruns, g_skips;
/* The broadcast time (unix ms) of the first byte of the current ffmpeg run (an estimate, see run_ffmpeg); 0 = no
   run yet. With g_rd it says which moment of the broadcast the listener hears. */
static volatile LONG64 g_run_wall;
static volatile LONG64 g_last_read;             /* GetTickCount64() of FMOD's last read of the live stream, 0 = never */
static volatile LONG64 g_heard_since;           /* GetTickCount64() when the current listening period began */
static HANDLE g_wake;                           /* set when a listening period begins */
/* what FMOD did with the live stream since it was last opened, reported when ffmpeg stops (nothing is logged from
   FMOD's own threads) */
static volatile LONG64 g_open_tick, g_first_read_tick;
static volatile LONG g_reads;

static unsigned long long align_frame(unsigned long long v) { return v - v % FRAME; }

static void ring_write(const unsigned char* p, size_t n) {
    EnterCriticalSection(&g_ring_cs);
    if (n > RING) { p += n - RING; g_wr += n - RING; n = RING; }
    size_t at = (size_t)(g_wr % RING), first = n < RING - at ? n : RING - at;
    memcpy(g_ring + at, p, first);
    memcpy(g_ring, p + first, n - first);
    g_wr += n;
    if (g_wr - g_rd > RING) g_rd = align_frame(g_wr - RING + FRAME - 1);   /* overrun: the oldest audio is gone */
    LeaveCriticalSection(&g_ring_cs);
}

/* A new ffmpeg run starts on a frame boundary even if the last one ended mid-frame; what is left of an earlier run
   is stale. */
static void ring_new_run(void) {
    EnterCriticalSection(&g_ring_cs);
    if (g_wr % FRAME) {
        static const unsigned char zero[FRAME];
        LeaveCriticalSection(&g_ring_cs);
        ring_write(zero, FRAME - (size_t)(g_wr % FRAME));
        EnterCriticalSection(&g_ring_cs);
    }
    g_run_start = g_rd = g_wr;
    g_run_id++;
    g_primed = 0;
    LeaveCriticalSection(&g_ring_cs);
}

/* Seconds of audio buffered ahead of the listener; 0 while waiting for a run. */
static double buffered_s(void) {
    EnterCriticalSection(&g_ring_cs);
    double s = g_run_id >= g_need_run ? (double)(g_wr - g_rd) / BPS : 0;
    LeaveCriticalSection(&g_ring_cs);
    return s;
}

/* ------------------------------------------------------------ the live stream */
/* Each live stream carries a StreamTag as its FMOD userdata. The newest one is fed from the ring; an older one (a
   stream the game is still fading out, or that the radio replaced) gets silence, so two streams never split the ring
   between them (as in FH2 Universal Radio). */
typedef struct { volatile LONG64 gen; } StreamTag;
#define TAGS 64
static StreamTag g_tags[TAGS];
static volatile LONG64 g_next_gen, g_active_gen;
typedef int (*GetUserDataFn)(void*, void**);
static GetUserDataFn g_getUserData;             /* FMOD::Sound::getUserData, game+0x199fbe0 */

/* A new stream takes over the ring with its first read, not when it is created: until FMOD has opened it and starts
   reading, the old one keeps playing, so a track change has no gap of its own. */
static StreamTag* new_tag(void) {
    LONG64 gen = InterlockedIncrement64(&g_next_gen);
    StreamTag* t = &g_tags[gen % TAGS];
    t->gen = gen;
    return t;
}

static int is_tag(const void* ud) { return ud >= (const void*)&g_tags[0] && ud < (const void*)&g_tags[TAGS]; }

/* FMOD's stream thread asks for the next `len` bytes (whole frames). Never blocks for long. */
static int pcm_read(void* sound, void* data, unsigned int len) {
    unsigned char* out = (unsigned char*)data;
    unsigned n = 0;
    void* ud = NULL;
    if (g_getUserData) g_getUserData(sound, &ud);
    LONG64 gen = is_tag(ud) ? ((StreamTag*)ud)->gen : 0, active = g_active_gen;
    if (gen > active) { InterlockedCompareExchange64(&g_active_gen, gen, active); active = g_active_gen; }
    if (!gen || gen != active) { memset(out, 0, len); return 0; }
    LONG64 now = (LONG64)GetTickCount64();
    LONG64 prev = InterlockedExchange64(&g_last_read, now);
    int new_period = !prev || now - prev >= IDLE_MS;
    if (new_period) { InterlockedExchange64(&g_heard_since, now); SetEvent(g_wake); }
    if (InterlockedIncrement(&g_reads) == 1) InterlockedExchange64(&g_first_read_tick, now);
    EnterCriticalSection(&g_ring_cs);
    if (new_period) { g_need_run = g_run_id + 1; g_primed = 0; }   /* back to live: wait for a fresh run */
    if (g_run_id >= g_need_run) {
        if (!g_primed && g_wr - g_rd >= PREROLL) g_primed = 1;
        if (g_primed && g_wr - g_rd > MAX_LAG) { g_rd = align_frame(g_wr - TARGET_LAG); g_skips++; }
        if (g_primed) {
            unsigned long long avail = g_wr - g_rd;
            n = (unsigned)(avail < len ? avail : len) & ~(unsigned)(FRAME - 1);
            size_t at = (size_t)(g_rd % RING), first = n < RING - at ? n : RING - at;
            memcpy(out, g_ring + at, first);
            memcpy(out + first, g_ring, n - first);
            g_rd += n;
            if (n < len) { g_primed = 0; g_underruns++; }
        }
    }
    LeaveCriticalSection(&g_ring_cs);
    if (n < len) memset(out + n, 0, len - n);
    return 0;   /* FMOD_OK */
}

/* Seeking a live stream means nothing: the radio keeps the event within 5 s of the station's clock. */
static int pcm_setpos(void* sound, int subsound, unsigned int position, unsigned int postype) {
    (void)sound; (void)subsound; (void)position; (void)postype;
    return 0;
}

/* One line on what FMOD did with the live stream since the track was opened. */
static void fmod_report(char* out, size_t cap) {
    LONG64 open = g_open_tick, first = g_first_read_tick, last = g_last_read;
    if (!g_reads) snprintf(out, cap, "FMOD has not read the newest stream since it was opened");
    else snprintf(out, cap, "FMOD read the newest stream %ld times, first %.1f s and last %.1f s after it was opened",
                  g_reads, open ? (first - open) / 1000.0 : -1.0, open ? (last - open) / 1000.0 : -1.0);
}

/* The silent stinger / DJ placeholders: a one-second silent WAV in memory (the path Horizon Most Wanted's files take). */
static unsigned char* g_silence;
static unsigned g_silence_size;

static void wav_header(unsigned char* h, unsigned data) {
    memcpy(h, "RIFF", 4); *(unsigned*)(h + 4) = 36 + data; memcpy(h + 8, "WAVEfmt ", 8);
    *(unsigned*)(h + 16) = 16; *(unsigned short*)(h + 20) = 1; *(unsigned short*)(h + 22) = 2;
    *(unsigned*)(h + 24) = RATE; *(unsigned*)(h + 28) = BPS; *(unsigned short*)(h + 32) = FRAME; *(unsigned short*)(h + 34) = 16;
    memcpy(h + 36, "data", 4); *(unsigned*)(h + 40) = data;
}

/* ------------------------------------------------------------ HTTP, clocks */
static HINTERNET g_http;                        /* one WinHTTP session for the feed, the schedule and the playlist */

/* GET url into a malloc'd, NUL-terminated buffer (NULL on failure; status/error say why). */
static char* http_get(const char* url, DWORD* status, DWORD* error) {
    wchar_t wurl[1024], host[256], path[1536], extra[1024];
    char* body = NULL; size_t len = 0;
    *status = 0; *error = 0;
    MultiByteToWideChar(CP_UTF8, 0, url, -1, wurl, 1024);
    URL_COMPONENTS uc; memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path; uc.dwUrlPathLength = 1024;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    if (!g_http || !WinHttpCrackUrl(wurl, 0, 0, &uc)) { *error = GetLastError(); return NULL; }
    wcsncat(path, extra, 1535 - wcslen(path));
    HINTERNET c = WinHttpConnect(g_http, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD sz = sizeof *status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, status, &sz, WINHTTP_NO_HEADER_INDEX);
        for (;;) {
            DWORD avail = 0, n = 0;
            if (!WinHttpQueryDataAvailable(r, &avail) || !avail || len + avail > (1u << 20)) break;
            char* nb = (char*)realloc(body, len + avail + 1);
            if (!nb) break;
            body = nb;
            if (!WinHttpReadData(r, body + len, avail, &n) || !n) break;
            len += n;
        }
        if (body) body[len] = 0;
    } else {
        *error = GetLastError();
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    return body;
}

/* The PC's clock as unix ms (UTC). */
static LONG64 unix_ms(void) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (LONG64)((u.QuadPart - 116444736000000000ULL) / 10000);
}

/* "2026-11-10T06:00:00Z" or "...T04:33:23.200000Z" (UTC) -> unix ms, 0 if it does not parse. */
static LONG64 iso_ms(const char* s) {
    int y, mo, d, h, mi; double sec;
    if (sscanf(s, "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi, &sec) != 6 || mo < 1 || mo > 12) return 0;
    y -= mo <= 2;                                /* days from civil (H. Hinnant) */
    LONG64 era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    LONG64 days = era * 146097 + (LONG64)doe - 719468;
    return days * 86400000LL + h * 3600000LL + mi * 60000LL + (LONG64)(sec * 1000.0);
}

/* How far behind the playlist's live edge ffmpeg starts (-live_start_index -3): the length of the last 3 segments,
   in ms; 0 if the playlist has fewer. The playlist's EXT-X-PROGRAM-DATE-TIME is no broadcast clock: its newest
   segment ends about 30 s in the future (2026-11-10, against a PC clock that matched the server's Date header). */
static LONG64 playlist_tail_ms(const char* body) {
    double dur[128]; int n = 0;
    for (const char* l = body; l && *l; ) {
        const char* e = strchr(l, '\n');
        if (!strncmp(l, "#EXTINF:", 8) && n < 128) dur[n++] = atof(l + 8);
        l = e ? e + 1 : NULL;
    }
    if (n < 3) return 0;
    return (LONG64)((dur[n - 1] + dur[n - 2] + dur[n - 3]) * 1000.0);
}

/* A segment is in the playlist this long after it went on air (it has to be complete, then packaged and spread).
   An estimate; name_delay in stream.ini corrects it. */
#define HLS_AVAILABLE_MS 15000

/* The broadcast moment the listener hears now (unix ms), 0 if unknown. */
static LONG64 audio_wall_ms(void) {
    EnterCriticalSection(&g_ring_cs);
    LONG64 w = g_run_wall && g_run_id >= g_need_run && g_primed ? g_run_wall + (LONG64)((g_rd - g_run_start) * 1000 / BPS) : 0;
    LeaveCriticalSection(&g_ring_cs);
    return w;
}

/* ---------------------------------------------------------------- ffmpeg */
static HANDLE g_job;

static int heard_recently(void) {
    LONG64 last = g_last_read;
    return last && (LONG64)GetTickCount64() - last < IDLE_MS;
}

/* Heard now, and for at least HEARD_MS without a gap. */
static int heard_steadily(void) {
    return heard_recently() && (LONG64)GetTickCount64() - g_heard_since >= HEARD_MS;
}

/* Runs one ffmpeg until it ends or the station stops being heard. Returns the seconds of audio it delivered, or -1 if
   it could not be started (logged once per distinct error). */
static double run_ffmpeg(void) {
    static DWORD last_start_error;
    wchar_t exe[MAX_PATH], logp[MAX_PATH], cmd[2048], wurl[1024];
    swprintf(exe, MAX_PATH, L"%ls\\tools\\ffmpeg.exe", g_dir);
    swprintf(logp, MAX_PATH, L"%ls\\ffmpeg.log", g_dir);
    MultiByteToWideChar(CP_UTF8, 0, g_url, -1, wurl, 1024);
    swprintf(cmd, 2048, L"\"%ls\" -nostdin -hide_banner -loglevel error -rw_timeout 15000000 -live_start_index -3 -i \"%ls\" "
             L"-vn -ac 2 -ar %d -f s16le pipe:1", exe, wurl, RATE);

    /* the broadcast time of the segment ffmpeg is about to start with: now, less the 3 segments it starts behind the
       live edge (read from the playlist just before ffmpeg reads it) and the time a segment takes to show up */
    DWORD pst, perr;
    char* playlist = http_get(g_url, &pst, &perr);
    LONG64 tail = playlist && pst == 200 ? playlist_tail_ms(playlist) : 0;
    free(playlist);
    LONG64 run_wall = unix_ms() - (tail ? tail : 19200) - HLS_AVAILABLE_MS;

    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 20)) {
        DWORD e = GetLastError();
        if (e != last_start_error) say("ffmpeg not started: CreatePipe failed (error %lu)", e);
        last_start_error = e;
        return -1;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE err = CreateFileW(logp, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, 0, NULL);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (err == INVALID_HANDLE_VALUE) err = NULL;
    if (nul == INVALID_HANDLE_VALUE) nul = NULL;

    /* hand ffmpeg these handles and nothing else of the game's */
    HANDLE list[3]; int nlist = 0;
    list[nlist++] = wr;
    if (err) list[nlist++] = err;
    if (nul) list[nlist++] = nul;
    SIZE_T asz = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &asz);
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = (LPPROC_THREAD_ATTRIBUTE_LIST)malloc(asz);
    int have_list = attrs && InitializeProcThreadAttributeList(attrs, 1, 0, &asz);
    int list_set = have_list && UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, list, nlist * sizeof(HANDLE), NULL, NULL);
    STARTUPINFOEXW si; memset(&si, 0, sizeof si);
    si.StartupInfo.cb = list_set ? sizeof si : sizeof si.StartupInfo;
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = nul;
    si.StartupInfo.hStdOutput = wr;
    si.StartupInfo.hStdError = err ? err : wr;
    if (list_set) si.lpAttributeList = attrs;
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
    DWORD flags = CREATE_NO_WINDOW | CREATE_SUSPENDED | (list_set ? EXTENDED_STARTUPINFO_PRESENT : 0);
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, TRUE, flags, NULL, g_dir, &si.StartupInfo, &pi);
    DWORD e = GetLastError();
    if (have_list) DeleteProcThreadAttributeList(attrs);
    free(attrs);
    CloseHandle(wr);
    if (err) CloseHandle(err);
    if (nul) CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        if (e != last_start_error) say("ffmpeg not started: CreateProcess failed for %ls (error %lu)", exe, e);
        last_start_error = e;
        return -1;
    }
    last_start_error = 0;
    if (g_job && !AssignProcessToJobObject(g_job, pi.hProcess))
        say("ffmpeg pid %lu is not in the kill-on-exit job (error %lu): it may outlive the game if the game crashes", pi.dwProcessId, GetLastError());
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    ring_new_run();
    g_run_wall = run_wall;
    say("ffmpeg started (pid %lu) for %s; its audio starts about %.0f s behind the broadcast (%s)", pi.dwProcessId, g_url,
        (unix_ms() - run_wall) / 1000.0, tail ? "3 segments from the playlist" : "playlist not read, 3 x 6.4 s assumed");

    static unsigned char buf[1 << 16];
    unsigned long long got = 0;
    ULONGLONG t0 = GetTickCount64();
    const char* why = "it ended";
    for (;;) {
        DWORD avail = 0, n = 0;
        if (!heard_recently()) { why = "the station is no longer heard"; TerminateProcess(pi.hProcess, 0); break; }
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break;      /* broken pipe: ffmpeg exited */
        if (avail) {
            if (!ReadFile(rd, buf, avail < sizeof buf ? avail : sizeof buf, &n, NULL) || !n) break;
            ring_write(buf, n);
            got += n;
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) {
            if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) continue;   /* drain what it left */
            break;
        }
    }
    WaitForSingleObject(pi.hProcess, 2000);
    DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    double secs = (double)got / BPS;
    char rep[700]; fmod_report(rep, sizeof rep);
    say("ffmpeg (pid %lu) stopped because %s: exit code %lu, %.0f s of audio in %.0f s, %u underruns, %u skips; %s (details in ffmpeg.log)",
        pi.dwProcessId, why, code, secs, (GetTickCount64() - t0) / 1000.0, g_underruns, g_skips, rep);
    return secs;
}

static DWORD WINAPI stream_thread(LPVOID unused) {
    (void)unused;
    int backoff = 2;
    for (;;) {
        WaitForSingleObject(g_wake, 250);
        if (!heard_steadily()) { if (!heard_recently()) backoff = 2; continue; }
        double secs = run_ffmpeg();
        if (!heard_recently()) continue;
        if (secs > 60) backoff = 2;
        if (secs >= 0) say("ffmpeg restarts in %d s (the station is still being heard)", backoff);
        Sleep(backoff * 1000);
        backoff = backoff < 30 ? backoff * 2 : 30;
    }
    return 0;
}

/* ----------------------------------------------------------- now playing */
/* The BBC's own "now playing" feed for the service (the one bbc.co.uk/sounds shows), polled while the station is
   heard: the latest songs, each with its start and end in seconds from the start of the current show, and the show's
   start time from the schedule. With the audio clock (audio_wall_ms) that says which song the listener hears right
   now; a song changes in the HUD when it changes in their ears. Without the clock: the song the feed marks as now
   playing, shown after what is buffered here plus a guess at the HLS delay. name_delay in stream.ini (seconds, may be
   negative) moves the names later or earlier. */
static CRITICAL_SECTION g_now_cs;
typedef struct { char artist[160], title[160]; LONG gen; LONG64 due; } Song;
static Song g_song;                 /* the song to show, under g_now_cs; empty names = no song (presenter, jingle) */
static int g_name_delay_ms;
#define FALLBACK_DELAY_MS 15000     /* without the show's start time: HLS delay on top of what is buffered */
#define FIRST_NAME_MS 6000          /* after tuning in, the station popup shows for a few seconds */
#define FEED_AFTER_MS 300           /* the station has played this long: ask the feed */
/* song_popup=1 in stream.ini: end the live track when a new song starts, so the radio pops up its name. Off by
   default: the track change is audible (a micro-pause) and once left the radio between tracks for good
   (2026-11-10); FH2 Universal Radio only writes the names in place, which the HUD shows when it next appears. */
static int g_song_popup;

typedef struct { char artist[160], title[160]; int start, end, now; } Seg;
#define MAX_SEGS 6

/* Copies the JSON string value that starts at *p (just after the opening quote) into out as UTF-8. */
static const char* json_string(const char* p, char* out, size_t cap) {
    size_t o = 0;
    while (*p && *p != '"') {
        unsigned cp = (unsigned char)*p++;
        if (cp == '\\' && *p) {
            char c = *p++;
            if (c == 'u') {
                if (!p[0] || !p[1] || !p[2] || !p[3]) break;
                cp = (unsigned)strtoul((char[5]){p[0], p[1], p[2], p[3], 0}, NULL, 16); p += 4;
                if (cp >= 0xd800 && cp < 0xdc00 && p[0] == '\\' && p[1] == 'u' && p[2] && p[3] && p[4] && p[5]) {
                    unsigned lo = (unsigned)strtoul((char[5]){p[2], p[3], p[4], p[5], 0}, NULL, 16);
                    if (lo >= 0xdc00 && lo < 0xe000) { cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00); p += 6; }
                }
                char u[4]; int k = 0;
                if (cp < 0x80) u[k++] = (char)cp;
                else if (cp < 0x800) { u[k++] = (char)(0xc0 | cp >> 6); u[k++] = (char)(0x80 | (cp & 0x3f)); }
                else if (cp < 0x10000) { u[k++] = (char)(0xe0 | cp >> 12); u[k++] = (char)(0x80 | (cp >> 6 & 0x3f)); u[k++] = (char)(0x80 | (cp & 0x3f)); }
                else { u[k++] = (char)(0xf0 | cp >> 18); u[k++] = (char)(0x80 | (cp >> 12 & 0x3f)); u[k++] = (char)(0x80 | (cp >> 6 & 0x3f)); u[k++] = (char)(0x80 | (cp & 0x3f)); }
                for (int i = 0; i < k && o + 1 < cap; i++) out[o++] = u[i];
                continue;
            }
            cp = c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 'b' ? '\b' : c == 'f' ? '\f' : (unsigned char)c;
        }
        if (o + 1 < cap) out[o++] = (char)cp;
    }
    out[o] = 0;
    return p;
}

/* The music segments of a segments/latest answer, newest first: artist, title, start/end (seconds from the start of
   the show) and whether it is marked now playing. -1 if the answer is not the feed. */
static int parse_segments(const char* body, Seg* out, int max) {
    int n = 0;
    if (!strstr(body, "\"data\"")) return -1;
    for (const char* seg = strstr(body, "\"segment_item\""); seg && n < max; ) {
        const char* next = strstr(seg + 1, "\"segment_item\"");
        size_t span = next ? (size_t)(next - seg) : strlen(seg);
        char chunk[4096];
        if (span >= sizeof chunk) span = sizeof chunk - 1;
        memcpy(chunk, seg, span); chunk[span] = 0;
        const char* a = strstr(chunk, "\"primary\":\"");
        const char* t = strstr(chunk, "\"secondary\":\"");
        const char* off = strstr(chunk, "\"offset\":{");
        if (strstr(chunk, "\"segment_type\":\"music\"") && a && t && off) {
            Seg* s = &out[n];
            json_string(a + 11, s->artist, sizeof s->artist);
            json_string(t + 13, s->title, sizeof s->title);
            const char* st = strstr(off, "\"start\":");
            const char* en = strstr(off, "\"end\":");
            s->start = st ? atoi(st + 8) : -1;
            s->end = en ? atoi(en + 6) : -1;
            s->now = strstr(chunk, "\"now_playing\":true") != NULL;
            if (s->start >= 0 && s->end > s->start) n++;
        }
        seg = next;
    }
    return n;
}

/* The current show's start time (unix ms) from the schedule, 0 if unknown; *end_ms gets its end. */
static LONG64 fetch_show(LONG64* end_ms) {
    char url[256]; DWORD st, er;
    snprintf(url, sizeof url, "https://rms.api.bbc.co.uk/v2/broadcasts/latest?service=%s&on_air=now", g_service);
    char* body = http_get(url, &st, &er);
    LONG64 start = 0;
    *end_ms = 0;
    if (body && st == 200) {
        const char* s = strstr(body, "\"start\":\"");
        const char* e = strstr(body, "\"end\":\"");
        if (s) start = iso_ms(s + 9);
        if (e) *end_ms = iso_ms(e + 7);
    }
    free(body);
    return start;
}

#define POLL_MS 20000

/* Sets the song to show (empty names = no song) if it differs from the current one; it shows at `due` (tick ms). */
static void want_song(const char* artist, const char* title, LONG64 due, const char* why) {
    EnterCriticalSection(&g_now_cs);
    int changed = strcmp(artist, g_song.artist) || strcmp(title, g_song.title);
    if (changed) {
        snprintf(g_song.artist, sizeof g_song.artist, "%s", artist);
        snprintf(g_song.title, sizeof g_song.title, "%s", title);
        g_song.gen++;
        g_song.due = due;
    }
    LeaveCriticalSection(&g_now_cs);
    LONG64 wait = due - (LONG64)GetTickCount64();
    if (changed && title[0]) say("song: %s - %s (%s; in the HUD in %.0f s)", artist, title, why, wait > 0 ? wait / 1000.0 : 0.0);
    else if (changed) say("song: none (%s)", why);
}

static DWORD WINAPI now_thread(LPVOID unused) {
    (void)unused;
    Seg segs[MAX_SEGS];
    int nsegs = 0, failing = 0, listening = 0;
    LONG64 next_poll = 0, tuned = 0, show_start = 0, show_end = 0;
    char feed_now[330] = "";                     /* fallback: the feed's now-playing song when it was last seen */
    for (;;) {
        Sleep(listening ? 500 : 100);
        /* the feed is asked as soon as the station has played for a moment, so the name is in the track before the
           radio's station popup reads it (at game start, waiting for a steady second plus 6 s left it saying "Live") */
        if (!heard_recently() || (LONG64)GetTickCount64() - g_heard_since < FEED_AFTER_MS) { listening = 0; continue; }
        LONG64 tick = (LONG64)GetTickCount64();
        if (!listening) { listening = 1; tuned = tick; next_poll = 0; }
        if (tick >= next_poll) {
            next_poll = tick + POLL_MS;
            if (!show_start || unix_ms() > show_end) show_start = fetch_show(&show_end);
            char url[256]; DWORD st, er;
            snprintf(url, sizeof url, "https://rms.api.bbc.co.uk/v2/services/%s/segments/latest?experience=domestic&offset=0&limit=%d",
                     g_service, MAX_SEGS);
            char* body = http_get(url, &st, &er);
            int n = body && st == 200 ? parse_segments(body, segs, MAX_SEGS) : -1;
            free(body);
            if (n < 0) {
                if (!failing) say("now playing: the BBC feed failed (HTTP %lu, error %lu); keeps trying every %d s", st, er, POLL_MS / 1000);
                failing = 1;
            } else {
                if (failing) say("now playing: the BBC feed answers again");
                failing = 0;
                nsegs = n;
            }
        }
        /* names go into the tracks at once; only a forced popup (song_popup) waits for the radio's station popup */
        LONG64 earliest = g_song_popup && tuned + FIRST_NAME_MS > tick ? tuned + FIRST_NAME_MS : tick;
        /* before the audio clock runs (ffmpeg starting), where the listener will be: the usual lag behind the
           broadcast */
        LONG64 ear = audio_wall_ms();
        if (!ear) ear = unix_ms() - 19200 - HLS_AVAILABLE_MS;
        if (show_start && nsegs) {
            /* the song covering the moment in the listener's ears; between songs, none; outside what the feed covers
               (before the oldest listed song or in the previous show), unchanged */
            LONG64 t = ear - g_name_delay_ms;
            if (t < show_start) continue;
            int found = -1, covered = 0;
            for (int i = 0; i < nsegs; i++) {
                LONG64 s = show_start + segs[i].start * 1000LL, e = show_start + segs[i].end * 1000LL;
                if (t >= s && t < e) found = i;
                if (t >= s) covered = 1;
            }
            if (found >= 0) want_song(segs[found].artist, segs[found].title, earliest, "in the listener's ears now");
            else if (covered) want_song("", "", earliest, "between songs in the listener's ears");
        } else if (nsegs) {
            /* no clock: the feed's now-playing song, after what is buffered here and the usual HLS delay */
            const Seg* now = NULL;
            for (int i = 0; i < nsegs && !now; i++) if (segs[i].now) now = &segs[i];
            char key[330];
            snprintf(key, sizeof key, "%s\n%s", now ? now->artist : "", now ? now->title : "");
            if (strcmp(key, feed_now)) {
                snprintf(feed_now, sizeof feed_now, "%s", key);
                LONG64 due = tick + (LONG64)(buffered_s() * 1000) + FALLBACK_DELAY_MS + g_name_delay_ms;
                want_song(now ? now->artist : "", now ? now->title : "", due > earliest ? due : earliest, "feed, no audio clock");
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------ audio hooks */
#define RVA_GETSOUNDINFO 0x1a98920u   /* FMOD::Studio::System::getSoundInfo(this, key, SOUND_INFO*) */
#define RVA_CREATESOUND  0x1997740u   /* FMOD::System::createSound(this, name, mode, exinfo, Sound**) */
#define RVA_GETSUBSOUND  0x199f510u   /* FMOD::Sound::getSubSound(this, idx, Sound**) */

#define FMOD_LOOP_OFF 0x1u
#define FMOD_LOOP_NORMAL 0x2u
#define FMOD_2D 0x8u
#define FMOD_OPENMEMORY_POINT 0x10000000u
#define EXINFO_SIZE 216
/* FMOD_CREATESOUNDEXINFO field offsets (FMOD 1.06). The file callbacks' signatures are the ones of the game's own
   file system (System::setFileSystem in AudioEngineFmod at game+0x1003840; callbacks game+0x1006030/6010/60e0/6120):
   open(name, unicode, *filesize, **handle, userdata), close(handle, userdata), read(handle, buf, size, *got, userdata),
   seek(handle, pos, userdata). */
#define EX_LENGTH 0x04
#define EX_NUMCHANNELS 0x0c
#define EX_FREQUENCY 0x10
#define EX_FORMAT 0x14
#define EX_DECODEBUFFERSIZE 0x18
#define EX_PCMREAD 0x38
#define EX_PCMSETPOS 0x40
#define EX_USERDATA 0x68
#define FMOD_OPENUSER 0x400u
#define FMOD_CREATESTREAM 0x80u
#define FMOD_SOUND_FORMAT_PCM16 2
#define RVA_SOUND_GETUSERDATA 0x199fbe0u   /* FMOD::Sound::getUserData(this, void**) */

typedef struct {
    const char* name_or_data;
    unsigned mode;
    unsigned pad_;                 /* FMOD_CREATESOUNDEXINFO is 8-aligned: exinfo starts at +0x10, not +0xC */
    unsigned char exinfo[EXINFO_SIZE];
    int subsoundIndex;
} StudioSoundInfo;
_Static_assert(__builtin_offsetof(StudioSoundInfo, exinfo) == 0x10, "exinfo offset");
_Static_assert(__builtin_offsetof(StudioSoundInfo, subsoundIndex) == 0xE8, "subsoundIndex offset");

typedef int (*GetSoundInfoFn)(void*, const char*, StudioSoundInfo*);
typedef int (*CreateSoundFn)(void*, const char*, unsigned, void*, void**);
typedef int (*GetSubSoundFn)(void*, int, void**);
static GetSoundInfoFn o_getSoundInfo;
static CreateSoundFn o_createSound;
static GetSubSoundFn o_getSubSound;

/* The Sound handles created for our tracks, so getSubSound can tell them from the game's own. */
#define MAX_OURS 64
static void* g_ours[MAX_OURS];
static CRITICAL_SECTION g_cs;
static int is_ours(void* s) {
    int r = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_OURS && !r; i++) r = g_ours[i] == s;
    LeaveCriticalSection(&g_cs);
    return r;
}
static void set_ours(void* s, int on) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < MAX_OURS; i++) if (g_ours[i] == s) g_ours[i] = NULL;
    if (on) for (int i = 0; i < MAX_OURS; i++) if (!g_ours[i]) { g_ours[i] = s; break; }
    LeaveCriticalSection(&g_cs);
}

static void learn_entry(const char* key);

/* The game resolves a track/stinger/DJ SoundName to bank content here. Ours become a live WAV file or silence. The
   game ORs FMOD_NONBLOCKING | FMOD_CREATESTREAM into the mode when it calls createSound. */
static int d_getSoundInfo(void* sys, const char* key, StudioSoundInfo* info) {
    if (key && !strncmp(key, LIVE_PREFIX, strlen(LIVE_PREFIX))) {
        learn_entry(key);
        /* a user stream fed from the ring (FH2 Universal Radio's parameters: no length limit, a small decode buffer,
           looping); this one becomes the newest stream, the only one the ring feeds. A reopen while the station is
           heard goes on from where the ring is; a tune-in after a while away starts at live (see the comment at RING). */
        int heard = heard_recently();
        StreamTag* tag = new_tag();
        memset(info, 0, sizeof *info);
        info->name_or_data = NULL;
        info->mode = FMOD_LOOP_NORMAL | FMOD_2D | FMOD_OPENUSER | FMOD_CREATESTREAM;
        unsigned char* x = info->exinfo;
        *(int*)x = EXINFO_SIZE;
        *(unsigned*)(x + EX_LENGTH) = 0xffffffffu;
        *(int*)(x + EX_NUMCHANNELS) = 2;
        *(int*)(x + EX_FREQUENCY) = RATE;
        *(int*)(x + EX_FORMAT) = FMOD_SOUND_FORMAT_PCM16;
        *(unsigned*)(x + EX_DECODEBUFFERSIZE) = DECODE_FRAMES;
        *(void**)(x + EX_PCMREAD) = (void*)pcm_read;
        *(void**)(x + EX_PCMSETPOS) = (void*)pcm_setpos;
        *(void**)(x + EX_USERDATA) = tag;
        InterlockedExchange64(&g_open_tick, (LONG64)GetTickCount64());
        InterlockedExchange(&g_reads, 0);
        say("%s opened as live stream %lld (%.1f s of audio buffered; %s)", key, tag->gen, buffered_s(),
            heard ? "goes on where the station is" : "starts at live once the station has played for 1 s");
        return 0;
    }
    if (key && !strncmp(key, SILENT_PREFIX, strlen(SILENT_PREFIX))) {
        memset(info, 0, sizeof *info);
        info->name_or_data = (const char*)g_silence;
        info->mode = FMOD_LOOP_OFF | FMOD_2D | FMOD_OPENMEMORY_POINT;
        *(int*)info->exinfo = EXINFO_SIZE;
        *(unsigned*)(info->exinfo + EX_LENGTH) = g_silence_size;
        return 0;
    }
    return o_getSoundInfo(sys, key, info);
}

static int d_createSound(void* sys, const char* name, unsigned mode, void* exinfo, void** out) {
    int live = exinfo && *(int*)exinfo >= EX_PCMREAD + 8 && *(void**)((unsigned char*)exinfo + EX_PCMREAD) == (void*)pcm_read;
    int silent = name && name == (const char*)g_silence;
    int r = o_createSound(sys, name, mode, exinfo, out);
    if (out && *out) set_ours(*out, (live || silent) && r == 0);
    if (live && r != 0) say("FMOD refused the live stream: createSound error %d (mode 0x%x)", r, mode);
    else if (silent && r != 0) say("FMOD refused the silent placeholder: createSound error %d (mode 0x%x)", r, mode);
    return r;
}

/* The radio starts a track where the station's clock says it is (RadioStreamFmod at game+0x1022180 calls
   EventInstance::setTimelinePosition with it), and reopens the track whenever the stream's position is more than 5 s
   away from that clock (game+0x1022ef0). So the position is left alone (forcing 0 made the radio reopen the track
   every 50 ms, 2026-11-10), and the tracks are 16 minutes long: the "/Master/Radio/Track" event cannot be seeked much
   past that (FH2 Universal Radio: the longest stock track is 977656 ms, 16 min is proven safe), and a station clock
   past it left the stream silent. */

/* The game asks for subsound 0 of the opened sound. A plain WAV has none (error 34), so it is its own subsound. */
static int d_getSubSound(void* snd, int idx, void** out) {
    int r = o_getSubSound(snd, idx, out);
    if (r != 0 && is_ours(snd)) {
        *out = snd;
        return 0;
    }
    return r;
}

/* ------------------------------------------------------- song names in the HUD */
/* The HUD shows the playing track's DisplayName and Artist from its parsed <Entry> (the parser game+0x101cd30 fills
   SoundName +0x00, EventName +0x20, DisplayName +0x40, Artist +0x60 as MSVC std::strings, SampleLength +0x8c). The
   radio opens a track with getSoundInfo(entry's SoundName chars), and a short SoundName lives inside the string, so
   the key pointer is the entry itself. Song names are written into both live entries with the game's own
   std::string::assign (its allocator), on the game's thread, from the radio's per-frame track clock
   (game+0x1025070: position += dt; position past SampleLength stops the clock). The station's update
   (game+0x1024fc0) runs the clocks, then a state machine: in state 1 (a track plays, game+0x1027ec0) the track moves
   on to the stinger when its position reaches the entry's StingerStart (or DJStart) marker. So when a new song
   reaches the listener, the playing entry's StingerStart and DJStart are lowered to the track's position for half a
   second: the radio plays the stinger and starts the next live track, which carries the new names, the way it shows
   any new song. (Stopping the clock with SampleLength 0 left the station between tracks for good; moving the position
   to StingerStart made the radio reopen the track, which is more than 5 s off its clock, on top of the stinger, and
   playback stopped a few seconds later. Both 2026-11-10.) */
#define RVA_STRING_ASSIGN 0x22c300u
#define RVA_TRACK_CLOCK 0x1025070u
#define ENTRY_EVENTNAME 0x20
#define ENTRY_DISPLAYNAME 0x40
#define ENTRY_ARTIST 0x60
#define ENTRY_LENGTH 0x8c
#define ENTRY_MARKERS 0x90             /* -> int[8]: 0, TrackStart, TrackDrop, DJDrop, TrackBreakDown, StingerStart, DJStart, End */
#define MARKER_STINGERSTART 0x14
#define MARKER_DJSTART 0x18
#define PLAYSTATE_SAMPLE 0x08          /* the track's sample object; the sample keeps its entry at +8 (accessor game+0x5584e0) */
#define PLAYSTATE_POSITION 0x18        /* float, ms */
#define PLAYSTATE_PLAYING 0x1c
#define DEFAULT_TITLE "Live"           /* what the installer writes into the XML */
#define DEFAULT_ARTIST STATION_NAME
#define TITLE_MAX 56
#define ARTIST_MAX 48
typedef void* (*AssignFn)(void*, const char*, size_t);
typedef void (*TrackClockFn)(void*, float);
static AssignFn g_assign;
static TrackClockFn o_trackClock;
#define MAX_ENTRIES 4                  /* A and B, in FreeroamTracks and in EventTracks */
static unsigned char* g_entry[MAX_ENTRIES];   /* the parsed entries of the live tracks, learned when the radio opens them */
static LONG g_shown_gen;               /* the song generation written into the entries (game thread only) */
static char g_shown_title[160], g_shown_artist[160];   /* and its names, for entries learned later (game thread only) */
/* the entry whose StingerStart/DJStart are lowered for a song change, and their real values (game thread only) */
static unsigned char* g_lowered;
static unsigned g_lowered_stinger, g_lowered_dj;
static LONG64 g_lowered_tick;

static void restore_markers(void) {
    if (!g_lowered) return;
    unsigned char* m = *(unsigned char**)(g_lowered + ENTRY_MARKERS);
    if (m) {
        *(unsigned*)(m + MARKER_STINGERSTART) = g_lowered_stinger;
        *(unsigned*)(m + MARKER_DJSTART) = g_lowered_dj;
    }
    g_lowered = NULL;
}

static int is_learned(const unsigned char* p) {
    for (int i = 0; i < MAX_ENTRIES; i++) if (p && g_entry[i] == p) return 1;
    return 0;
}

/* A live track's entry: SoundName inline (short string) starting with LIVE_PREFIX, EventName the radio track event. */
static int is_live_entry(const unsigned char* p) {
    return p && *(const size_t*)(p + 0x18) == 15 && !strncmp((const char*)p, LIVE_PREFIX, strlen(LIVE_PREFIX)) &&
           string_is(p + ENTRY_EVENTNAME, "/Master/Radio/Track");
}

static void learn_entry(const char* key) {
    const unsigned char* p = (const unsigned char*)key;
    size_t n = strlen(key);
    if (n > 15 || *(const size_t*)(p + 0x10) != n || *(const size_t*)(p + 0x18) != 15 || !is_live_entry(p)) {
        say("%s: the key is not inside its track entry; song names stay off for this track", key);
        return;
    }
    if (g_lowered && (LONG64)GetTickCount64() - g_lowered_tick > 500) restore_markers();
    if (is_learned(p)) return;
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (g_entry[i]) continue;
        g_entry[i] = (unsigned char*)p;
        /* an entry first opened after a song was shown gets it now: at game start the first name went into the one
           live entry known then, and the radio went on to the other one, which still said "Live" (2026-11-10) */
        if (g_shown_gen && g_assign) {
            g_assign(g_entry[i] + ENTRY_DISPLAYNAME, g_shown_title, strlen(g_shown_title));
            g_assign(g_entry[i] + ENTRY_ARTIST, g_shown_artist, strlen(g_shown_artist));
        }
        say("%s: track entry found, song names on%s", key, g_shown_gen ? " (the current song written into it)" : "");
        return;
    }
}

/* How many bytes of s fit in max without cutting a UTF-8 character. */
static size_t utf8_fit(const char* s, size_t max) {
    size_t n = strlen(s);
    if (n <= max) return n;
    while (max && ((unsigned char)s[max] & 0xc0) == 0x80) max--;
    return max;
}

static void d_trackClock(void* state, float dt) {
    unsigned char* entry = NULL;
    if (state) {
        unsigned char* sample = *(unsigned char**)((unsigned char*)state + PLAYSTATE_SAMPLE);
        if (sample) entry = *(unsigned char**)(sample + 8);
    }
    if (!entry || !g_assign || !is_learned(entry)) { o_trackClock(state, dt); return; }
    if (g_lowered && (LONG64)GetTickCount64() - g_lowered_tick > 500) restore_markers();
    Song want; int due = 0;
    EnterCriticalSection(&g_now_cs);
    if (g_song.gen != g_shown_gen && (LONG64)GetTickCount64() >= g_song.due) { want = g_song; due = 1; }
    LeaveCriticalSection(&g_now_cs);
    if (!due) { o_trackClock(state, dt); return; }
    g_shown_gen = want.gen;
    const char* title = want.title[0] ? want.title : DEFAULT_TITLE;
    const char* artist = want.title[0] ? want.artist : DEFAULT_ARTIST;
    snprintf(g_shown_title, sizeof g_shown_title, "%.*s", (int)utf8_fit(title, TITLE_MAX), title);
    snprintf(g_shown_artist, sizeof g_shown_artist, "%.*s", (int)utf8_fit(artist, ARTIST_MAX), artist);
    for (int i = 0; i < MAX_ENTRIES; i++) {
        if (!g_entry[i] || !is_live_entry(g_entry[i])) continue;
        g_assign(g_entry[i] + ENTRY_DISPLAYNAME, title, utf8_fit(title, TITLE_MAX));
        g_assign(g_entry[i] + ENTRY_ARTIST, artist, utf8_fit(artist, ARTIST_MAX));
    }
    /* song_popup: a new song while the station is heard: the track reaches its stinger now, so the radio shows the
       next one */
    unsigned char* markers = *(unsigned char**)(entry + ENTRY_MARKERS);
    float pos = *(float*)((unsigned char*)state + PLAYSTATE_POSITION);
    if (g_song_popup && want.title[0] && heard_recently() && *((unsigned char*)state + PLAYSTATE_PLAYING) && markers && pos >= 1.0f &&
        pos < (float)*(unsigned*)(markers + MARKER_STINGERSTART)) {
        restore_markers();
        g_lowered = entry;
        g_lowered_stinger = *(unsigned*)(markers + MARKER_STINGERSTART);
        g_lowered_dj = *(unsigned*)(markers + MARKER_DJSTART);
        g_lowered_tick = (LONG64)GetTickCount64();
        *(unsigned*)(markers + MARKER_STINGERSTART) = (unsigned)pos;
        *(unsigned*)(markers + MARKER_DJSTART) = (unsigned)pos;
        say("HUD: %s - %s (the track reaches its stinger now so the radio shows it)", artist, title);
    } else {
        say("HUD: %s - %s", artist, title);
    }
    o_trackClock(state, dt);
}

static DWORD WINAPI hook_thread(LPVOID unused) {
    (void)unused;
    unsigned char* base = (unsigned char*)A->wait_for_code(300000);   /* the game is unpacked/relocated at runtime */
    if (!base) { say("hooks not installed: wait_for_code timed out after 300 s"); return 1; }
    g_assign = (AssignFn)(base + RVA_STRING_ASSIGN);
    g_getUserData = (GetUserDataFn)(base + RVA_SOUND_GETUSERDATA);
    FH2Hook* h[5] = {
        A->hook_function(M, "Studio::System::getSoundInfo", base + RVA_GETSOUNDINFO, d_getSoundInfo, (void**)&o_getSoundInfo),
        A->hook_function(M, "System::createSound", base + RVA_CREATESOUND, d_createSound, (void**)&o_createSound),
        A->hook_function(M, "Sound::getSubSound", base + RVA_GETSUBSOUND, d_getSubSound, (void**)&o_getSubSound),
        A->hook_function(M, "RadioModule::IsStationUnlocked", base + RVA_ISUNLOCKED, d_isUnlocked, (void**)&o_isUnlocked),
        A->hook_function(M, "Radio track clock", base + RVA_TRACK_CLOCK, d_trackClock, (void**)&o_trackClock),
    };
    if (!h[0] || !h[1] || !h[2] || !h[3]) { say("hooks not installed: hook creation failed, the game build does not match 1.0.0.64"); return 1; }
    if (!h[4]) say("song names stay off: the radio's track clock could not be hooked");
    say("hooks enabled: %d", A->enable_hooks(M, NULL));
    return 0;
}

/* ------------------------------------------------------------------- init */
static void read_config(void) {
    wchar_t ini[MAX_PATH]; swprintf(ini, MAX_PATH, L"%ls\\stream.ini", g_dir);
    char path[MAX_PATH * 3]; WideCharToMultiByte(CP_UTF8, 0, ini, -1, path, sizeof path, NULL, NULL);
    GetPrivateProfileStringA("stream", "url", DEFAULT_URL, g_url, sizeof g_url, path);
    GetPrivateProfileStringA("stream", "service", DEFAULT_SERVICE, g_service, sizeof g_service, path);
    char delay[32];
    GetPrivateProfileStringA("stream", "name_delay", "0", delay, sizeof delay, path);
    g_name_delay_ms = atoi(delay) * 1000;
    g_song_popup = (int)GetPrivateProfileIntA("stream", "song_popup", 0, path);
    if (!g_url[0]) snprintf(g_url, sizeof g_url, "%s", DEFAULT_URL);
    if (!g_service[0]) snprintf(g_service, sizeof g_service, "%s", DEFAULT_SERVICE);
}

FH2MOD_EXPORT int fh2mod_init(const FH2ModApi* api) {
    A = api;
    FH2ModInfo info = {0};
    info.size = sizeof info;
    info.name = "BBC Radio 1 Dance";
    info.version = MOD_VERSION;
    info.platform_version = FH2MOD_PLATFORM_VERSION;
    info.description = "Adds BBC Radio 1 Dance to the radio, live.";
    info.offline_only = 0;
    M = api->register_mod(&info);
    if (!M) return 0;
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_ring_cs);
    InitializeCriticalSection(&g_now_cs);

    swprintf(g_dir, MAX_PATH, L"%ls\\%ls", api->azimuth_dir, MOD_FOLDER);
    read_config();
    wchar_t od[MAX_PATH]; swprintf(od, MAX_PATH, L"%ls\\overrides", g_dir);
    int n = load_overrides(od, "");
    if (!n) { say("not active: nothing installed in %ls (run the installer)", g_dir); return 0; }
    wchar_t ff[MAX_PATH]; swprintf(ff, MAX_PATH, L"%ls\\tools\\ffmpeg.exe", g_dir);
    int have_ff = GetFileAttributesW(ff) != INVALID_FILE_ATTRIBUTES;
    say("%d overrides loaded; stream %s; feed %s; ffmpeg %s", n, g_url, g_service, have_ff ? "found" : "MISSING (the station stays silent; run the installer)");

    g_ring = (unsigned char*)malloc(RING);
    g_silence_size = 44 + BPS;
    g_silence = (unsigned char*)calloc(1, g_silence_size);   /* kept for the life of the process (FMOD_OPENMEMORY_POINT) */
    if (g_silence) wav_header(g_silence, BPS);
    g_wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_http = WinHttpOpen(L"FH2-Radio1Dance/" MOD_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (g_http) WinHttpSetTimeouts(g_http, 10000, 10000, 10000, 10000);
    else say("song names and the audio clock are off: WinHttpOpen failed (error %lu)", GetLastError());
    g_job = CreateJobObjectW(NULL, NULL);
    if (g_job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; memset(&li, 0, sizeof li);
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &li, sizeof li)) { CloseHandle(g_job); g_job = NULL; }
    }
    if (!g_ring || !g_wake || !g_silence) { say("not active: out of memory"); return 0; }
    CreateThread(NULL, 0, hook_thread, NULL, 0, NULL);
    CreateThread(NULL, 0, stream_thread, NULL, 0, NULL);
    CreateThread(NULL, 0, now_thread, NULL, 0, NULL);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) { (void)h; (void)reason; (void)r; return TRUE; }
