// liboutline — Outline Shader Mod for SA-MP Mobile
// Hook glShaderSource via libGLESv2 GOT
// Target: com.sampmobilerp.game (ARM 32-bit)
// Author: brruham-arch

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <android/log.h>
#include <GLES2/gl2.h>

#include "mod/amlmod.h"

// ============================================================
// MYMOD setup
// ============================================================
MYMOD(brruham.liboutline, liboutline, 1.0, brruham-arch)

// ============================================================
// Logging
// ============================================================
#define LOG_TAG  "liboutline"
#define LOGFILE  "/storage/emulated/0/liboutline_log.txt"

static void _log(const char* msg) {
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "%s", msg);
    FILE* f = fopen(LOGFILE, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}
static void _logf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    _log(buf);
}

// ============================================================
// State
// ============================================================
static int   g_enabled       = 1;
static float g_outline_r     = 1.0f;   // orange
static float g_outline_g     = 0.3f;
static float g_outline_b     = 0.0f;
static float g_outline_width = 0.003f;
static float g_outline_strength = 5.0f;

// ============================================================
// glShaderSource hook
// ============================================================
typedef void (*glShaderSource_t)(GLuint, GLsizei, const GLchar**, const GLint*);
static glShaderSource_t orig_glShaderSource = nullptr;

// Kata kunci yang menandai fragment shader skin/ped
// "gl_FragColor = fcolor" ada di fragment shader skin Arizona
static const char* FRAG_MARKERS[] = {
    "gl_FragColor = fcolor",
    "gl_FragColor.a < 0.2",
    "gl_FragColor.a < 0.8",
    "gl_FragColor.a < 0.5",
    nullptr
};

// Cek apakah string source mengandung marker fragment shader skin
static bool isSkinFragShader(const char* src) {
    if (!src) return false;
    for (int i = 0; FRAG_MARKERS[i] != nullptr; i++) {
        if (strstr(src, FRAG_MARKERS[i]) != nullptr) return true;
    }
    return false;
}

// Outline injection — append ke fragment shader sebelum void main penutup
// Teknik: sample alpha tetangga untuk deteksi tepi
static char* buildOutlineShader(const char* src) {
    // Cari posisi gl_FragColor terakhir — kita wrap seluruh output
    // Inject setelah baris gl_FragColor = ...; yang terakhir
    // dengan override warna bila edge terdeteksi

    const char* inject = R"(
void applyOutline() {
    float ox = %f;
    float a0 = gl_FragColor.a;
    if (a0 < 0.1) return;
    float a1 = texture2D(Diffuse, Out_Tex0 + vec2(ox, 0.0)).a;
    float a2 = texture2D(Diffuse, Out_Tex0 - vec2(ox, 0.0)).a;
    float a3 = texture2D(Diffuse, Out_Tex0 + vec2(0.0, ox)).a;
    float a4 = texture2D(Diffuse, Out_Tex0 - vec2(0.0, ox)).a;
    float edge = clamp((max(max(a1,a2),max(a3,a4)) - a0) * %f, 0.0, 1.0);
    gl_FragColor.rgb = mix(gl_FragColor.rgb, vec3(%f,%f,%f), edge);
}
)";

    // Format inject dengan nilai saat ini
    char inject_buf[1024];
    snprintf(inject_buf, sizeof(inject_buf), inject,
        g_outline_width,
        g_outline_strength,
        g_outline_r, g_outline_g, g_outline_b
    );

    // Cari "void main()" di source
    const char* main_pos = strstr(src, "void main()");
    if (!main_pos) {
        _log("[OUTLINE] void main() tidak ditemukan, skip inject");
        return nullptr;
    }

    // Cari closing brace terakhir dari main()
    // Strategi: cari "gl_FragColor" terakhir lalu inject setelahnya
    const char* last_frag = nullptr;
    const char* search = src;
    while (true) {
        const char* found = strstr(search, "gl_FragColor");
        if (!found) break;
        last_frag = found;
        search = found + 1;
    }

    if (!last_frag) {
        _log("[OUTLINE] gl_FragColor tidak ditemukan");
        return nullptr;
    }

    // Cari akhir baris tersebut (";")
    const char* semicolon = strchr(last_frag, ';');
    if (!semicolon) return nullptr;

    // Ukuran buffer baru
    size_t src_len    = strlen(src);
    size_t inject_len = strlen(inject_buf);
    // Inject fungsi sebelum void main, panggil di akhir
    // Kita inject fungsi di depan void main, lalu tambah call di akhir main

    // Hitung posisi insert: sebelum "void main()"
    size_t prefix_len = (size_t)(main_pos - src);
    size_t total_len  = src_len + inject_len + 64;

    char* newSrc = (char*)malloc(total_len);
    if (!newSrc) {
        _log("[OUTLINE] malloc gagal");
        return nullptr;
    }

    // Copy bagian sebelum void main()
    memcpy(newSrc, src, prefix_len);
    newSrc[prefix_len] = '\0';

    // Append fungsi outline
    strcat(newSrc, inject_buf);

    // Append sisa source
    const char* rest = main_pos;

    // Cari closing "}" terakhir dari source untuk inject applyOutline() call
    // Kita cari "}" paling akhir dan insert applyOutline() sebelumnya
    const char* last_brace = strrchr(rest, '}');
    if (!last_brace) {
        free(newSrc);
        return nullptr;
    }

    size_t rest_len = (size_t)(last_brace - rest);
    strncat(newSrc, rest, rest_len);
    strcat(newSrc, "\n    applyOutline();\n}");

    return newSrc;
}

