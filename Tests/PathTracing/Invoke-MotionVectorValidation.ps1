[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$OutputDirectory,
    [string]$SceneName = "DamagedHelmet",
    [ValidateRange(1, 10000)]
    [int]$CaptureAfterFrames = 30,
    [ValidateRange(0.1, 180.0)]
    [float]$OrbitDegrees = 8.0,
    [ValidateRange(1, 10000)]
    [int]$OrbitFrames = 8,
    [ValidateRange(1, 3600)]
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ([string]::IsNullOrWhiteSpace($ExecutablePath))
{
    $ExecutablePath = Join-Path $repoRoot "bin\x64\Debug\RtPbrSurvey.exe"
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory))
{
    $OutputDirectory = Join-Path $repoRoot "bin\x64\Debug\PathTracingMotionVectorValidation"
}

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf))
{
    throw "RtPbrSurvey executable not found: $ExecutablePath"
}
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

function Quote-ProcessArgument([string]$Value)
{
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Invoke-MotionVectorCapture([string]$Variant, [bool]$Moving)
{
    $capturePath = Join-Path $OutputDirectory "$Variant.png"
    $logPath = Join-Path $OutputDirectory "$Variant.log"
    Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue

    $arguments = @(
        "-AutoSelectGltfAsset", (Quote-ProcessArgument $SceneName),
        "-UseSceneDefaults",
        "-EnablePathTracing",
        "-DebugPreviewResource", "PathTracing.MotionVectors",
        "-CapturePath", (Quote-ProcessArgument $capturePath),
        "-CaptureAfterFrames", $CaptureAfterFrames,
        "-LogToFile", (Quote-ProcessArgument $logPath),
        "-ExitAfterCapture"
    )
    if ($Moving)
    {
        $arguments += @(
            "-ReflectionOrbitDegrees", $OrbitDegrees.ToString([Globalization.CultureInfo]::InvariantCulture),
            "-ReflectionOrbitFrames", $OrbitFrames
        )
    }

    $process = Start-Process -FilePath $ExecutablePath `
        -ArgumentList ($arguments -join " ") `
        -WorkingDirectory $repoRoot `
        -WindowStyle Hidden `
        -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Capture $Variant timed out after $TimeoutSeconds seconds."
    }
    if ($process.ExitCode -ne 0)
    {
        throw "Capture $Variant failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $capturePath -PathType Leaf))
    {
        throw "Capture $Variant did not produce a PNG."
    }
    if (-not (Test-Path -LiteralPath $logPath -PathType Leaf))
    {
        throw "Capture $Variant did not produce a log."
    }

    $logLines = @(Get-Content -LiteralPath $logPath)
    $errors = @($logLines | Where-Object { $_ -match '^\[(ERROR|CORRUPTION)\]' })
    return [ordered]@{
        variant = $Variant
        moving = $Moving
        capturePath = $capturePath
        logPath = $logPath
        sha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        d3d12ErrorCount = $errors.Count
        d3d12Errors = $errors
    }
}

$captures = @(
    Invoke-MotionVectorCapture "stationary" $false
    Invoke-MotionVectorCapture "moving" $true
)
$different = $captures[0].sha256 -ne $captures[1].sha256
$d3d12ErrorCount = [int](($captures | ForEach-Object { $_.d3d12ErrorCount } | Measure-Object -Sum).Sum)
$report = [ordered]@{
    schemaVersion = 1
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    commit = (& git -C $repoRoot rev-parse HEAD).Trim()
    scene = $SceneName
    resource = "PathTracing.MotionVectors"
    centeredPreviewScale = 32.0
    captureAfterFrames = $CaptureAfterFrames
    orbitDegrees = $OrbitDegrees
    orbitFrames = $OrbitFrames
    result = [ordered]@{
        capturesDiffer = $different
        d3d12ErrorCount = $d3d12ErrorCount
    }
    captures = $captures
}

$jsonPath = Join-Path $OutputDirectory "path-tracing-motion-vectors.json"
$markdownPath = Join-Path $OutputDirectory "path-tracing-motion-vectors.md"
$report | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $jsonPath -Encoding utf8NoBOM
$markdown = @"
# Path Tracing Motion Vector Validation

- Generated UTC: $($report.generatedUtc)
- Commit: ``$($report.commit)``
- Scene: $SceneName
- Resource: ``PathTracing.MotionVectors``
- Centered preview scale: $($report.centeredPreviewScale)x
- Stationary SHA-256: ``$($captures[0].sha256)``
- Moving SHA-256: ``$($captures[1].sha256)``
- Captures differ: ``$different``
- D3D12 errors: $d3d12ErrorCount

The moving capture uses the generic deterministic Arcball automation exposed by the existing ReflectionOrbit flags.
The comparison proves that the displayed motion-vector resource reacts to camera movement. It does not independently
prove reprojection sign or coordinate-space correctness; those remain defined by the previous-NDC minus current-NDC
renderer contract.
"@
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8NoBOM

Write-Host "Stationary capture: $($captures[0].capturePath)"
Write-Host "Moving capture: $($captures[1].capturePath)"
Write-Host "JSON report: $jsonPath"
Write-Host "Captures differ: $different"
Write-Host "D3D12 errors: $d3d12ErrorCount"

if (-not $different)
{
    throw "Stationary and moving motion-vector captures are identical."
}
if ($d3d12ErrorCount -ne 0)
{
    throw "Motion-vector validation logged D3D12 errors."
}
