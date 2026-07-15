[CmdletBinding()]
param(
    [string]$EngineRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path,
    [int]$TimeoutSeconds = 180,
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'
$EngineRoot = (Resolve-Path -LiteralPath $EngineRoot).Path
$EditorCmd = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path -LiteralPath $EditorCmd -PathType Leaf)) {
    throw "UnrealEditor-Cmd.exe was not found: $EditorCmd"
}

if (Get-Process -Name 'UnrealEditor', 'UnrealEditor-Cmd' -ErrorAction SilentlyContinue) {
    throw 'An Unreal Editor process is already running. Close it before the smoke test.'
}

if ([string]::IsNullOrWhiteSpace($LogPath)) {
    $LogDirectory = Join-Path $EngineRoot '.codex\tmp'
    New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null
    $LogPath = Join-Path $LogDirectory 'unreal-editor-smoke.log'
}
else {
    $LogPath = [System.IO.Path]::GetFullPath($LogPath)
}

Remove-Item -LiteralPath $LogPath -Force -ErrorAction SilentlyContinue

$EditorArguments = @(
    '-Unattended'
    '-NoP4'
    '-NullRHI'
    '-NoSplash'
    '-NoSound'
    '-NoLiveCoding'
    '-ExecCmds=QUIT_EDITOR'
    '-UTF8Output'
    "-AbsLog=$LogPath"
)

Write-Host "Smoke test: $EditorCmd $($EditorArguments -join ' ')"
$QuotedArguments = $EditorArguments | ForEach-Object {
    '"' + $_.Replace('"', '\"') + '"'
}
$ProcessStartInfo = New-Object System.Diagnostics.ProcessStartInfo
$ProcessStartInfo.FileName = $EditorCmd
$ProcessStartInfo.Arguments = $QuotedArguments -join ' '
$ProcessStartInfo.UseShellExecute = $false
$ProcessStartInfo.CreateNoWindow = $true
$EditorProcess = [System.Diagnostics.Process]::Start($ProcessStartInfo)

if (-not $EditorProcess.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $EditorProcess.Id -Force -ErrorAction SilentlyContinue
    throw "Unreal Editor smoke test exceeded $TimeoutSeconds seconds. Log: $LogPath"
}

$EditorExitCode = $EditorProcess.ExitCode

if (-not (Test-Path -LiteralPath $LogPath -PathType Leaf)) {
    throw "Unreal Editor did not create the smoke-test log: $LogPath"
}

$FailurePatterns = @(
    'LogPluginManager: Error'
    'Fatal error'
    'Assertion failed'
    'Unhandled Exception'
)
$Failures = Select-String -LiteralPath $LogPath -Pattern $FailurePatterns

if ($EditorExitCode -ne 0 -or $Failures) {
    if ($Failures) {
        $Failures | Select-Object LineNumber, Line | Format-Table -Wrap
    }
    [Console]::Error.WriteLine("Unreal Editor smoke test failed with exit code $EditorExitCode. Log: $LogPath")
    exit 1
}

Write-Host "Unreal Editor smoke test passed. Log: $LogPath"
exit 0
