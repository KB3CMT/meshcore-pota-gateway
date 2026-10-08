#ifdef WITH_POTA_GATEWAY

#include <Arduino.h>
#include "pota_spotter.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <cstdarg>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "pota_guard.h"

#define POTA_HOST "api.pota.app"
#define POTA_PATH "/spot"
#define PNP_HOST  "parksnpeaks.org"
#define PNP_PATH  "/api/SPOT"
#define PNP_KEY_MIN 8
#define POTA_QUEUE_LEN 6

enum SpotProgram : uint8_t {
    PROG_POTA = POTA_PROG_POTA,
    PROG_WWFF = POTA_PROG_WWFF,
    PROG_SOTA = POTA_PROG_SOTA
};

struct Pending {
    uint8_t program;
    char activator[16];
    char park[20];
    char freq[16];
    char mode[12];
    char spotter[16];
    char comments[96];
};

static WiFiManager wm;
static Preferences pnpStore;
static QueueHandle_t spotQueue = nullptr;
static TaskHandle_t httpTaskHandle = nullptr;
static unsigned long lastStaAttempt = 0;
static unsigned long lastReconnect = 0;
static bool portalStarted = false;
static bool staOnline = false;
static bool pnpHttpUp = false;
static volatile bool httpBusy = false;
static IPAddress cachedPotaIp;
static unsigned long cachedPotaAt = 0;

static char pnpUser[16];
static char pnpKey[48];
static WiFiManagerParameter* wmPnpUser = nullptr;
static WiFiManagerParameter* wmPnpKey = nullptr;
static WebServer pnpHttp(80);
static PotaGuard spotGuard;

static void logf(const char* fmt, ...) {
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.print(buf);
    if (buf[0] && buf[strlen(buf) - 1] != '\n') Serial.println();
    Serial.flush();
}

static void setPublicDns() {
    ip_addr_t primary, secondary;
    IP_ADDR4(&primary, 8, 8, 8, 8);
    IP_ADDR4(&secondary, 1, 1, 1, 1);
    dns_setserver(0, &primary);
    dns_setserver(1, &secondary);
}

static void bindStaDns() {
    setPublicDns();
    IPAddress gw = WiFi.gatewayIP();
    if ((uint32_t)gw != 0) {
        ip_addr_t g;
        IP_ADDR4(&g, gw[0], gw[1], gw[2], gw[3]);
        dns_setserver(1, &g);
    }
}

static void tearDownSoftAp() {
    if (wm.getConfigPortalActive()) {
        wm.stopConfigPortal();
    }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    portalStarted = false;
}

static bool parseDnsA(const char* body, IPAddress& out) {
    const char* ans = strstr(body, "\"Answer\"");
    if (!ans) ans = body;
    const char* d = strstr(ans, "\"data\":\"");
    if (!d) return false;
    int a, b, c, e;
    if (sscanf(d + 8, "%d.%d.%d.%d", &a, &b, &c, &e) != 4) return false;
    out = IPAddress((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)e);
    return (uint32_t)out != 0;
}

static bool httpDnsAt(IPAddress server, const char* hostHdr, IPAddress& out) {
    WiFiClient client;
    client.setTimeout(5000);
    if (!client.connect(server, 80)) {
        logf("[POTA] TCP %s:80 failed\n", server.toString().c_str());
        return false;
    }
    client.printf("GET /resolve?name=%s&type=A HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n",
                  POTA_HOST, hostHdr);
    unsigned long start = millis();
    char buf[896];
    size_t n = 0;
    while ((millis() - start) < 6000 && n + 1 < sizeof(buf)) {
        while (client.available() && n + 1 < sizeof(buf)) buf[n++] = (char)client.read();
        if (!client.connected() && !client.available()) break;
        delay(5);
    }
    buf[n] = 0;
    client.stop();
    return parseDnsA(buf, out);
}

