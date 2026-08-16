# Build Wasm kernel via Emscripten (uses local .emsdk if present)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$EmsdkRoot = Join-Path $Root ".emsdk"

if (-not (Test-Path (Join-Path $EmsdkRoot "emsdk.bat"))) {
    Write-Host "Cloning Emscripten SDK (first run only)..."
    git clone https://github.com/emscripten-core/emsdk.git $EmsdkRoot
    $env:PATH = "$env:LOCALAPPDATA\Programs\Python\Python312;$env:LOCALAPPDATA\Programs\Python\Python312\Scripts;" + $env:PATH
    Push-Location $EmsdkRoot
    .\emsdk install latest
    .\emsdk activate latest
    Pop-Location
}

$EmccArgs = @(
    "src-cpp/vxr_kernel.cpp",
    "-o", "docs/vxr_kernel.js",
    "-O3", "-std=c++17",
    "-s", "MODULARIZE=1",
    "-s", "EXPORT_NAME=createVXRModule",
    "-s", "EXPORTED_FUNCTIONS=[`\"_analyzePrompt`\",`\"_analyze_prompt`\",`\"_free`\"]",
    "-s", "EXPORTED_RUNTIME_METHODS=[`"ccall`",`"cwrap`",`"UTF8ToString`",`"stringToNewUTF8`"]",
    "-s", "ENVIRONMENT=web",
    "-s", "FILESYSTEM=0",
    "--no-entry"
)

Push-Location $EmsdkRoot
cmd /c "emsdk_env.bat >nul 2>&1 && cd /d `"$Root`" && emcc $($EmccArgs -join ' ')"
Pop-Location

$wasm = Join-Path $Root "docs\vxr_kernel.wasm"
$js = Join-Path $Root "docs\vxr_kernel.js"
if ((Test-Path $wasm) -and (Test-Path $js) -and ((Get-Item $js).Length -gt 5000)) {
    Write-Host "Wasm build OK: docs/vxr_kernel.js + docs/vxr_kernel.wasm"
} else {
    throw "Wasm build failed. Check emcc output above."
}
