#include "pota_guard.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

static void expect(int cond, const char* msg) {
    if (!cond) {
        printf("FAIL %s\n", msg);
        fails++;
    }
}

static void remember(PotaGuard* g, uint8_t program, const char* call, const char* ref,
                     const char* freq, const char* mode, uint32_t now, const char* node = "") {
    potaGuardNoteTime(g, now);
    expect(potaGuardLimit(g, program, call, ref, freq, mode, node, now) == POTA_LIMIT_OK, "remember precondition");
    potaGuardRemember(g, program, call, ref, freq, mode, node, now);
}

int main() {
    expect(potaCallOk("W1AW"), "W1AW");
    expect(potaCallOk("KC2G"), "KC2G");
    expect(potaCallOk("2E0SQL"), "2E0SQL");
    expect(potaCallOk("W1AW/P"), "W1AW/P");
    expect(potaCallOk("KH6/W1AW"), "KH6/W1AW");
    expect(potaCallOk("DL/W1AW/P"), "DL/W1AW/P");
    expect(!potaCallOk("SPOT"), "SPOT has no digit");
    expect(!potaCallOk("W1"), "too short");
    expect(!potaCallOk("W1AW/P/Q/R"), "three slashes");
    expect(!potaCallOk("/W1AW"), "leading slash");

    char base[16];
    expect(potaBaseCall("W1AW/P", base, sizeof(base)) && strcmp(base, "W1AW") == 0, "base portable");
    expect(potaBaseCall("KH6/W1AW", base, sizeof(base)) && strcmp(base, "W1AW") == 0, "base prefix");
    expect(potaBaseCall("DL/W1AW", base, sizeof(base)) && strcmp(base, "W1AW") == 0, "base dx prefix");

    expect(potaRefPota("US-1234"), "US-1234");
    expect(potaRefPota("US-12345"), "US-12345");
    expect(potaRefPota("F-0001"), "F-0001");
    expect(!potaRefPota("US-123"), "short pota");
    expect(!potaRefPota("US-1234,US-5678"), "2fer");
    expect(!potaRefPota("HELLO"), "word");
    expect(potaRefWwff("KFF-0001"), "KFF");
    expect(potaRefWwff("VKFF-1234"), "VKFF");
    expect(potaRefWwff("ONFF-0001"), "ONFF");
    expect(!potaRefWwff("US-1234"), "pota is not wwff");
    expect(potaRefSota("W1/GM-001"), "sota");
    expect(potaRefSota("GM/SS-123"), "sota gm");
    expect(!potaRefSota("US-1234"), "pota is not sota");
    expect(!potaShapeWhy(POTA_PROG_POTA, "W1AW", "US-1234", "14285", "SSB"), "good pota shape");
    expect(potaShapeWhy(POTA_PROG_POTA, "W1AW", "NOPE", "14285", "SSB") != 0, "bad ref");
    expect(potaFreqOk("14285"), "hf khz");
    expect(potaFreqOk("146520"), "2m khz");
    expect(potaFreqOk("472"), "630m");
    expect(!potaFreqOk("12"), "too low");
    expect(!potaFreqOk("14.285"), "still has a dot");
    expect(!potaFreqOk("9999999"), "too high");
    expect(potaModeOk("SSB") && potaModeOk("FT8") && potaModeOk("CW"), "modes");
    expect(!potaModeOk("S") && !potaModeOk("SSB/CW"), "bad modes");

    PotaGuard g;
    memset(&g, 0, sizeof(g));
    expect(potaBlockAdd(&g, "W1AW") == 0, "block add");
    expect(potaBlocked(&g, "W1AW"), "blocked exact");
    expect(potaBlocked(&g, "W1AW/P"), "blocked portable");
    expect(potaBlocked(&g, "KH6/W1AW"), "blocked prefix");
    expect(!potaBlocked(&g, "N0CALL"), "other call");
    expect(potaBlockAdd(&g, "W1AW") == 3, "dup block");
    expect(potaBlockRemove(&g, "W1AW") == 0, "unblock");
    expect(!potaBlocked(&g, "W1AW/P"), "cleared");

    char csv[64];
    potaBlockAdd(&g, "W1AW");
    potaBlockAdd(&g, "N0CALL");
    potaBlockCsv(&g, csv, sizeof(csv));
    PotaGuard loaded;
    memset(&loaded, 0, sizeof(loaded));
    potaBlockLoadCsv(&loaded, csv);
    expect(loaded.blockCount == 2 && potaBlocked(&loaded, "N0CALL"), "csv roundtrip");
    potaBlockLoadCsv(&loaded, "BAD,W1AW,123");
    expect(loaded.blockCount == 1 && potaBlocked(&loaded, "W1AW"), "csv skips junk");

    memset(&g, 0, sizeof(g));
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", 1000);
    potaGuardNoteTime(&g, 2000);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", "", 2000) == POTA_LIMIT_DUP,
           "dup inside 5 min");
    potaGuardNoteTime(&g, 1000 + POTA_GUARD_DUP_MS);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", "",
                          1000 + POTA_GUARD_DUP_MS) == POTA_LIMIT_OK,
           "dup expired");

    memset(&g, 0, sizeof(g));
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", 0);
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0002", "14285", "SSB", 1000);
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0003", "7030", "CW", 2000);
    potaGuardNoteTime(&g, 3000);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W1AW", "US-0004", "14074", "FT8", "", 3000) == POTA_LIMIT_CALL,
           "4th in 10 min");
    uint32_t later = POTA_GUARD_CALL_MS;
    potaGuardNoteTime(&g, later);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W1AW", "US-0004", "14074", "FT8", "", later) == POTA_LIMIT_OK,
           "call window slid");

    memset(&g, 0, sizeof(g));
    for (int i = 0; i < POTA_GUARD_HOUR_MAX; i++) {
        char call[16], ref[20];
        snprintf(call, sizeof(call), "W1%03d", i);
        snprintf(ref, sizeof(ref), "US-%04d", i + 1);
        remember(&g, POTA_PROG_POTA, call, ref, "14285", "SSB", (uint32_t)(i * 1000));
    }
    potaGuardNoteTime(&g, 30000);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "N0CALL", "US-9999", "14285", "SSB", "", 30000) == POTA_LIMIT_HOUR,
           "21st in the hour");
    uint32_t hour = POTA_GUARD_HOUR_MS;
    potaGuardNoteTime(&g, hour);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "N0CALL", "US-9999", "14285", "SSB", "", hour) == POTA_LIMIT_OK,
           "hour window slid");

    memset(&g, 0, sizeof(g));
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", 50);
    g.lastMs = 100;
    potaGuardNoteTime(&g, 10);
    expect(g.count == 0, "millis wrap clears history");

    char nodeId[POTA_NODE_ID_LEN];
    uint8_t pubA[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    uint8_t pubB[8] = {0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
    potaNodeFromPub(nodeId, sizeof(nodeId), pubA, sizeof(pubA));
    expect(strcmp(nodeId, "0123456789ABCDEF") == 0, "node hex");
    char nodeB[POTA_NODE_ID_LEN];
    potaNodeFromPub(nodeB, sizeof(nodeB), pubB, sizeof(pubB));
    expect(strcmp(nodeB, "FEDCBA9876543210") == 0, "other node hex");
    potaNodeFromPub(nodeId, sizeof(nodeId), 0, 0);
    expect(nodeId[0] == 0, "empty node");

    memset(&g, 0, sizeof(g));
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", 0, "NODEA");
    remember(&g, POTA_PROG_POTA, "K1ABC", "US-0002", "7030", "CW", 1000, "NODEA");
    remember(&g, POTA_PROG_POTA, "N0CALL", "US-0003", "14074", "FT8", 2000, "NODEA");
    potaGuardNoteTime(&g, 3000);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W2XYZ", "US-0004", "146520", "FM", "NODEA", 3000) ==
               POTA_LIMIT_NODE,
           "same node rotating callsigns");
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W2XYZ", "US-0004", "146520", "FM", "NODEB", 3000) ==
               POTA_LIMIT_OK,
           "other node still allowed");
    uint32_t nodeLater = POTA_GUARD_CALL_MS;
    potaGuardNoteTime(&g, nodeLater);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W2XYZ", "US-0004", "146520", "FM", "NODEA", nodeLater) ==
               POTA_LIMIT_OK,
           "node window slid");

    memset(&g, 0, sizeof(g));
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0001", "14285", "SSB", 0, "NODEA");
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0002", "7030", "CW", 1000, "NODEB");
    remember(&g, POTA_PROG_POTA, "W1AW", "US-0003", "14074", "FT8", 2000, "NODEA");
    potaGuardNoteTime(&g, 3000);
    expect(potaGuardLimit(&g, POTA_PROG_POTA, "W1AW", "US-0004", "146520", "FM", "NODEB", 3000) ==
               POTA_LIMIT_CALL,
           "call cap still shared across nodes");

    if (fails) {
        printf("%d failed\n", fails);
        return 1;
    }
    printf("ok\n");
    return 0;
}
