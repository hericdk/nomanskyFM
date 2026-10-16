#!/bin/sh
# No Man Sky FM: compila o firmware (SLOOP 2.6 + telas preto e branco) para WebAssembly, para os vídeos das telas (tools/telas.mjs).
#   precisa: python3 com ziglang, numpy, pillow  (pip3 install ziglang numpy pillow)
#   ./build-wasm.sh   ->  build/nms.wasm
set -e
cd "$(dirname "$0")"
ROOT=$(pwd)
"${PYTHON:-/usr/bin/python3}" -c "import sys; sys.path.insert(0, '$ROOT/tools'); import build; build.generate()" >/dev/null
"${PYTHON:-/usr/bin/python3}" -m ziglang cc --target=wasm32-freestanding -O2 -fno-builtin -ffreestanding -nostdlib -w \
  -I "$ROOT/build/gen" -I "$ROOT/firmware/src" -I wasm \
  -Wl,--no-entry -Wl,--export-memory -Wl,-z,stack-size=1048576 -Wl,--global-base=1048576 \
  -o build/nms.wasm wasm/nms_wasm.c
ls -la build/nms.wasm
