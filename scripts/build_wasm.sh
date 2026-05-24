#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Compiling vxr_kernel.cpp to WebAssembly..."

emcc "${ROOT_DIR}/src-cpp/vxr_kernel.cpp" \
  -o "${ROOT_DIR}/docs/vxr_kernel.js" \
  -O3 \
  -std=c++17 \
  -s MODULARIZE=1 \
  -s EXPORT_NAME=createVXRModule \
  -s EXPORTED_FUNCTIONS='["_analyze_prompt","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToNewUTF8"]' \
  -s ENVIRONMENT=web \
  -s FILESYSTEM=0 \
  --no-entry

echo "Done! Outputs:"
echo "  - docs/vxr_kernel.js"
echo "  - docs/vxr_kernel.wasm"