static bool resolvePotaHost(IPAddress& ip) {
    if (cachedPotaAt && (millis() - cachedPotaAt) < 300000ul && (uint32_t)cachedPotaIp) {
        ip = cachedPotaIp;
        return true;
    }
    bindStaDns();
    if (WiFi.hostByName(POTA_HOST, ip) == 1 && (uint32_t)ip != 0) {
        cachedPotaIp = ip;
        cachedPotaAt = millis();
        logf("[POTA] DNS %s -> %s\n", POTA_HOST, ip.toString().c_str());
        return true;
    }
    logf("[POTA] hostByName failed (ip=%s gw=%s dns=%s rssi=%d)\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.gatewayIP().toString().c_str(),
                  WiFi.dnsIP().toString().c_str(),
                  WiFi.RSSI());
    if (httpDnsAt(IPAddress(8, 8, 8, 8), "dns.google", ip) ||
        httpDnsAt(IPAddress(8, 8, 4, 4), "dns.google", ip)) {
        cachedPotaIp = ip;
        cachedPotaAt = millis();
        logf("[POTA] Google HTTP DNS %s -> %s\n", POTA_HOST, ip.toString().c_str());
        return true;
    }
    logf("[POTA] no path to the internet (hotspot not routing UDP/TCP DNS)");
    return false;
}

static void copyField(char* dest, size_t destLen, const char* src, size_t srcLen) {
    if (srcLen >= destLen) srcLen = destLen - 1;
    memcpy(dest, src, srcLen);
    dest[srcLen] = 0;
}

static const char* skipWs(const char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static bool tokenEq(const char* s, const char* word, size_t n, const char** after) {
    if (strncasecmp(s, word, n) != 0) return false;
    char next = s[n];
    if (next && !isspace((unsigned char)next)) return false;
    *after = skipWs(s + n);
    return true;
}

static const char* token(const char* s, char* out, size_t outLen) {
    s = skipWs(s);
    const char* start = s;
    while (*s && !isspace((unsigned char)*s)) s++;
    copyField(out, outLen, start, (size_t)(s - start));
    return s;
}

static void jsonEscape(const char* src, char* dest, size_t destLen) {
    size_t j = 0;
    for (const unsigned char* p = (const unsigned char*)src; *p && j + 2 < destLen; ++p) {
        if (*p == '"' || *p == '\\') {
            if (j + 3 >= destLen) break;
            dest[j++] = '\\';
            dest[j++] = (char)*p;
        } else if (*p < 32) {
            dest[j++] = ' ';
        } else {
            dest[j++] = (char)*p;
        }
    }
    dest[j] = 0;
}

static void toUpperInPlace(char* s) {
    for (; *s; ++s) *s = (char)toupper((unsigned char)*s);
}

static void normalizePark(char* park) {
    toUpperInPlace(park);
    if (park[0] == 'K' && park[1] == '-' && isdigit((unsigned char)park[2])) {
        char tmp[20];
        snprintf(tmp, sizeof(tmp), "US-%s", park + 2);
        copyField(park, 20, tmp, strlen(tmp));
    }
}

static void normalizeFreq(char* freq) {
    /* A decimal is MHz (14.285, 146.52). A bare integer is already kHz. */
    if (strchr(freq, '.') == NULL) return;
    float f = atof(freq);
    if (f <= 0.0f || f >= 10000.0f) return;
    int khz = (int)(f * 1000.0f + 0.5f);
    if (khz > 0) snprintf(freq, 16, "%d", khz);
}

static void freqToMhz(const char* khz, char* mhz, size_t mhzLen) {
    int k = atoi(khz);
    if (k <= 0) {
        copyField(mhz, mhzLen, khz, strlen(khz));
        return;
    }
    snprintf(mhz, mhzLen, "%.3f", k / 1000.0f);
}

static const char* programName(uint8_t program) {
    switch (program) {
        case PROG_WWFF: return "WWFF";
        case PROG_SOTA: return "SOTA";
        default: return "POTA";
    }
}

static bool pnpUserLooksValid(const char* s) {
    if (!s) return false;
    size_t n = 0;
    unsigned char first = (unsigned char)s[0];
    if (!isalnum(first)) return false;
    for (const char* p = s; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '/' || c == '-' || c == '_')) return false;
        n++;
        if (n > 15) return false;
    }
    return n >= 3;
}

static bool pnpKeyLooksValid(const char* s) {
    if (!s) return false;
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        if (*p <= 32 || *p >= 127) return false;
        n++;
        if (n >= sizeof(pnpKey)) return false;
    }
    return n >= PNP_KEY_MIN;
}

static bool pnpEnabled() {
    return pnpUserLooksValid(pnpUser) && pnpKeyLooksValid(pnpKey);
}

static void pnpForgetPortalKey() {
    if (!wmPnpKey) return;
    char* v = (char*)wmPnpKey->getValue();
    if (v) {
        size_t n = strlen(v);
        if (n) memset(v, 0, n);
    }
    wmPnpKey->setValue("", (int)sizeof(pnpKey) - 1);
}

