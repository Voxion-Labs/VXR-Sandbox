# Full local build: figures + PDF + Wasm
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Py = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"

Write-Host "=== 1/3 Telemetry figures ==="
Push-Location (Join-Path $Root "research")
if (Test-Path $Py) {
    & $Py -m pip install -q -r requirements.txt
    & $Py generate_visuals.py
} else {
    Write-Warning "Python not found; skipping figure generation (using existing PNGs if present)."
}
Pop-Location

Write-Host "=== 2/3 Research PDF ==="
& (Join-Path $Root "scripts\build_pdf.ps1")

Write-Host "=== 3/3 Wasm kernel ==="
& (Join-Path $Root "scripts\build_wasm.ps1")

Write-Host ""
Write-Host "Build complete. Serve docs/ with: npx serve docs -p 8080"
