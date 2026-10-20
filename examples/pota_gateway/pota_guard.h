#pragma once

/*
 * Anti-abuse checks for the POTA gateway. No Arduino types: the firmware
 * and the host test both include this header.
 *
 * Callsigns and references must already be uppercase.
 * A blocked entry is a base call (no slash). W1AW/P and KH6/W1AW match W1AW.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    POTA_PROG_POTA = 1,
    POTA_PROG_WWFF = 2,
    POTA_PROG_SOTA = 3
};

enum {
    POTA_LIMIT_OK = 0,
    POTA_LIMIT_DUP = 1,
    POTA_LIMIT_CALL = 2,
    POTA_LIMIT_HOUR = 3,
    POTA_LIMIT_NODE = 4
};

/* 3 spots per activator callsign per 10 minutes, 3 per logged-in node per
 * 10 minutes, 20 spots per hour. Node id is hex of the first 8 pubkey bytes. */
#define POTA_GUARD_CALL_MAX 3
#define POTA_GUARD_CALL_MS (10UL * 60UL * 1000UL)
#define POTA_GUARD_HOUR_MAX 20
#define POTA_GUARD_HOUR_MS (60UL * 60UL * 1000UL)
#define POTA_GUARD_DUP_MS (5UL * 60UL * 1000UL)
#define POTA_GUARD_HIST 20
#define POTA_BLOCK_MAX 16
#define POTA_NODE_ID_BYTES 8
#define POTA_NODE_ID_LEN (POTA_NODE_ID_BYTES * 2 + 1)

typedef struct PotaGuardSpot {
    char call[16];
    char ref[20];
    char freq[16];
    char mode[12];
    char node[POTA_NODE_ID_LEN];
    uint8_t program;
    uint32_t ms;
} PotaGuardSpot;

typedef struct PotaGuard {
    PotaGuardSpot hist[POTA_GUARD_HIST];
    uint8_t next;
    uint8_t count;
    uint32_t lastMs;
    char blocks[POTA_BLOCK_MAX][16];
    uint8_t blockCount;
} PotaGuard;

static inline int potaIsDigit(char c) { return c >= '0' && c <= '9'; }
static inline int potaIsAlpha(char c) { return c >= 'A' && c <= 'Z'; }
static inline int potaIsAlnum(char c) { return potaIsDigit(c) || potaIsAlpha(c); }

static inline void potaCopy(char* dest, size_t destLen, const char* src) {
    size_t i = 0;
    if (!src) src = "";
    if (destLen == 0) return;
    while (src[i] && i + 1 < destLen) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

static inline void potaNodeFromPub(char* dest, size_t destLen, const uint8_t* pub, size_t pubLen) {
    if (!dest || destLen == 0) return;
    dest[0] = 0;
    if (!pub || pubLen == 0) return;
    size_t n = pubLen;
    if (n > POTA_NODE_ID_BYTES) n = POTA_NODE_ID_BYTES;
    if (destLen < n * 2 + 1) n = (destLen - 1) / 2;
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < n; i++) {
        dest[i * 2] = hex[pub[i] >> 4];
        dest[i * 2 + 1] = hex[pub[i] & 0x0F];
    }
    dest[n * 2] = 0;
}

static inline int potaCallOk(const char* s) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (n < 3 || n > 15) return 0;
    int letters = 0, digits = 0, slashes = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '/') {
            if (i == 0 || i + 1 == n || s[i - 1] == '/') return 0;
            slashes++;
            if (slashes > 2) return 0;
        } else if (potaIsAlpha(c)) {
            letters++;
        } else if (potaIsDigit(c)) {
            digits++;
        } else {
            return 0;
        }
    }
    return letters >= 1 && digits >= 1;
}

/* Longest slash-piece that is itself a callsign. W1AW/P -> W1AW. */
static inline int potaBaseCall(const char* call, char* out, size_t outLen) {
    if (!out || outLen < 4 || !potaCallOk(call)) return 0;
    if (!strchr(call, '/')) {
        potaCopy(out, outLen, call);
        return 1;
    }
    out[0] = 0;
    size_t bestN = 0;
    const char* p = call;
    while (*p) {
        const char* start = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - start);
        if (n >= 3 && n < 16 && n + 1 <= outLen) {
            char tmp[16];
            memcpy(tmp, start, n);
            tmp[n] = 0;
            if (potaCallOk(tmp) && n > bestN) {
                memcpy(out, tmp, n + 1);
                bestN = n;
            }
        }
        if (*p == '/') p++;
    }
    return bestN > 0;
}

