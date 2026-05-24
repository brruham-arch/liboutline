#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <android/log.h>
#include "amlmod.h"

MYMOD(brruham.libshadowfix, libshadowfix, 1.2, brruham-arch)

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
#define OFF_gpShadowPedTex          0xA48244
#define OFF_gpShadowCarTex          0xA48240
#define OFF_gpPostShadowTex         0xA4826C
#define OFF_shadowTarget            0x6B4060
#define OFF_UseAdvancedShadows      0x5B94EC
#define OFF_RenderStoredShadows     0x5BA720
#define OFF_CShadows_Init           0x5B87A0

static uint8_t* g_bRenderShadows = nullptr;
static float*   g_maxDist        = nullptr;
static float*   g_maxDistSqr     = nullptr;

typedef int  (*fn_UseAdvShadows)(int);
typedef void (*fn_RenderStored)(int);
typedef void (*fn_CShadowsInit)(void);

static fn_UseAdvShadows  orig_UseAdvShadows  = nullptr;
static fn_RenderStored   orig_RenderStored   = nullptr;
static fn_CShadowsInit   orig_CShadowsInit   = nullptr;

static uintptr_t g_base = 0;

static int hook_UseAdvShadows(int level) { return 1; }

static void hook_RenderStored(int param) {
    if (g_bRenderShadows) *g_bRenderShadows = 1;
    if (g_maxDist && *g_maxDist < 50.0f) *g_maxDist = 100.0f;
    if (g_maxDistSqr && *g_maxDistSqr < 2500.0f) *g_maxDistSqr = 10000.0f;
    if (orig_RenderStored) orig_RenderStored(param);
}

// Hook CShadows::Init — log texture pointers setelah init
static void hook_CShadowsInit(void) {
    if (orig_CShadowsInit) orig_CShadowsInit();
    // Log texture pointers setelah init
    uintptr_t* pedTex  = (uintptr_t*)(g_base + OFF_gpShadowPedTex);
    uintptr_t* carTex  = (uintptr_t*)(g_base + OFF_gpShadowCarTex);
    uintptr_t* postTex = (uintptr_t*)(g_base + OFF_gpPostShadowTex);
    uintptr_t  shadTgt = *(uintptr_t*)(g_base + OFF_shadowTarget);
    _logf("[SF] After Init: pedTex=0x%08X carTex=0x%08X postTex=0x%08X shadowTarget=0x%08X",
          (unsigned)*pedTex, (unsigned)*carTex, (unsigned)*postTex, (unsigned)shadTgt);
}

static uintptr_t getLibBase(const char* libname) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512]; uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, libname) && strstr(line, "r-xp")) {
            base = (uintptr_t)strtoul(line, nullptr, 16); break;
        }
    }
    fclose(f); return base;
}

static void doHook(uintptr_t base, uintptr_t off, void* hook, void** orig, const char* name) {
    void* addr = (void*)(base + off + 1);
    aml->Hook(addr, hook, orig);
    if (!*orig) {
        void* hD = dlopen("libdobby.so", RTLD_NOW|RTLD_GLOBAL);
        if (hD) {
            auto dH = (int(*)(void*,void*,void**))dlsym(hD, "DobbyHook");
            if (dH) dH(addr, hook, orig);
        }
    }
    _logf("[SF] Hook %s -> %s", name, *orig ? "OK" : "FAIL");
}

ON_MOD_PRELOAD() {
    remove(LOGFILE);
    _log("[SF] OnModPreLoad v1.2");
}

ON_MOD_LOAD() {
    _log("[SF] OnModLoad start");

    g_base = getLibBase("libGTASA.so");
    if (!g_base) {
        _log("[SF] ERROR: libGTASA not found");
        aml->ShowToast(false, "[ShadowFix] ERROR");
        return;
    }
    _logf("[SF] libGTASA base=0x%08X", (unsigned)g_base);

    g_bRenderShadows = (uint8_t*)(g_base + OFF_bRenderShadows);
    g_maxDist        = (float*)(g_base + OFF_MAX_DIST_PED_SHADOW);
    g_maxDistSqr     = (float*)(g_base + OFF_MAX_DIST_PED_SHADOW_SQR);

    *g_bRenderShadows = 1;
    *g_maxDist        = 100.0f;
    *g_maxDistSqr     = 10000.0f;

    doHook(g_base, OFF_UseAdvShadows,    (void*)hook_UseAdvShadows,  (void**)&orig_UseAdvShadows,  "UseAdvShadows");
    doHook(g_base, OFF_RenderStoredShadows, (void*)hook_RenderStored,(void**)&orig_RenderStored,   "RenderStored");
    doHook(g_base, OFF_CShadows_Init,    (void*)hook_CShadowsInit,   (void**)&orig_CShadowsInit,   "CShadows::Init");

    _log("[SF] OnModLoad DONE");
    aml->ShowToast(false, "[ShadowFix] v1.2");
}