static void pnpClear() {
    pnpUser[0] = 0;
    pnpKey[0] = 0;
    pnpStore.begin("pnp", false);
    pnpStore.clear();
    pnpStore.end();
    pnpForgetPortalKey();
    logf("[PNP] disabled (no API key)");
}

static void pnpLoad() {
    pnpUser[0] = 0;
    pnpKey[0] = 0;
    pnpStore.begin("pnp", true);
    pnpStore.getString("user", pnpUser, sizeof(pnpUser));
    pnpStore.getString("key", pnpKey, sizeof(pnpKey));
    pnpStore.end();
    if (pnpEnabled()) {
        logf("[PNP] enabled user=%s\n", pnpUser);
    } else {
        pnpUser[0] = 0;
        pnpKey[0] = 0;
        logf("[PNP] off — set user + API key at http://<dhcp-ip>/ (or first Wi-Fi portal)");
    }
}

static void pnpSave(const char* user, const char* key) {
    copyField(pnpUser, sizeof(pnpUser), user, strlen(user));
    copyField(pnpKey, sizeof(pnpKey), key, strlen(key));
    toUpperInPlace(pnpUser);
    pnpStore.begin("pnp", false);
    pnpStore.putString("user", pnpUser);
    pnpStore.putString("key", pnpKey);
    pnpStore.end();
    pnpForgetPortalKey();
    logf("[PNP] saved — WWFF/SOTA spots will POST to parksnpeaks.org as %s\n", pnpUser);
}

static bool keyMeansOff(const char* k) {
    return k && (strcasecmp(k, "off") == 0 || strcmp(k, "-") == 0 || strcasecmp(k, "clear") == 0);
}

static void applyPnpForm(const char* user, const char* key) {
    if (!user) user = "";
    if (!key) key = "";

    if (keyMeansOff(key)) {
        pnpClear();
    } else if (key[0] == 0) {
        if (pnpEnabled() && pnpUserLooksValid(user) && strcasecmp(user, pnpUser) != 0) {
            pnpSave(user, pnpKey);
        } else {
            logf("[PNP] key unchanged; still %s", pnpEnabled() ? "on" : "off");
        }
    } else if (!pnpKeyLooksValid(key)) {
        logf("[PNP] API key rejected (need 8+ printable characters); PnP stays off");
    } else if (!pnpUserLooksValid(user)) {
        logf("[PNP] user ID rejected (use your ParksnPeaks callsign); PnP stays off");
    } else {
        pnpSave(user, key);
    }
    pnpForgetPortalKey();
}

static void onPortalSave() {
    if (!wmPnpUser || !wmPnpKey) return;
    applyPnpForm(wmPnpUser->getValue(), wmPnpKey->getValue());
}

static void handlePnpRoot() {
    char page[1500];
    String ip = WiFi.localIP().toString();
    snprintf(page, sizeof(page),
             "<!DOCTYPE html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
             "<title>MeshCore POTA gateway</title></head><body>"
             "<h1>MeshCore POTA gateway</h1>"
             "<p>DHCP <b>%s</b><br>POTA: api.pota.app<br>ParksnPeaks: <b>%s</b> user=%s<br>API key: <b>%s</b></p>"
             "<h2>ParksnPeaks API key</h2>"
             "<form method=post autocomplete=off>"
             "<p>User ID<br><input name=user value='%s' maxlength=15></p>"
             "<p>API key (leave blank to keep)<br>"
             "<input name=key type=password maxlength=47 value='' autocomplete=off></p>"
             "<button type=submit>Save</button>"
             "</form>"
             "<p>Blank key keeps the stored key. Type OFF to disable. This page does not change Wi-Fi "
             "and never shows the stored key.</p>"
             "</body></html>",
             ip.c_str(),
             pnpEnabled() ? "ON" : "OFF",
             pnpEnabled() ? pnpUser : "-",
             pnpKeyLooksValid(pnpKey) ? "set" : "not set",
             pnpUser[0] ? pnpUser : "");
    pnpHttp.send(200, "text/html", page);
    logf("[PNP] config page served http://%s/", ip.c_str());
}

static void handlePnpSave() {
    String user = pnpHttp.arg("user");
    String key = pnpHttp.arg("key");
    logf("[PNP] config form submitted user=%s key=%s",
         user.c_str(), key.length() ? "(set)" : "(blank)");
    applyPnpForm(user.c_str(), key.c_str());
    pnpHttp.sendHeader("Location", "/");
    pnpHttp.send(303, "text/plain", "");
}

