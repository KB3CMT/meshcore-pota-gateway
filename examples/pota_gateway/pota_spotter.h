#pragma once

#include <Arduino.h>

#ifdef WITH_POTA_GATEWAY

/**
 * Heltec / ESP32 room-server POTA gateway.
 * Parse and queue only. TLS POST runs on a worker task so Mesh::loop() stays
 * on the mesh. Build Heltec_v3_room_server_pota, heltec_v4_room_server_pota,
 * heltec_v4_r8_room_server_pota, or Xiao_S3_WIO_room_server_pota.
 *
 *   SPOT <CALL> <PARK> <FREQ> <MODE> [comments]          (POTA)
 *   SPOT [POTA|WWFF|SOTA] <CALL> <REF> <FREQ> <MODE> …
 *   #pota / #wwff / #sota SPOT ...
 *
 * WWFF/SOTA POST to parksnpeaks.org only if a valid PnP user+API key
 * was saved on the Wi-Fi portal. POTA always uses api.pota.app.
 *
 * Before queueing, drops spots that fail call/ref/freq/mode shape,
 * hit the local block list, repeat inside 5 minutes, exceed 3 spots
 * per activator callsign per 10 minutes, exceed 3 spots per logged-in
 * room node per 10 minutes, or exceed 20 spots per hour.
 */
class PotaSpotter {
public:
    static bool looksLikeSpot(const char* message);
    static void initWiFi();
    static void handleLoop();
    static bool processMessage(const char* senderCall, const char* message,
                               const uint8_t* nodePubKey = nullptr, unsigned nodePubLen = 0);
    static void handleAdmin(const char* args, char* reply, unsigned replyLen);
    static void formatStatus(char* buf, unsigned bufLen);
    static bool staIsUp();
    static void formatScreen(char* ipBuf, unsigned ipLen, char* extraBuf, unsigned extraLen);
};

#endif
