# Compile IEEE paper using Tectonic (auto-downloads TeX deps)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Research = Join-Path $Root "research"
$TectonicDir = Join-Path $Root ".tools\tectonic"
$TectonicExe = Join-Path $TectonicDir "tectonic.exe"
$WhitepaperDir = Join-Path $Root "docs\whitepaper"

if (-not (Test-Path $TectonicExe)) {
    Write-Host "Downloading Tectonic..."
    New-Item -ItemType Directory -Force -Path $TectonicDir | Out-Null
    $Zip = Join-Path $env:TEMP "tectonic.zip"
    $Url = "https://github.com/tectonic-typesetting/tectonic/releases/download/tectonic%400.16.9/tectonic-0.16.9-x86_64-pc-windows-msvc.zip"
    Invoke-WebRequest -Uri $Url -OutFile $Zip -UseBasicParsing
    Expand-Archive -Path $Zip -DestinationPath $TectonicDir -Force
    Remove-Item $Zip -Force
    $nested = Get-ChildItem -Path $TectonicDir -Filter "tectonic.exe" -Recurse | Select-Object -First 1
    if ($nested -and $nested.FullName -ne $TectonicExe) {
        Copy-Item $nested.FullName $TectonicExe -Force
    }
}

Push-Location $Research
& $TectonicExe VXR_Sandbox_Paper.tex
Pop-Location

New-Item -ItemType Directory -Force -Path $WhitepaperDir | Out-Null
Copy-Item (Join-Path $Research "VXR_Sandbox_Paper.pdf") (Join-Path $WhitepaperDir "VXR_Sandbox_Research.pdf") -Force
Write-Host "PDF ready: docs/whitepaper/VXR_Sandbox_Research.pdf"
