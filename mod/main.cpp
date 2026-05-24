#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <android/log.h>
#include "amlmod.h"

MYMOD(brruham.libshadowfix, libshadowfix, 1.1, brruham-arch)

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

// Offsets libGTASA.so (file offset = VA & ~1)
#define OFF_bRenderShadows          0xA46D3C
#define OFF_MAX_DIST_PED_SHADOW     0xA53530
#define OFF_MAX_DIST_PED_SHADOW_SQR 0xA53534
#define OFF_UseAdvancedShadows      0x5B94EC  // file even, THUMB hook +1
#define OFF_RenderStoredShadows     0x5BA720  // file even, THUMB hook +1
#define OFF_StoreShadowForVehicle   0x5B951C  // file even, THUMB hook +1
#define OFF_StoreShadowForPedObject 0x5B9E18  // file even, THUMB hook +1
#define OFF_CalcPedShadowValues     0x5BA3A4  // file even, THUMB hook +1

// Globals pointer — set setelah base diketahui
static uint8_t*  g_bRenderShadows  = nullptr;
static float*    g_maxDist         = nullptr;
static float*    g_maxDistSqr      = nullptr;

// Hook UseAdvancedShadows → force return 1
typedef int (*fn_UseAdvancedShadows)(int);
static fn_UseAdvancedShadows orig_UseAdvancedShadows = nullptr;
static int hook_UseAdvancedShadows(int level) {
    return 1;
}

// Hook RenderStoredShadows — pastikan flag enable setiap frame
typedef void (*fn_RenderStoredShadows)(int);
static fn_RenderStoredShadows orig_RenderStoredShadows = nullptr;
static void hook_RenderStoredShadows(int param) {
    // Paksa flag tiap kali render dipanggil
    if (g_bRenderShadows) *g_bRenderShadows = 1;
    if (g_maxDist && *g_maxDist < 50.0f) *g_maxDist = 100.0f;
    if (g_maxDistSqr && *g_maxDistSqr < 2500.0f) *g_maxDistSqr = 10000.0f;
    if (orig_RenderStoredShadows) orig_RenderStoredShadows(param);
}

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

static void doHook(uintptr_t base, uintptr_t off, void* hook, void** orig) {
    void* addr = (void*)(base + off + 1); // +1 = THUMB
    aml->Hook(addr, hook, orig);
    if (!*orig) {
        void* hDobby = dlopen("libdobby.so", RTLD_NOW | RTLD_GLOBAL);
        if (hDobby) {
            auto dobbyHook = (int(*)(void*,void*,void**))dlsym(hDobby, "DobbyHook");
            if (dobbyHook) dobbyHook(addr, hook, orig);
        }
    }
    _logf("[SF] Hook 0x%06X -> %s orig=%p",
          (unsigned)off, *orig ? "OK" : "FAIL", *orig);
}

ON_MOD_PRELOAD() {
    remove(LOGFILE);
    _log("[SF] OnModPreLoad v1.1");
}

ON_MOD_LOAD() {
    _log("[SF] OnModLoad start");

    uintptr_t base = getLibBase("libGTASA.so");
    if (!base) {
        _log("[SF] ERROR: libGTASA base not found");
        aml->ShowToast(false, "[ShadowFix] ERROR: libGTASA not found");
        return;
    }
    _logf("[SF] libGTASA base=0x%08X", (unsigned)base);

    // Setup global pointers
    g_bRenderShadows = (uint8_t*)(base + OFF_bRenderShadows);
    g_maxDist        = (float*)(base + OFF_MAX_DIST_PED_SHADOW);
    g_maxDistSqr     = (float*)(base + OFF_MAX_DIST_PED_SHADOW_SQR);

    // Set nilai awal
    *g_bRenderShadows = 1;
    *g_maxDist        = 100.0f;
    *g_maxDistSqr     = 10000.0f;
    _logf("[SF] globals set: bRender=1 maxDist=100");

    // Hook UseAdvancedShadows — force return 1
    doHook(base, OFF_UseAdvancedShadows,
           (void*)hook_UseAdvancedShadows,
           (void**)&orig_UseAdvancedShadows);

    // Hook RenderStoredShadows — enforce flags tiap frame
    doHook(base, OFF_RenderStoredShadows,
           (void*)hook_RenderStoredShadows,
           (void**)&orig_RenderStoredShadows);

    _log("[SF] OnModLoad DONE");
    aml->ShowToast(false, "[ShadowFix] v1.1 aktif!");
}
