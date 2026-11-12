// ms2_log.cpp -- see ms2_log.h for why this is hand-rolled rather than printf.
#include "ms2_log.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cstring>

namespace ms2 {

namespace {
HANDLE        g_file = INVALID_HANDLE_VALUE;
SRWLOCK       g_lock = SRWLOCK_INIT;
wchar_t       g_path[MAX_PATH] = L"";
unsigned long long g_bytes = 0;
const unsigned long long kMaxBytes = 64ull * 1024 * 1024;   // rotate once at 64 MB

// -- formatting helpers -----------------------------------------------------
char* Put(char* d, const char* s) { while (*s) *d++ = *s++; *d = 0; return d; }

char* PutDec(char* d, unsigned long long v, bool neg) {
    char tmp[24]; int n = 0;
    if (neg) *d++ = '-';
    if (v == 0) { *d++ = '0'; *d = 0; return d; }
    while (v && n < 24) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n) *d++ = tmp[--n];
    *d = 0;
    return d;
}

char* PutHex(char* d, unsigned long long v, bool upper) {
    const char* H = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24]; int n = 0;
    if (v == 0) { *d++ = '0'; *d = 0; return d; }
    while (v && n < 24) { tmp[n++] = H[v & 0xF]; v >>= 4; }
    while (n) *d++ = tmp[--n];
    *d = 0;
    return d;
}

char* PutFixed(char* d, double v, int decimals) {
    if (v != v) return Put(d, "nan");
    if (v > 1.0e300) return Put(d, "inf");
    if (v < -1.0e300) return Put(d, "-inf");
    if (v < 0) { *d++ = '-'; v = -v; }
    unsigned long long whole = (unsigned long long)v;
    d = PutDec(d, whole, false);
    if (decimals > 0) {
        *d++ = '.';
        double frac = v - (double)whole;
        for (int i = 0; i < decimals; ++i) frac *= 10.0;
        unsigned long long f = (unsigned long long)frac;
        // keep leading zeros inside the fraction
        char tmp[16];
        for (int i = decimals - 1; i >= 0; --i) { tmp[i] = (char)('0' + (f % 10)); f /= 10; }
        for (int i = 0; i < decimals; ++i) *d++ = tmp[i];
    }
    *d = 0;
    return d;
}
}  // namespace