static void startPnpHttp() {
    if (pnpHttpUp) return;
    delay(50);
    pnpHttp.on("/", HTTP_GET, handlePnpRoot);
    pnpHttp.on("/", HTTP_POST, handlePnpSave);
    pnpHttp.begin();
    pnpHttpUp = true;
    logf("[PNP] portal http://%s/  (DHCP, does not touch Wi-Fi)",
         WiFi.localIP().toString().c_str());
}

static void stopPnpHttp() {
    if (!pnpHttpUp) return;
    pnpHttp.stop();
    pnpHttpUp = false;
}

static void setupWmParams() {
    if (wmPnpUser) return;
    pnpLoad();
    wmPnpUser = new WiFiManagerParameter(
        "pnp_user", "ParksnPeaks user ID (callsign)", pnpUser, sizeof(pnpUser) - 1);
    wmPnpKey = new WiFiManagerParameter(
        "pnp_key",
        "ParksnPeaks API key (blank=keep, OFF=disable)",
        "",
        sizeof(pnpKey) - 1,
        "type=\"password\"");
    wm.addParameter(wmPnpUser);
    wm.addParameter(wmPnpKey);
    wm.setSaveParamsCallback(onPortalSave);
}

static bool fatalHttp(int code) {
    return code >= 400 && code < 500 && code != 408 && code != 429;
}

static int readHttpCode(Client& s) {
    unsigned long start = millis();
    char resp[160];
    size_t rn = 0;
    while ((millis() - start) < 8000 && rn + 1 < sizeof(resp)) {
        while (s.available() && rn + 1 < sizeof(resp)) resp[rn++] = (char)s.read();
        if (!s.connected() && !s.available()) break;
        delay(5);
    }
    resp[rn] = 0;
    int code = -1;
    const char* hp = strstr(resp, "HTTP/");
    if (hp) sscanf(hp, "HTTP/%*s %d", &code);
    return code;
}

static int postPota(const Pending& spot) {
    char activator[24], park[24], freq[24], mode[20], spotter[24], comments[120];
    jsonEscape(spot.activator, activator, sizeof(activator));
    jsonEscape(spot.park, park, sizeof(park));
    jsonEscape(spot.freq, freq, sizeof(freq));
    jsonEscape(spot.mode, mode, sizeof(mode));
    jsonEscape(spot.spotter, spotter, sizeof(spotter));
    jsonEscape(spot.comments, comments, sizeof(comments));

    char json[384];
    int n = snprintf(json, sizeof(json),
                     "{\"activator\":\"%s\",\"reference\":\"%s\",\"frequency\":\"%s\","
                     "\"mode\":\"%s\",\"spotter\":\"%s\",\"source\":\"MeshCore\","
                     "\"comments\":\"%s\"}",
                     activator, park, freq, mode, spotter, comments);
    if (n <= 0 || n >= (int)sizeof(json)) {
        logf("[POTA] JSON overflow");
        return 400;
    }

    logf("[POTA] POST %s\n", json);

    WiFiClientSecure tls;
    tls.setInsecure();
    tls.setTimeout(8000);
    // Must connect by hostname so CloudFront gets SNI api.pota.app (IP connect = fatal alert).
    if (!tls.connect(POTA_HOST, 443)) {
        logf("[POTA] TLS api.pota.app:443 failed");
        cachedPotaAt = 0;
        return -1;
    }

    int bodyLen = (int)strlen(json);
    tls.printf("POST %s HTTP/1.1\r\n", POTA_PATH);
    tls.print("Host: " POTA_HOST "\r\n");
    tls.print("User-Agent: MeshCore-POTA-Gateway/v1.17.1+\r\n");
    tls.print("Content-Type: application/json\r\n");
    tls.printf("Content-Length: %d\r\n", bodyLen);
    tls.print("Connection: close\r\n\r\n");
    tls.write((const uint8_t*)json, (size_t)bodyLen);

    int code = readHttpCode(tls);
    tls.stop();
    return code;
}

