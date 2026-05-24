#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <android/log.h>
#include "amlmod.h"

MYMOD(brruham.libshadowfix, libshadowfix, 1.0, brruham-arch)

#define LOG_TAG "libshadowfix"
#define LOGFILE "/storage/emulated/0/libshadowfix_log.txt"

static void _log(const char* msg) {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", msg);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static void _logf(const char* fmt, ...) {
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    _log(buf);
}

#define OFF_bRenderShadows          0xA46D3C
#define OFF_MAX_DIST_PED_SHADOW     0xA53530
#define OFF_MAX_DIST_PED_SHADOW_SQR 0xA53534
#define OFF_SHADOW_STRENGTH         0x967540
#define OFF_UseAdvancedShadows      0x5B94EC

typedef int (*UseAdvancedShadows_t)(int);
static UseAdvancedShadows_t orig_UseAdvancedShadows = nullptr;

static int hook_UseAdvancedShadows(int level) {
    return 1;
}

// Baca base address dari /proc/self/maps
static uintptr_t getLibBase(const char* libname) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, libname) && strstr(line, "r-xp")) {
            base = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(f);
    return base;
}

static void fixShadowGlobals(uintptr_t base) {
    uint8_t* bRender = (uint8_t*)(base + OFF_bRenderShadows);
    _logf("[SF] bRenderShadows before=%d", *bRender);
    *bRender = 1;
    _logf("[SF] bRenderShadows after=%d", *bRender);

    float* maxDist = (float*)(base + OFF_MAX_DIST_PED_SHADOW);
    _logf("[SF] MAX_DIST before=%.2f", *maxDist);
    *maxDist = 100.0f;

    float* maxDistSqr = (float*)(base + OFF_MAX_DIST_PED_SHADOW_SQR);
    *maxDistSqr = 10000.0f;

    int16_t* strength = (int16_t*)(base + OFF_SHADOW_STRENGTH);
    for (int i = 0; i < 10; i++) {
        if (strength[i] == 0) strength[i] = 150;
    }
    _log("[SF] globals fixed");
}

ON_MOD_PRELOAD() {
    remove(LOGFILE);
    _log("[SF] OnModPreLoad v1.0");
}

ON_MOD_LOAD() {
    _log("[SF] OnModLoad start");

    // Baca base dari /proc/self/maps — paling reliable
    uintptr_t base = getLibBase("libGTASA.so");
    if (!base) {
        _log("[SF] ERROR: libGTASA base tidak ditemukan di maps");
        aml->ShowToast(false, "[ShadowFix] ERROR: libGTASA not found");
        return;
    }
    _logf("[SF] libGTASA base=0x%08X", (unsigned)base);

    fixShadowGlobals(base);

    void* useAdvAddr = (void*)(base + OFF_UseAdvancedShadows + 1);
    _logf("[SF] UseAdvancedShadows addr=%p", useAdvAddr);

    aml->Hook(useAdvAddr,
              (void*)hook_UseAdvancedShadows,
              (void**)&orig_UseAdvancedShadows);

    if (!orig_UseAdvancedShadows) {
        _log("[SF] WARN: aml->Hook gagal, coba Dobby");
        void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
        if (hDobby) {
            auto dobbyHook = (int(*)(void*,void*,void**))
                                dlsym(hDobby, "DobbyHook");
            if (dobbyHook) {
                int r = dobbyHook(useAdvAddr,
                                  (void*)hook_UseAdvancedShadows,
                                  (void**)&orig_UseAdvancedShadows);
                _logf("[SF] DobbyHook result=%d", r);
            }
        }
    } else {
        _log("[SF] Hook UseAdvancedShadows OK");
    }

    _log("[SF] OnModLoad DONE");
    aml->ShowToast(false, "[ShadowFix] Aktif!");
}