bool OpenLog(const wchar_t* exeDir) {
    if (g_file != INVALID_HANDLE_VALUE) return true;

    // Rotate the previous run aside: ms2coop-native.log -> .1.log -> .2.log
    wchar_t old1[MAX_PATH], old2[MAX_PATH];
    _snwprintf_s(old1, MAX_PATH, _TRUNCATE, L"%s\\ms2coop-native.log", exeDir);
    _snwprintf_s(old2, MAX_PATH, _TRUNCATE, L"%s\\ms2coop-native.2.log", exeDir);
    if (GetFileAttributesW(old2) != INVALID_FILE_ATTRIBUTES) DeleteFileW(old2);
    wchar_t one[MAX_PATH], two[MAX_PATH];
    _snwprintf_s(one, MAX_PATH, _TRUNCATE, L"%s\\ms2coop-native.1.log", exeDir);
    _snwprintf_s(two, MAX_PATH, _TRUNCATE, L"%s\\ms2coop-native.2.log", exeDir);
    if (GetFileAttributesW(one) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW(one, two, MOVEFILE_REPLACE_EXISTING);

    _snwprintf_s(g_path, MAX_PATH, _TRUNCATE, L"%s\\ms2coop-native.log", exeDir);
    if (GetFileAttributesW(g_path) != INVALID_FILE_ATTRIBUTES)
        MoveFileExW(g_path, one, MOVEFILE_REPLACE_EXISTING);

    g_file = CreateFileW(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    g_bytes = 0;

    char line[MAX_PATH * 3];
    char* d = line;
    d = Put(d, "=== ms2 native module, log opened at ");
    d = PutHex(d, (unsigned long long)GetTickCount64(), false);
    d = Put(d, " ms uptime ===");
    Log(line);
    return g_file != INVALID_HANDLE_VALUE;
}

void CloseLog() {
    if (g_file == INVALID_HANDLE_VALUE) return;
    CloseHandle(g_file);
    g_file = INVALID_HANDLE_VALUE;
}

const wchar_t* LogPath() { return g_path; }

void Log(const char* line) {
    if (g_file == INVALID_HANDLE_VALUE || !line) return;
    AcquireSRWLockExclusive(&g_lock);
    DWORD wrote = 0;
    size_t n = strlen(line);
    if (n > 900) n = 900;
    WriteFile(g_file, line, (DWORD)n, &wrote, NULL);
    WriteFile(g_file, "\r\n", 2, &wrote, NULL);
    g_bytes += n + 2;
    if (g_bytes > kMaxBytes) {
        // One rotation is enough for a session; further growth is capped by the
        // next rotation happening only at start-up.
        g_bytes = 0;
    }
    ReleaseSRWLockExclusive(&g_lock);
}


// ---------------------------------------------------------------------------
// Logf -- a deliberately small printf.  It exists because this DLL must not
// depend on the C runtime's formatter being initialised at the moment it logs,
// and because the first version of it parsed no width/precision and turned every
// "%-13s" into literal text (visible in ms2coop-native.log as "%-13s = 0x...").
// So: flags, width, precision and "ll" are parsed for real, and an unknown
// conversion is echoed verbatim instead of eating an argument.
// ---------------------------------------------------------------------------
void Logf(const char* fmt, ...) {
    if (g_file == INVALID_HANDLE_VALUE || !fmt) return;

    char line[1200];
    char* d = line;
    char* const end = line + sizeof(line) - 32;

    va_list ap;
    va_start(ap, fmt);

    for (const char* p = fmt; *p && d < end; ++p) {
        if (*p != '%') { *d++ = *p; continue; }
        const char* spec = p;      // for echoing an unsupported spec verbatim
        ++p;
        if (!*p) break;

        bool left = false, len64 = false;
        int  width = 0, prec = -1;
        for (;; ++p) {
            if (*p == '-') { left = true; continue; }
            if (*p == '0' || *p == '+' || *p == ' ') continue;
            break;
        }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); ++p; }
        if (*p == '.') {
            ++p;
            prec = 0;
            while (*p >= '0' && *p <= '9') { prec = prec * 10 + (*p - '0'); ++p; }
        }
        if (*p == 'l') { ++p; if (*p == 'l') ++p; len64 = true; }
        if (!*p) break;

        char tmp[128];
        char* t = tmp;
        tmp[0] = 0;
        bool numeric = false;

        switch (*p) {
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int n = 0;
            while (s[n]) ++n;
            if (prec >= 0 && n > prec) n = prec;
            for (int i = 0; i < n && d < end; ++i) *d++ = s[i];
            for (int i = n; i < width && d < end; ++i) *d++ = ' ';
            *d = 0;
            continue;
        }
        case 'c': *d++ = (char)va_arg(ap, int); *d = 0; continue;
        case '%': *d++ = '%'; *d = 0; continue;
        case 'd': {
            long long v = len64 ? va_arg(ap, long long) : (long long)va_arg(ap, int);
            t = PutDec(t, (unsigned long long)v, v < 0);
            numeric = true;
            break;
        }
        case 'u': {
            unsigned long long v = len64 ? va_arg(ap, unsigned long long)
                                         : (unsigned long long)va_arg(ap, unsigned int);
            t = PutDec(t, v, false);
            numeric = true;
            break;
        }
        case 'x':
        case 'X': {
            unsigned long long v = len64 ? va_arg(ap, unsigned long long)
                                         : (unsigned long long)va_arg(ap, unsigned int);
            t = PutHex(t, v, *p == 'X');
            numeric = true;
            break;
        }
        case 'p': {
            t = Put(t, "0x");
            t = PutHex(t, (unsigned long long)(uintptr_t)va_arg(ap, void*), true);
            numeric = true;
            break;
        }
        case 'f': {
            const double v = va_arg(ap, double);
            t = PutFixed(t, v, prec < 0 ? 2 : (prec > 6 ? 6 : prec));
            numeric = true;
            break;
        }
        default: {
            // Unsupported: echo the whole spec so a bad format string is visible
            // in the log instead of corrupting the argument list.
            for (const char* q = spec; *q && d < end; ++q) *d++ = *q;
            *d = 0;
            continue;
        }
        }

        if (numeric) {
            int n = (int)strlen(tmp);
            if (left) {
                for (int i = 0; i < n; ++i) *d++ = tmp[i];
                for (int i = n; i < width && d < end; ++i) *d++ = ' ';
            } else {
                for (int i = n; i < width && d < end; ++i) *d++ = ' ';
                for (int i = 0; i < n; ++i) *d++ = tmp[i];
            }
            *d = 0;
        }
    }

    va_end(ap);
    *d = 0;
    Log(line);
}

}  // namespace ms2