static inline int potaTokenHas(const char* call, const char* piece) {
    if (!call || !piece || !piece[0]) return 0;
    size_t pn = strlen(piece);
    const char* p = call;
    while (*p) {
        const char* start = p;
        while (*p && *p != '/') p++;
        size_t n = (size_t)(p - start);
        if (n == pn && memcmp(start, piece, n) == 0) return 1;
        if (*p == '/') p++;
    }
    return 0;
}

static inline int potaRefPota(const char* s) {
    if (!s) return 0;
    size_t i = 0;
    while (s[i] && potaIsAlpha(s[i])) i++;
    if (i < 1 || i > 3 || s[i] != '-') return 0;
    i++;
    size_t d = 0;
    while (s[i] && potaIsDigit(s[i])) {
        d++;
        i++;
    }
    return s[i] == 0 && d >= 4 && d <= 5;
}

static inline int potaRefWwff(const char* s) {
    if (!s) return 0;
    const char* ff = strstr(s, "FF-");
    if (!ff || ff == s) return 0;
    size_t pre = (size_t)(ff - s);
    if (pre < 1 || pre > 5) return 0;
    for (size_t i = 0; i < pre; i++) {
        if (!potaIsAlnum(s[i])) return 0;
    }
    const char* d = ff + 3;
    size_t n = 0;
    while (d[n] && potaIsDigit(d[n])) n++;
    return d[n] == 0 && n == 4;
}

static inline int potaRefSota(const char* s) {
    if (!s) return 0;
    const char* sl = strchr(s, '/');
    if (!sl || sl == s) return 0;
    size_t pre = (size_t)(sl - s);
    if (pre < 1 || pre > 3) return 0;
    for (size_t i = 0; i < pre; i++) {
        if (!potaIsAlnum(s[i])) return 0;
    }
    const char* r = sl + 1;
    if (!potaIsAlnum(r[0]) || !potaIsAlnum(r[1]) || r[2] != '-') return 0;
    const char* d = r + 3;
    size_t n = 0;
    while (d[n] && potaIsDigit(d[n])) n++;
    return d[n] == 0 && n == 3;
}

static inline int potaRefOk(uint8_t program, const char* ref) {
    if (program == POTA_PROG_WWFF) return potaRefWwff(ref);
    if (program == POTA_PROG_SOTA) return potaRefSota(ref);
    return potaRefPota(ref);
}

/* Integer kilohertz, 100 kHz through 1300 MHz. */
static inline int potaFreqOk(const char* s) {
    if (!s || !s[0]) return 0;
    size_t n = 0;
    for (; s[n]; n++) {
        if (!potaIsDigit(s[n]) || n >= 7) return 0;
    }
    long k = 0;
    for (size_t i = 0; i < n; i++) k = k * 10 + (s[i] - '0');
    return k >= 100 && k <= 1300000;
}

static inline int potaModeOk(const char* s) {
    if (!s) return 0;
    size_t n = strlen(s);
    if (n < 2 || n > 8) return 0;
    for (size_t i = 0; i < n; i++) {
        if (!potaIsAlnum(s[i])) return 0;
    }
    return 1;
}

static inline const char* potaShapeWhy(uint8_t program, const char* call, const char* ref,
                                      const char* freq, const char* mode) {
    if (!potaCallOk(call)) return "call";
    if (!potaRefOk(program, ref)) return "ref";
    if (!potaFreqOk(freq)) return "freq";
    if (!potaModeOk(mode)) return "mode";
    return 0;
}

static inline int potaBlocked(const PotaGuard* g, const char* call) {
    if (!g || !call || !call[0]) return 0;
    for (uint8_t i = 0; i < g->blockCount; i++) {
        if (potaTokenHas(call, g->blocks[i])) return 1;
    }
    return 0;
}

static inline int potaBlockAdd(PotaGuard* g, const char* base) {
    if (!g || !potaCallOk(base) || strchr(base, '/') != 0) return 1;
    for (uint8_t i = 0; i < g->blockCount; i++) {
        if (strcmp(g->blocks[i], base) == 0) return 3;
    }
    if (g->blockCount >= POTA_BLOCK_MAX) return 2;
    potaCopy(g->blocks[g->blockCount], 16, base);
    g->blockCount++;
    return 0;
}

static inline int potaBlockRemove(PotaGuard* g, const char* base) {
    if (!g || !base) return 1;
    for (uint8_t i = 0; i < g->blockCount; i++) {
        if (strcmp(g->blocks[i], base) == 0) {
            for (uint8_t j = (uint8_t)(i + 1); j < g->blockCount; j++) {
                memcpy(g->blocks[j - 1], g->blocks[j], 16);
            }
            g->blockCount--;
            g->blocks[g->blockCount][0] = 0;
            return 0;
        }
    }
    return 1;
}

