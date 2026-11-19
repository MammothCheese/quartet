#!/bin/sh
# QUARTET in a web browser: the firmware compiled to WebAssembly (Emscripten), for the emulator page docs/emu/.
#   sh emu/build.sh            (needs emcc on the PATH: https://emscripten.org/docs/getting_started/)
# Out: docs/emu/quartet.js and docs/emu/quartet.wasm. The page itself is docs/emu/index.html, emu.js and worklet.js.
set -e
cd "$(dirname "$0")/.."
V=web                                     # (fixed, so the files change only when the firmware does)
emcc -O2 -DDINGHY_HOST -DDINGHY_WEB -DAPP_VER="\"$V\"" -Wno-unused-function \
    -Ifirmware/hal -Ifirmware/src -Ifirmware/gen firmware/src/quartet.c \
    -o docs/emu/quartet.js \
    -sASYNCIFY -sINVOKE_RUN=0 -sMODULARIZE -sEXPORT_NAME=Quartet -sENVIRONMENT=web \
    -sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8,HEAPU16,HEAP16 \
    -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576
ls -l docs/emu/quartet.js docs/emu/quartet.wasm
