<#
.SYNOPSIS
    Set up and run the Cosmo laptop-side pipeline (PyCozmo + STT + dialogue + TTS).

.DESCRIPTION
    Automates the steps in this folder's README: creates a virtual environment,
    installs requirements, downloads the Piper voice model into assets\voices,
    and then runs whichever target you ask for (a smoke test or the full
    conversational pipeline).

    Run setup once. Connect your laptop to Cozmo's Wi-Fi before running any
    target that talks to the robot (hardware, movement, pipeline).

    Place this script in the cosmo-laptop\ folder (next to requirements.txt).

.PARAMETER Task
    What to do. One of:
      setup     - create venv, install deps, download the voice model (default)
      hardware  - run main.py        (face + beep smoke test)
      stt       - run test_stt.py
      dialogue  - run test_dialogue.py
      tts       - run test_tts.py
      movement  - run test_movement.py (head, arms, wheels)
      tests     - run every smoke test in order
      pipeline  - run the full conversational loop (pipeline.py)

.PARAMETER Voice
    Piper voice name to download (default: en_US-lessac-medium).

.PARAMETER Rebuild
    Delete and recreate the .venv before setup. Use this to repair a broken or
    version-mismatched virtual environment.

.PARAMETER PyVersion
    Pin the Python version for the venv via the py launcher (e.g. 3.12).

.EXAMPLE
    .\run.ps1                          # one-time setup
    .\run.ps1 -Task pipeline           # run the full pipeline
    .\run.ps1 -Task movement           # exercise the movement code
    .\run.ps1 -Rebuild -PyVersion 3.12 # nuke and rebuild the venv on Python 3.12
#>

[CmdletBinding()]
param(
    [ValidateSet("setup", "hardware", "stt", "dialogue", "tts", "movement", "tests", "pipeline")]
    [string]$Task = "setup",

    [string]$Voice = "en_US-lessac-medium",

    # Delete and recreate .venv before setup (use this to repair a broken venv).
    [switch]$Rebuild,

    # Pin the Python used for the venv via the py launcher, e.g. -PyVersion 3.12.
    # Leave empty to use whatever 'python' (or 'py') resolves to.
    [string]$PyVersion = ""
)

$ErrorActionPreference = "Stop"

# $PSScriptRoot is populated only when this file is *executed* as a script (not
# when its contents are pasted into the console). Fall back to the current
# directory so we can fail with a clear message instead of a cryptic one.
$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = (Get-Location).Path }

if (-not (Test-Path (Join-Path $ScriptDir "requirements.txt"))) {
    throw "No requirements.txt in '$ScriptDir'. Save run.ps1 into the cosmo-laptop folder and run it from there as a file (don't paste the script into the console)."
}

$VenvDir    = Join-Path $ScriptDir ".venv"
$VenvPython = Join-Path $VenvDir "Scripts\python.exe"
$VoicesDir  = Join-Path $ScriptDir "assets\voices"

function Write-Step($message) {
    Write-Host ""
    Write-Host ">> $message" -ForegroundColor Cyan
}

function Write-Ok($message) {
    Write-Host "   $message" -ForegroundColor Green
}

function Invoke-Checked($file, [string[]]$arguments) {
    & $file @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed (exit $LASTEXITCODE): $file $($arguments -join ' ')"
    }
}

function Get-BasePython {
    foreach ($candidate in @("python", "py")) {
        if (Get-Command $candidate -ErrorAction SilentlyContinue) { return $candidate }
    }
    throw "Python was not found on PATH. Install Python 3 and try again."
}

function New-Venv {
    if ($PyVersion) {
        Invoke-Checked "py" @("-$PyVersion", "-m", "venv", $VenvDir)
    } else {
        Invoke-Checked (Get-BasePython) @("-m", "venv", $VenvDir)
    }
}

function Initialize-Venv {
    if ($Rebuild -and (Test-Path $VenvDir)) {
        Write-Step "Removing existing .venv (rebuild requested)"
        Remove-Item -Recurse -Force $VenvDir
    }
    if (Test-Path $VenvPython) {
        Write-Ok "Virtual environment already exists."
        return
    }
    Write-Step "Creating virtual environment in .venv"
    New-Venv
    Write-Ok "Created .venv"
}

function Install-Deps {
    Write-Step "Installing dependencies from requirements.txt"
    Invoke-Checked $VenvPython @("-m", "pip", "install", "--upgrade", "pip")
    Invoke-Checked $VenvPython @("-m", "pip", "install", "-r", "requirements.txt")
    Write-Ok "Dependencies installed."
}

function Initialize-Voice {
    $onnx     = Join-Path $VoicesDir "$Voice.onnx"
    $onnxJson = Join-Path $VoicesDir "$Voice.onnx.json"
    if ((Test-Path $onnx) -and (Test-Path $onnxJson)) {
        Write-Ok "Voice '$Voice' already present in assets\voices."
        return
    }
    Write-Step "Downloading Piper voice '$Voice' into assets\voices"
    if (-not (Test-Path $VoicesDir)) {
        New-Item -ItemType Directory -Path $VoicesDir | Out-Null
    }
    Invoke-Checked $VenvPython @("-m", "piper.download_voices", $Voice, "--data-dir", $VoicesDir)

    # Fallback: some Piper versions ignore --data-dir and write to the cwd.
    if (-not (Test-Path $onnx)) {
        Get-ChildItem -Path $ScriptDir -Filter "$Voice.onnx*" -ErrorAction SilentlyContinue |
            ForEach-Object { Move-Item $_.FullName -Destination $VoicesDir -Force }
    }
    if ((Test-Path $onnx) -and (Test-Path $onnxJson)) {
        Write-Ok "Voice ready."
    } else {
        throw "Voice files missing after download. Put $Voice.onnx and $Voice.onnx.json in assets\voices manually."
    }
}

function Assert-Setup {
    if (-not (Test-Path $VenvPython)) {
        throw "No virtual environment found. Run '.\run.ps1 -Task setup' first."
    }
}

function Invoke-Python($scriptName) {
    if (-not (Test-Path (Join-Path $ScriptDir $scriptName))) {
        throw "$scriptName not found in this folder. (Is it on a branch you haven't merged yet?)"
    }
    Write-Step "Running $scriptName"
    Invoke-Checked $VenvPython @($scriptName)
}

Push-Location $ScriptDir
try {
    switch ($Task) {
        "setup" {
            Initialize-Venv
            Install-Deps
            Initialize-Voice
            Write-Step "Setup complete"
            Write-Host "   Connect your laptop to Cozmo's Wi-Fi, then run:" -ForegroundColor Green
            Write-Host "     .\run.ps1 -Task pipeline" -ForegroundColor Green
        }
        "hardware" { Assert-Setup; Invoke-Python "main.py" }
        "stt"      { Assert-Setup; Invoke-Python "test_stt.py" }
        "dialogue" { Assert-Setup; Invoke-Python "test_dialogue.py" }
        "tts"      { Assert-Setup; Initialize-Voice; Invoke-Python "test_tts.py" }
        "movement" { Assert-Setup; Invoke-Python "test_movement.py" }
        "tests" {
            Assert-Setup
            Initialize-Voice
            Invoke-Python "main.py"
            Invoke-Python "test_stt.py"
            Invoke-Python "test_dialogue.py"
            Invoke-Python "test_tts.py"
            Invoke-Python "test_movement.py"
        }
        "pipeline" { Assert-Setup; Initialize-Voice; Invoke-Python "pipeline.py" }
    }
}
finally {
    Pop-Location
}