static int postPnp(const Pending& spot) {
    if (!pnpEnabled()) return 0;

    char activator[24], site[28], mode[20], comments[120], user[24], key[56], mhz[16];
    jsonEscape(spot.activator, activator, sizeof(activator));
    jsonEscape(spot.park, site, sizeof(site));
    jsonEscape(spot.mode, mode, sizeof(mode));
    jsonEscape(spot.comments, comments, sizeof(comments));
    jsonEscape(pnpUser, user, sizeof(user));
    jsonEscape(pnpKey, key, sizeof(key));
    freqToMhz(spot.freq, mhz, sizeof(mhz));

    const char* actClass = (spot.program == PROG_SOTA) ? "SOTA" : "WWFF";

    char json[512];
    int n = snprintf(json, sizeof(json),
                     "{\"actClass\":\"%s\",\"actCallsign\":\"%s\",\"actSite\":\"%s\","
                     "\"mode\":\"%s\",\"freq\":\"%s\",\"comments\":\"%s\","
                     "\"userID\":\"%s\",\"APIKey\":\"%s\"}",
                     actClass, activator, site, mode, mhz, comments, user, key);
    if (n <= 0 || n >= (int)sizeof(json)) {
        logf("[PNP] JSON overflow");
        return 400;
    }

    logf("[PNP] POST %s %s %s %s %s\n", actClass, spot.activator, spot.park, mhz, spot.mode);

    WiFiClient http;
    http.setTimeout(8000);
    if (!http.connect(PNP_HOST, 80)) {
        logf("[PNP] TCP parksnpeaks.org:80 failed");
        return -1;
    }

    int bodyLen = (int)strlen(json);
    http.printf("POST %s HTTP/1.1\r\n", PNP_PATH);
    http.print("Host: " PNP_HOST "\r\n");
    http.print("User-Agent: MeshCore-POTA-Gateway/v1.17.1+\r\n");
    http.print("Content-Type: application/json\r\n");
    http.printf("Content-Length: %d\r\n", bodyLen);
    http.print("Connection: close\r\n\r\n");
    http.write((const uint8_t*)json, (size_t)bodyLen);

    int code = readHttpCode(http);
    http.stop();
    return code;
}

static bool httpOk(int code) {
    return code == 200 || code == 201 || code == 204;
}

