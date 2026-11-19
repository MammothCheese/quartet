# QUARTET in a web browser, built on Windows (emu/build.sh is the same for Linux and GitHub Actions).
#   powershell -File emu/build.ps1    (emcc on the PATH: run emsdk_env.ps1 first)
Set-Location (Join-Path $PSScriptRoot '..')
emcc -O2 -DDINGHY_HOST -DDINGHY_WEB '-DAPP_VER="web"' -Wno-unused-function -Ifirmware/hal -Ifirmware/src -Ifirmware/gen firmware/src/quartet.c -o docs/emu/quartet.js -sASYNCIFY -sINVOKE_RUN=0 -sMODULARIZE -sEXPORT_NAME=Quartet -sENVIRONMENT=web '-sEXPORTED_RUNTIME_METHODS=callMain,HEAPU8,HEAPU16,HEAP16' -sINITIAL_MEMORY=33554432 -sSTACK_SIZE=1048576
