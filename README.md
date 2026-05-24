# liboutline

Outline shader mod untuk SA-MP Mobile (com.sampmobilerp.game).

Hook `glShaderSource` via libGLESv2 untuk inject outline effect pada skin/ped characters. Murni kosmetik — visible-only outline, tidak tembus dinding.

## Cara Kerja

- Hook `glShaderSource` yang dipanggil saat shader skin di-compile
- Deteksi fragment shader skin via marker string (`gl_FragColor = fcolor`, dll)
- Inject fungsi `applyOutline()` ke fragment shader sebelum compile
- Outline menggunakan alpha neighbor sampling — hanya muncul di tepi karakter

## Setup AML Headers

**WAJIB** sebelum build: copy 3 header dari AML SDK ke `include/mod/`:

```bash
cp /path/to/AML/mod/amlmod.h    include/mod/
cp /path/to/AML/mod/iaml.h      include/mod/
cp /path/to/AML/mod/interface.h include/mod/
```

Atau clone AML SDK:
```bash
git clone https://github.com/AndroidModLoader/AML.git
cp AML/mod/amlmod.h AML/mod/iaml.h AML/mod/interface.h include/mod/
```

## Build via GitHub Actions

Push ke branch `main` atau `dev` → Actions otomatis build → download artifact `liboutline-arm32`.

## Build Manual di Termux

```bash
source ~/.bashrc
cd ~/liboutline
$NDK/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./jni/Android.mk
```

Output: `libs/armeabi-v7a/liboutline.so`

## Install

Salin ke folder mods AML:
```bash
cp libs/armeabi-v7a/liboutline.so \
  /storage/emulated/0/Android/data/com.sampmobilerp.game/mods/
```

## Log

```bash
tail -f /storage/emulated/0/liboutline_log.txt
```

## Konfigurasi Default

| Parameter | Default | Keterangan |
|---|---|---|
| Warna outline | RGB(1.0, 0.3, 0.0) | Orange |
| Width | 0.003 | Lebar sampling alpha |
| Strength | 5.0 | Intensitas outline |

## API (opsional, via Lua FFI)

```lua
local ffi = require "ffi"
ffi.cdef[[
    typedef struct {
        void (*enable)(void);
        void (*disable)(void);
        int  (*is_enabled)(void);
        void (*set_color)(float r, float g, float b);
        void (*set_width)(float w);
    } OutlineAPI;
]]
local addr = tonumber(io.open("/storage/emulated/0/liboutline_addr.txt","r"):read("*l"))
local api = ffi.cast("OutlineAPI*", addr)

api.set_color(0.0, 1.0, 1.0)  -- cyan
api.set_width(0.004)
```

## Catatan

- Shader di-compile saat awal game load / saat enter server
- Efek baru terlihat setelah shader recompile (masuk server)
- Jika outline tidak muncul, cek log — kemungkinan GOT scan tidak menemukan slot, fallback ke hook direct libGLESv2

## Author

brruham-arch
