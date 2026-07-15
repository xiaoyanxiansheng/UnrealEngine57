[CmdletBinding()]
param(
    [string]$EngineRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path,
    [string]$Target = 'UnrealEditor',
    [ValidateSet('Win64')]
    [string]$Platform = 'Win64',
    [ValidateSet('Debug', 'DebugGame', 'Development', 'Shipping', 'Test')]
    [string]$Configuration = 'Development',
    [switch]$Clean,
    [string[]]$ExtraArgs = @()
)

$ErrorActionPreference = 'Stop'

$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$BuildBat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$BuildVersion = Join-Path $EngineRoot 'Engine\Build\Build.version'

if (-not (Test-Path -LiteralPath $BuildBat -PathType Leaf)) {
    throw "Engine root is invalid; Build.bat was not found: $BuildBat"
}

if (-not (Test-Path -LiteralPath $BuildVersion -PathType Leaf)) {
    throw "Engine root is invalid; Build.version was not found: $BuildVersion"
}

if (Get-Process -Name 'UnrealEditor' -ErrorAction SilentlyContinue) {
    throw 'UnrealEditor is running. Close it before rebuilding engine binaries.'
}

$BuildArguments = @(
    $Target
    $Platform
    $Configuration
    '-WaitMutex'
    '-NoHotReloadFromIDE'
)

$BuildArguments += $ExtraArgs

Write-Host "Engine root: $EngineRoot"
Write-Host "Build: $Target $Platform $Configuration"

Push-Location $EngineRoot
try {
    if ($Clean) {
        $CleanArguments = $BuildArguments + '-Clean'
        Write-Host "Clean command: $BuildBat $($CleanArguments -join ' ')"
        & $BuildBat @CleanArguments
        $CleanExitCode = $LASTEXITCODE

        if ($CleanExitCode -ne 0) {
            [Console]::Error.WriteLine("Unreal clean failed with exit code $CleanExitCode.")
            exit $CleanExitCode
        }
    }

    Write-Host "Build command: $BuildBat $($BuildArguments -join ' ')"
    & $BuildBat @BuildArguments
    $BuildExitCode = $LASTEXITCODE
}
finally {
    Pop-Location
}

if ($null -eq $BuildExitCode) {
    throw 'Build.bat did not return an exit code.'
}

if ($BuildExitCode -ne 0) {
    [Console]::Error.WriteLine("Unreal build failed with exit code $BuildExitCode.")
    exit $BuildExitCode
}

if ($Target -eq 'UnrealEditor' -and $Platform -eq 'Win64') {
    $EditorBinary = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
    $ProjectsBinary = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Projects.dll'
    Get-Item -LiteralPath $EditorBinary, $ProjectsBinary |
        Select-Object FullName, Length, LastWriteTime |
        Format-Table -AutoSize
}

Write-Host 'Unreal build completed successfully.'
exit 0