// Hook glShaderSource
static void hook_glShaderSource(GLuint shader,
                                 GLsizei count,
                                 const GLchar** string,
                                 const GLint* length) {
    if (!g_enabled || count <= 0 || !string || !string[0]) {
        orig_glShaderSource(shader, count, string, length);
        return;
    }

    // Cek apakah ini fragment shader skin
    bool isSkin = false;
    for (GLsizei i = 0; i < count; i++) {
        if (string[i] && isSkinFragShader(string[i])) {
            isSkin = true;
            break;
        }
    }

    if (!isSkin) {
        orig_glShaderSource(shader, count, string, length);
        return;
    }

    _logf("[OUTLINE] Skin frag shader terdeteksi, shader id=%u", shader);

    // Gabung semua string source jadi satu (biasanya count=1 tapi jaga-jaga)
    size_t total = 0;
    for (GLsizei i = 0; i < count; i++) {
        if (string[i]) {
            if (length && length[i] > 0)
                total += (size_t)length[i];
            else
                total += strlen(string[i]);
        }
    }

    char* combined = (char*)malloc(total + 1);
    if (!combined) {
        orig_glShaderSource(shader, count, string, length);
        return;
    }
    combined[0] = '\0';
    for (GLsizei i = 0; i < count; i++) {
        if (!string[i]) continue;
        if (length && length[i] > 0) {
            strncat(combined, string[i], (size_t)length[i]);
        } else {
            strcat(combined, string[i]);
        }
    }

    // Build shader dengan outline
    char* newSrc = buildOutlineShader(combined);
    free(combined);

    if (!newSrc) {
        _log("[OUTLINE] buildOutlineShader gagal, pakai original");
        orig_glShaderSource(shader, count, string, length);
        return;
    }

    _logf("[OUTLINE] Shader injected, ukuran baru=%zu", strlen(newSrc));

    const GLchar* newStrings[1] = { newSrc };
    orig_glShaderSource(shader, 1, newStrings, nullptr);

    free(newSrc);
}

