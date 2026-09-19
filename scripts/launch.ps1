# SPDX-License-Identifier: MIT
# Copyright (c) 2026 itsloopyo

<#
.SYNOPSIS
    Launches Ghostrunner through Steam for a head-tracking test run.

.DESCRIPTION
    Unattended - Start-Process without -Wait, no prompts. Steam is the only
    store this mod has a build profile for.

.PARAMETER Windowed
    Launch windowed at -ResX by -ResY instead of the saved display mode.
#>

[CmdletBinding()]
param(
    [switch]$Windowed,
    [int]$ResX = 1920,
    [int]$ResY = 1080
)
$ErrorActionPreference = 'Stop'

$steam = Join-Path ${env:ProgramFiles(x86)} 'Steam\steam.exe'
if (-not (Test-Path $steam)) {
    Write-Host "ERROR: steam.exe not found at $steam." -ForegroundColor Red
    exit 1
}

$gameArgs = @()
if ($Windowed) { $gameArgs += @('-windowed', "-ResX=$ResX", "-ResY=$ResY") }

Write-Host "Launching Ghostrunner via Steam: $($gameArgs -join ' ')" -ForegroundColor Cyan
Start-Process -FilePath $steam -ArgumentList (@('-applaunch', '1139900') + $gameArgs)