static inline void potaBlockLoadCsv(PotaGuard* g, const char* csv) {
    if (!g) return;
    g->blockCount = 0;
    if (!csv) return;
    while (*csv && g->blockCount < POTA_BLOCK_MAX) {
        while (*csv == ',' || *csv == ' ') csv++;
        if (!*csv) break;
        char tmp[16];
        size_t i = 0;
        while (*csv && *csv != ',' && i + 1 < sizeof(tmp)) tmp[i++] = (char)*csv++;
        tmp[i] = 0;
        while (*csv && *csv != ',') csv++;
        if (potaCallOk(tmp) && strchr(tmp, '/') == 0) {
            potaCopy(g->blocks[g->blockCount], 16, tmp);
            g->blockCount++;
        }
    }
}

static inline void potaBlockCsv(const PotaGuard* g, char* dest, size_t destLen) {
    if (!dest || destLen == 0) return;
    dest[0] = 0;
    if (!g) return;
    size_t used = 0;
    for (uint8_t i = 0; i < g->blockCount; i++) {
        size_t len = strlen(g->blocks[i]);
        size_t need = len + (i ? 1 : 0);
        if (used + need + 1 > destLen) break;
        if (i) dest[used++] = ',';
        memcpy(dest + used, g->blocks[i], len);
        used += len;
        dest[used] = 0;
    }
}

static inline void potaGuardNoteTime(PotaGuard* g, uint32_t now) {
    if (!g) return;
    if (g->lastMs && now < g->lastMs) {
        g->next = 0;
        g->count = 0;
    }
    g->lastMs = now;
}

static inline int potaFresh(uint32_t now, uint32_t then, uint32_t window) {
    return (uint32_t)(now - then) < window;
}

static inline uint8_t potaHistIndex(const PotaGuard* g, uint8_t k) {
    return (uint8_t)((g->next + POTA_GUARD_HIST - g->count + k) % POTA_GUARD_HIST);
}

static inline int potaGuardLimit(const PotaGuard* g, uint8_t program, const char* call,
                                 const char* ref, const char* freq, const char* mode,
                                 const char* node, uint32_t now) {
    if (!g) return POTA_LIMIT_OK;
    int callN = 0;
    int nodeN = 0;
    int hourN = 0;
    for (uint8_t k = 0; k < g->count; k++) {
        const PotaGuardSpot* s = &g->hist[potaHistIndex(g, k)];
        if (!potaFresh(now, s->ms, POTA_GUARD_HOUR_MS)) continue;
        hourN++;
        if (strcmp(s->call, call) == 0 && potaFresh(now, s->ms, POTA_GUARD_CALL_MS)) callN++;
        if (node && node[0] && s->node[0] && strcmp(s->node, node) == 0 &&
            potaFresh(now, s->ms, POTA_GUARD_CALL_MS)) {
            nodeN++;
        }
        if (s->program == program && strcmp(s->call, call) == 0 && strcmp(s->ref, ref) == 0 &&
            strcmp(s->freq, freq) == 0 && strcmp(s->mode, mode) == 0 &&
            potaFresh(now, s->ms, POTA_GUARD_DUP_MS)) {
            return POTA_LIMIT_DUP;
        }
    }
    if (callN >= POTA_GUARD_CALL_MAX) return POTA_LIMIT_CALL;
    if (node && node[0] && nodeN >= POTA_GUARD_CALL_MAX) return POTA_LIMIT_NODE;
    if (hourN >= POTA_GUARD_HOUR_MAX) return POTA_LIMIT_HOUR;
    return POTA_LIMIT_OK;
}

static inline void potaGuardRemember(PotaGuard* g, uint8_t program, const char* call, const char* ref,
                                    const char* freq, const char* mode, const char* node, uint32_t now) {
    if (!g) return;
    potaGuardNoteTime(g, now);
    PotaGuardSpot* s = &g->hist[g->next];
    potaCopy(s->call, sizeof(s->call), call);
    potaCopy(s->ref, sizeof(s->ref), ref);
    potaCopy(s->freq, sizeof(s->freq), freq);
    potaCopy(s->mode, sizeof(s->mode), mode);
    potaCopy(s->node, sizeof(s->node), node);
    s->program = program;
    s->ms = now;
    g->next = (uint8_t)((g->next + 1) % POTA_GUARD_HIST);
    if (g->count < POTA_GUARD_HIST) g->count++;
}
