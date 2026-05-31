#pragma once
#include <stdint.h>
#include <stdbool.h>

// Central state struct. All numeric sensor params match the JSON keys sent
// by the brain webapp's camera settings form (camQuality, camBright, etc.).
typedef struct {
    // WiFi
    char wifiSsid[64];
    char wifiPass[64];
    char wifiMode[4];    // "AP" | "STA"
    char espIP[16];
    char hostname[32];   // mDNS hostname, default "esprc-cam"

    // Stream
    uint8_t framesize;   // framesize_t enum value — use FRAMESIZE_* constants, not raw integers
    uint8_t jpegQuality; // 4-63, lower = better
    uint8_t fpsLimit;    // 0 = unlimited

    // Sensor (match brain cam form IDs without the "cam" prefix)
    int8_t  brightness;  // -2 to 2
    int8_t  contrast;    // -2 to 2
    int8_t  saturation;  // -2 to 2
    int8_t  aeLevel;     // -2 to 2
    uint16_t exposure;   // 0-1200
    uint8_t agcGain;     // 0-30
    uint8_t gainCeiling; // gainceiling_t (0=2x … 6=128x)
    uint8_t effect;      // 0-6
    uint8_t wbMode;      // 0-4

    // Sensor flags (stored as bool, sent as 0/1 in JSON)
    bool awb;
    bool awbGain;
    bool aecSensor;
    bool aecDsp;
    bool agc;
    bool bpc;
    bool wpc;
    bool rawGma;
    bool lensCorrection;
    bool hMirror;
    bool vFlip;
    bool dcw;
    bool colorBar;

    bool statsEnabled;  // when false the webapp skips polling /api/stats
} CameraState;