static void waitForWifi(TickType_t maxWait) {
    TickType_t start = xTaskGetTickCount();
    while (WiFi.status() != WL_CONNECTED) {
        if ((xTaskGetTickCount() - start) >= maxWait) return;
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static bool enqueueSpot(const Pending& slot) {
    if (!spotQueue) return false;
    if (xQueueSend(spotQueue, &slot, 0) == pdTRUE) return true;
    Pending dropped;
    if (xQueueReceive(spotQueue, &dropped, 0) == pdTRUE) {
        logf("[POTA] queue full, dropped oldest %s %s", dropped.activator, dropped.park);
    }
    if (xQueueSend(spotQueue, &slot, 0) == pdTRUE) return true;
    logf("[POTA] queue full, dropped %s %s", slot.activator, slot.park);
    return false;
}

static void httpTask(void*) {
    Pending spot;
    for (;;) {
        if (xQueueReceive(spotQueue, &spot, portMAX_DELAY) != pdTRUE) continue;
        httpBusy = true;

        int attempts = 0;
        for (;;) {
            waitForWifi(pdMS_TO_TICKS(15000));
            if (WiFi.status() != WL_CONNECTED) {
                logf("[POTA] waiting for Wi-Fi");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }

            const bool needPota = (spot.program == PROG_POTA);
            const bool needPnp = pnpEnabled() && (spot.program == PROG_WWFF || spot.program == PROG_SOTA);

            if (!needPota && !needPnp) {
                logf("[PNP] skipped — no valid ParksnPeaks API key on the portal");
                break;
            }

            int potaCode = 0;
            int pnpCode = 0;
            if (needPota) {
                potaCode = postPota(spot);
                if (httpOk(potaCode)) {
                    logf("[POTA] OK HTTP %d %s @ %s %s %s\n",
                                  potaCode, spot.activator, spot.park, spot.freq, spot.mode);
                } else {
                    logf("[POTA] POST failed HTTP %d\n", potaCode);
                }
            }
            if (needPnp) {
                pnpCode = postPnp(spot);
                if (httpOk(pnpCode)) {
                    logf("[PNP] OK HTTP %d %s %s @ %s %s %s\n",
                                  pnpCode, programName(spot.program),
                                  spot.activator, spot.park, spot.freq, spot.mode);
                } else {
                    logf("[PNP] POST failed HTTP %d\n", pnpCode);
                }
            }

            const bool potaDone = !needPota || httpOk(potaCode);
            const bool pnpDone = !needPnp || httpOk(pnpCode);
            if (potaDone && pnpDone) break;

            const int failCode = (!potaDone) ? potaCode : pnpCode;
            if (fatalHttp(failCode) || ++attempts >= 5) {
                logf("[POTA] dropping spot");
                break;
            }
            uint32_t backoff = 3000u * (1u << (attempts - 1));
            if (backoff > 30000) backoff = 30000;
            vTaskDelay(pdMS_TO_TICKS(backoff));
        }
        httpBusy = false;
    }
}

static const char* skipSpotPrefix(const char* message, const char** afterSpot) {
    const char* p = skipWs(message);
    const char* after;
    if (tokenEq(p, "#pota", 5, &after) ||
        tokenEq(p, "#wwff", 5, &after) ||
        tokenEq(p, "#sota", 5, &after) ||
        tokenEq(p, "#pnp", 4, &after)) {
        p = after;
    }
    if (!tokenEq(p, "SPOT", 4, &after)) return nullptr;
    *afterSpot = after;
    return p;
}

static void potaBlocksLoad() {
    char csv[POTA_BLOCK_MAX * 16];
    csv[0] = 0;
    Preferences store;
    store.begin("pota", true);
    store.getString("blocks", csv, sizeof(csv));
    store.end();
    potaBlockLoadCsv(&spotGuard, csv);
    if (spotGuard.blockCount) {
        logf("[POTA] block list %u callsign(s)", (unsigned)spotGuard.blockCount);
    }
}

static void potaBlocksSave() {
    char csv[POTA_BLOCK_MAX * 16];
    potaBlockCsv(&spotGuard, csv, sizeof(csv));
    Preferences store;
    store.begin("pota", false);
    store.putString("blocks", csv);
    store.end();
}

bool PotaSpotter::looksLikeSpot(const char* message) {
    if (!message) return false;
    const char* after;
    return skipSpotPrefix(message, &after) != nullptr;
}

void PotaSpotter::initWiFi() {
#if defined(ESP_ARDUINO_VERSION_VAL) && \
    ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    Serial.setTxTimeoutMs(50);
#endif
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.persistent(true);

    setupWmParams();
    potaBlocksLoad();

    wm.setHostname("meshcore-pota");
    wm.setConfigPortalBlocking(false);
    wm.setConnectTimeout(8);
    wm.setConfigPortalTimeout(180);

    lastStaAttempt = millis();
    lastReconnect = lastStaAttempt;
    portalStarted = false;
    WiFi.begin();

    if (!spotQueue) {
        spotQueue = xQueueCreate(POTA_QUEUE_LEN, sizeof(Pending));
    }
    if (!httpTaskHandle && spotQueue) {
        xTaskCreatePinnedToCore(httpTask, "potaHttp", 12288, nullptr, 1, &httpTaskHandle, 0);
    }

    logf("[POTA] Wi-Fi STA starting; portal MeshCore-POTA-Gateway if no network in 25s");
}

void PotaSpotter::handleLoop() {
    unsigned long now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        wifi_mode_t mode = WiFi.getMode();
        if (portalStarted || wm.getConfigPortalActive() || mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) {
            tearDownSoftAp();
        }
        if (!staOnline) {
            bindStaDns();
            logf("[POTA] Wi-Fi %s  gw %s  dns %s  rssi %d",
                 WiFi.localIP().toString().c_str(),
                 WiFi.gatewayIP().toString().c_str(),
                 WiFi.dnsIP().toString().c_str(),
                 WiFi.RSSI());
            startPnpHttp();
            staOnline = true;
        }
        if (pnpHttpUp) pnpHttp.handleClient();
        return;
    }
    staOnline = false;
    stopPnpHttp();

    if (portalStarted || wm.getConfigPortalActive()) {
        wm.process();
        return;
    }

    if (!portalStarted && (now - lastStaAttempt) > 25000) {
        logf("[POTA] STA timeout — opening config portal MeshCore-POTA-Gateway (192.168.4.1)");
        WiFi.disconnect(false, false);
        delay(200);
        wm.startConfigPortal("MeshCore-POTA-Gateway");
        portalStarted = true;
        lastReconnect = now;
        return;
    }
}

bool PotaSpotter::processMessage(const char* senderCall, const char* message,
                                 const uint8_t* nodePubKey, unsigned nodePubLen) {
    if (!looksLikeSpot(message)) return false;

    const char* p = nullptr;
    if (!skipSpotPrefix(message, &p)) return false;

    Pending slot;
    memset(&slot, 0, sizeof(slot));

    const char* after;
    if (tokenEq(p, "POTA", 4, &after)) {
        slot.program = PROG_POTA;
        p = after;
    } else if (tokenEq(p, "WWFF", 4, &after)) {
        slot.program = PROG_WWFF;
        p = after;
    } else if (tokenEq(p, "SOTA", 4, &after)) {
        slot.program = PROG_SOTA;
        p = after;
    }

    p = token(p, slot.activator, sizeof(slot.activator));
    p = token(p, slot.park, sizeof(slot.park));
    p = token(p, slot.freq, sizeof(slot.freq));
    p = token(p, slot.mode, sizeof(slot.mode));
    p = skipWs(p);

    if (!slot.activator[0] || !slot.park[0] || !slot.freq[0] || !slot.mode[0]) {
        logf("[POTA] Need: SPOT [POTA|WWFF|SOTA] <CALL> <REF> <FREQ> <MODE> [comment]");
        return false;
    }

    toUpperInPlace(slot.activator);
    toUpperInPlace(slot.mode);
    toUpperInPlace(slot.park);

    if (slot.program == 0) {
        if (strchr(slot.park, '/')) slot.program = PROG_SOTA;
        else if (strstr(slot.park, "FF-")) slot.program = PROG_WWFF;
        else slot.program = PROG_POTA;
    }

    if (slot.program == PROG_POTA) normalizePark(slot.park);
    normalizeFreq(slot.freq);

    if (senderCall && senderCall[0] && strncasecmp(senderCall, "MC-", 3) != 0) {
        copyField(slot.spotter, sizeof(slot.spotter), senderCall, strlen(senderCall));
        toUpperInPlace(slot.spotter);
    } else {
        copyField(slot.spotter, sizeof(slot.spotter), slot.activator, strlen(slot.activator));
    }

    if (*p) {
        copyField(slot.comments, sizeof(slot.comments), p, strlen(p));
    } else {
        copyField(slot.comments, sizeof(slot.comments), "MeshCore room gateway", 22);
    }

    const char* shape = potaShapeWhy(slot.program, slot.activator, slot.park, slot.freq, slot.mode);
    if (shape) {
        logf("[POTA] rejected shape %s %s %s %s %s",
             shape, slot.activator, slot.park, slot.freq, slot.mode);
        return false;
    }
    if (potaBlocked(&spotGuard, slot.activator)) {
        logf("[POTA] rejected blocked %s", slot.activator);
        return false;
    }
    if (slot.spotter[0] && strcmp(slot.spotter, slot.activator) != 0 &&
        potaBlocked(&spotGuard, slot.spotter)) {
        logf("[POTA] rejected blocked %s", slot.spotter);
        return false;
    }

    if ((slot.program == PROG_WWFF || slot.program == PROG_SOTA) && !pnpEnabled()) {
        logf("[PNP] %s spot not queued — set ParksnPeaks user + API key at http://%s/",
                      programName(slot.program),
                      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "192.168.4.1");
        return false;
    }

    if (!spotQueue) {
        logf("[POTA] queue not ready");
        return false;
    }

    char node[POTA_NODE_ID_LEN];
    potaNodeFromPub(node, sizeof(node), nodePubKey, nodePubLen);

    uint32_t now = (uint32_t)millis();
    potaGuardNoteTime(&spotGuard, now);
    int limit = potaGuardLimit(&spotGuard, slot.program, slot.activator, slot.park, slot.freq, slot.mode,
                               node, now);
    if (limit != POTA_LIMIT_OK) {
        const char* why = (limit == POTA_LIMIT_DUP) ? "duplicate" :
                          (limit == POTA_LIMIT_CALL) ? "call-rate" :
                          (limit == POTA_LIMIT_NODE) ? "node-rate" : "hourly";
        logf("[POTA] rejected %s %s %s", why, slot.activator, slot.park);
        return false;
    }

    if (!enqueueSpot(slot)) return false;
    potaGuardRemember(&spotGuard, slot.program, slot.activator, slot.park, slot.freq, slot.mode, node, now);
    logf("[%s] Queued %s %s %s %s\n",
                  slot.program == PROG_POTA ? "POTA" : "PNP",
                  slot.activator, slot.park, slot.freq, slot.mode);
    return true;
}

void PotaSpotter::handleAdmin(const char* args, char* reply, unsigned replyLen) {
    if (!reply || replyLen < 8) return;
    reply[0] = 0;
    if (!args) args = "";

    char cmd[16];
    const char* rest = token(args, cmd, sizeof(cmd));
    if (strcasecmp(cmd, "blocks") == 0) {
        if (spotGuard.blockCount == 0) {
            snprintf(reply, replyLen, "block list empty");
            return;
        }
        logf("[POTA] block list (%u)", (unsigned)spotGuard.blockCount);
        for (uint8_t i = 0; i < spotGuard.blockCount; i++) {
            logf("[POTA]  %s", spotGuard.blocks[i]);
        }
        int used = snprintf(reply, replyLen, "blocked (%u):", (unsigned)spotGuard.blockCount);
        if (used < 0 || (unsigned)used >= replyLen) return;
        for (uint8_t i = 0; i < spotGuard.blockCount; i++) {
            size_t left = replyLen - (unsigned)used;
            int n = snprintf(reply + used, left, " %s", spotGuard.blocks[i]);
            if (n < 0 || (unsigned)n >= left) {
                if (left > 4) snprintf(reply + used, left, " ...");
                break;
            }
            used += n;
        }
        return;
    }

    if (strcasecmp(cmd, "block") != 0 && strcasecmp(cmd, "unblock") != 0) {
        snprintf(reply, replyLen, "pota block|unblock CALL | pota blocks");
        return;
    }

    char call[16];
    token(rest, call, sizeof(call));
    toUpperInPlace(call);
    char base[16];
    if (!potaBaseCall(call, base, sizeof(base))) {
        snprintf(reply, replyLen, "bad callsign");
        return;
    }

    if (strcasecmp(cmd, "block") == 0) {
        int rc = potaBlockAdd(&spotGuard, base);
        if (rc == 0) {
            potaBlocksSave();
            snprintf(reply, replyLen, "blocked %s", base);
            logf("[POTA] blocked %s", base);
        } else if (rc == 2) {
            snprintf(reply, replyLen, "block list full (%d)", POTA_BLOCK_MAX);
        } else if (rc == 3) {
            snprintf(reply, replyLen, "already blocked %s", base);
        } else {
            snprintf(reply, replyLen, "bad callsign");
        }
        return;
    }

    if (potaBlockRemove(&spotGuard, base) == 0) {
        potaBlocksSave();
        snprintf(reply, replyLen, "unblocked %s", base);
        logf("[POTA] unblocked %s", base);
    } else {
        snprintf(reply, replyLen, "not blocked %s", base);
    }
}

void PotaSpotter::formatStatus(char* buf, unsigned bufLen) {
    if (!buf || bufLen < 8) return;
    UBaseType_t waiting = spotQueue ? uxQueueMessagesWaiting(spotQueue) : 0;
    const char* pnp = pnpEnabled() ? "pnp=on" : "pnp=off";
    if (WiFi.status() == WL_CONNECTED) {
        String ip = WiFi.localIP().toString();
        snprintf(buf, bufLen, "WiFi %s q=%u busy=%d %s",
                 ip.c_str(), (unsigned)waiting, httpBusy ? 1 : 0, pnp);
    } else if (wm.getConfigPortalActive()) {
        snprintf(buf, bufLen, "portal 192.168.4.1 q=%u %s", (unsigned)waiting, pnp);
    } else {
        snprintf(buf, bufLen, "WiFi down q=%u %s", (unsigned)waiting, pnp);
    }
    size_t n = strlen(buf);
    if (n + 1 < bufLen) {
        snprintf(buf + n, bufLen - n, " blk=%u", (unsigned)spotGuard.blockCount);
    }
}

bool PotaSpotter::staIsUp() {
    return WiFi.status() == WL_CONNECTED || wm.getConfigPortalActive();
}

void PotaSpotter::formatScreen(char* ipBuf, unsigned ipLen, char* extraBuf, unsigned extraLen) {
    if (ipBuf && ipLen) {
        if (WiFi.status() == WL_CONNECTED) {
            String ip = WiFi.localIP().toString();
            snprintf(ipBuf, ipLen, "%s", ip.c_str());
        } else if (wm.getConfigPortalActive()) {
            snprintf(ipBuf, ipLen, "192.168.4.1");
        } else {
            snprintf(ipBuf, ipLen, "WiFi ...");
        }
    }
    if (extraBuf && extraLen) {
        snprintf(extraBuf, extraLen, "POTA %s", pnpEnabled() ? "pnp=on" : "pnp=off");
    }
}

#endif