// ============================================================
// Cari dan patch GOT libGLESv2 di libsamp.so
// ============================================================
static bool patchGLESGOT() {
    // Ambil alamat glShaderSource dari libGLESv2 yang sudah di-load
    void* hGLES = dlopen("libGLESv2.so", RTLD_NOW | RTLD_NOLOAD);
    if (!hGLES) {
        // Coba tanpa RTLD_NOLOAD
        hGLES = dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
    }
    if (!hGLES) {
        _log("[OUTLINE] ERROR: libGLESv2.so tidak bisa dibuka");
        return false;
    }
    _logf("[OUTLINE] libGLESv2.so handle=%p", hGLES);

    void* real_glShaderSource = dlsym(hGLES, "glShaderSource");
    if (!real_glShaderSource) {
        _log("[OUTLINE] ERROR: glShaderSource tidak ditemukan di libGLESv2");
        return false;
    }
    _logf("[OUTLINE] real glShaderSource=%p", real_glShaderSource);

    // Ambil base libsamp.so
    void* hSamp = dlopen("libsamp.so", RTLD_NOW | RTLD_NOLOAD);
    if (!hSamp) {
        _log("[OUTLINE] ERROR: libsamp.so handle tidak ditemukan");
        return false;
    }
    uintptr_t samp_base = (uintptr_t)hSamp;
    _logf("[OUTLINE] libsamp.so base=0x%08X", (unsigned)samp_base);

    // Cari glShaderSource di GOT libsamp.so dengan scan
    // .got.plt libsamp: VA=0x235224, size=0x12e0
    // File offset: 0x234224 (dari analisa sebelumnya)
    // Tapi saat runtime, VA = base + section_va
    uintptr_t got_start = samp_base + 0x235224;
    uintptr_t got_end   = got_start + 0x12e0;

    _logf("[OUTLINE] Scan GOT: 0x%08X - 0x%08X", (unsigned)got_start, (unsigned)got_end);

    uintptr_t* found_slot = nullptr;
    for (uintptr_t addr = got_start; addr < got_end; addr += 4) {
        uintptr_t* slot = (uintptr_t*)addr;
        if (*slot == (uintptr_t)real_glShaderSource) {
            found_slot = slot;
            _logf("[OUTLINE] glShaderSource GOT slot ditemukan @ 0x%08X", (unsigned)addr);
            break;
        }
    }

    if (!found_slot) {
        // Fallback: scan .got juga (bukan hanya .got.plt)
        // .got libsamp: VA=0x23492c, size=0x8f8
        uintptr_t got2_start = samp_base + 0x23492c;
        uintptr_t got2_end   = got2_start + 0x8f8;
        _logf("[OUTLINE] Scan GOT2: 0x%08X - 0x%08X", (unsigned)got2_start, (unsigned)got2_end);

        for (uintptr_t addr = got2_start; addr < got2_end; addr += 4) {
            uintptr_t* slot = (uintptr_t*)addr;
            if (*slot == (uintptr_t)real_glShaderSource) {
                found_slot = slot;
                _logf("[OUTLINE] glShaderSource GOT2 slot @ 0x%08X", (unsigned)addr);
                break;
            }
        }
    }

    if (!found_slot) {
        _log("[OUTLINE] WARN: glShaderSource tidak ditemukan di GOT libsamp");
        _log("[OUTLINE] Fallback: hook via AML");

        // Fallback: hook langsung di libGLESv2 via AML
        // Ini akan affect semua caller, bukan hanya libsamp
        // Tapi tetap aman karena kita cek isSkinFragShader
        aml->Hook((void*)real_glShaderSource,
                  (void*)hook_glShaderSource,
                  (void**)&orig_glShaderSource);

        if (!orig_glShaderSource) {
            _log("[OUTLINE] ERROR: AML hook juga gagal");
            return false;
        }
        _logf("[OUTLINE] Fallback hook sukses, orig=%p", orig_glShaderSource);
        return true;
    }

    // Patch GOT: ubah pointer ke hook kita
    // Perlu set memory writable dulu
    orig_glShaderSource = (glShaderSource_t)(real_glShaderSource);

    // Gunakan llmo::mem::prot::set dari libsamp via AML
    // Offset 0x1E2DA7 (dari analisa)
    uintptr_t llmo_prot_set_off = 0x1E2DA6; // THUMB: offset - 1
    typedef void (*llmo_prot_set_t)(void*, unsigned int, int);
    llmo_prot_set_t llmo_prot_set = (llmo_prot_set_t)(samp_base + llmo_prot_set_off);

    // Set 1 page writable (PROT_READ|PROT_WRITE|PROT_EXEC = 7)
    uintptr_t page_size = 4096;
    uintptr_t page_addr = ((uintptr_t)found_slot) & ~(page_size - 1);
    llmo_prot_set((void*)page_addr, page_size, 7);
    _logf("[OUTLINE] mprotect via llmo @ page=0x%08X", (unsigned)page_addr);

    // Patch
    *found_slot = (uintptr_t)hook_glShaderSource;
    _logf("[OUTLINE] GOT patched: 0x%08X -> hook_glShaderSource", (unsigned)(uintptr_t)found_slot);

    // Restore ke read-only
    llmo_prot_set((void*)page_addr, page_size, 5); // PROT_READ|PROT_EXEC

    return true;
}

// ============================================================
// Eksport API sederhana untuk Lua (opsional)
// ============================================================
struct OutlineAPI {
    void (*enable)(void);
    void (*disable)(void);
    int  (*is_enabled)(void);
    void (*set_color)(float r, float g, float b);
    void (*set_width)(float w);
};

static void _enable(void)                       { g_enabled = 1; _log("[OUTLINE] enabled"); }
static void _disable(void)                      { g_enabled = 0; _log("[OUTLINE] disabled"); }
static int  _is_enabled(void)                   { return g_enabled; }
static void _set_color(float r, float g, float b) {
    g_outline_r = r; g_outline_g = g; g_outline_b = b;
    _logf("[OUTLINE] color=%.2f,%.2f,%.2f", r, g, b);
}
static void _set_width(float w) {
    g_outline_width = w;
    _logf("[OUTLINE] width=%.4f", w);
}

extern "C" {
    EXPORT OutlineAPI outline_api = {
        _enable, _disable, _is_enabled, _set_color, _set_width
    };
}

// ============================================================
// Mod lifecycle
// ============================================================
ON_MOD_PRELOAD() {
    remove(LOGFILE);
    _log("[OUTLINE] OnModPreLoad v1.0");
    g_enabled       = 1;
    g_outline_r     = 1.0f;
    g_outline_g     = 0.3f;
    g_outline_b     = 0.0f;
    g_outline_width = 0.003f;
    g_outline_strength = 5.0f;
}

ON_MOD_LOAD() {
    _log("[OUTLINE] OnModLoad mulai");
    _logf("[OUTLINE] Build: %s %s", __DATE__, __TIME__);

    if (!patchGLESGOT()) {
        _log("[OUTLINE] ERROR: patchGLESGOT gagal");
        aml->ShowToast(false, "[Outline] ERROR: hook gagal, cek log");
        return;
    }

    // Simpan alamat API
    FILE* f = fopen("/storage/emulated/0/liboutline_addr.txt", "w");
    if (f) {
        fprintf(f, "%lu\n", (unsigned long)&outline_api);
        fclose(f);
    }

    _log("[OUTLINE] OnModLoad SELESAI");
    aml->ShowToast(false, "[Outline] Shader mod aktif!");
}
