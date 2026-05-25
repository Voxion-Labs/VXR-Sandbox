# Compile ReportLab PDF research paper
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Py = "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe"
$Research = Join-Path $Root "research"
$GenScript = Join-Path $Research "generate_research_paper.py"

Write-Host "Compiling VXR-Sandbox Research Paper (ReportLab)..."
Push-Location $Research
if (Test-Path $Py) {
    & $Py $GenScript
} else {
    python $GenScript
}
Pop-Location

Write-Host "PDF compilation completed successfully."

