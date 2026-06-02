# Cosmo Hardware Phase 1-5 Setup and Run Script
# Paste this entire script into a fresh PowerShell terminal

Write-Host "=== Cosmo Setup and Run ===" -ForegroundColor Cyan

# Navigate to cosmo-laptop directory
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptPath
Write-Host "Working directory: $(Get-Location)" -ForegroundColor Green

# Create Python 3.12 venv
Write-Host "`n[1/5] Creating Python 3.12 virtual environment..." -ForegroundColor Cyan
python3.12 -m venv .venv
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to create venv. Ensure Python 3.12 is installed." -ForegroundColor Red
    exit 1
}

# Upgrade pip
Write-Host "[2/5] Upgrading pip..." -ForegroundColor Cyan
.venv\Scripts\python.exe -m pip install --upgrade pip --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to upgrade pip." -ForegroundColor Red
    exit 1
}

# Install requirements
Write-Host "[3/5] Installing dependencies from requirements.txt..." -ForegroundColor Cyan
.venv\Scripts\python.exe -m pip install -r requirements.txt --quiet
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Failed to install dependencies." -ForegroundColor Red
    exit 1
}

# Pre-download Whisper base model
Write-Host "[4/5] Pre-downloading Whisper base model (~140MB)..." -ForegroundColor Cyan
Write-Host "       This may take 2-5 minutes on first run..." -ForegroundColor Yellow
.venv\Scripts\python.exe -c "
import whisper
print('Downloading Whisper base model...')
model = whisper.load_model('base')
print('Model loaded successfully!')
" 2>&1 | ForEach-Object { Write-Host "       $_" }
if ($LASTEXITCODE -ne 0) {
    Write-Host "WARNING: Model download may have issues. Continuing anyway..." -ForegroundColor Yellow
}

# Run pipeline
Write-Host "`n[5/5] Starting Cosmo pipeline..." -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Green
Write-Host "Ready to chat! Speak into your mic:" -ForegroundColor Green
Write-Host "  - Say 'hello' for greetings" -ForegroundColor Green
Write-Host "  - Say 'tell me a joke' for humor" -ForegroundColor Green
Write-Host "  - Say 'who are you' for identity" -ForegroundColor Green
Write-Host "  - Or just chat naturally!" -ForegroundColor Green
Write-Host "======================================" -ForegroundColor Green
Write-Host ""

.venv\Scripts\python.exe pipeline.py
