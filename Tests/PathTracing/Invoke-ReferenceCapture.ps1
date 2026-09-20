[CmdletBinding()]
param(
    [string]$ExecutablePath,
    [string]$OutputDirectory,
    [string]$SceneName = "DamagedHelmet",
    [string]$EvaluationCaseName,
    [ValidateRange(1, [uint32]::MaxValue)]
    [uint32]$Samples = 64,
    [uint32]$Seed = 1,
    [ValidateRange(0, 4)]
    [uint32]$EnvironmentMode = 0,
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
    $OutputDirectory = Join-Path $repoRoot "bin\x64\Debug\PathTracingReference"
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

function Invoke-Capture([string]$Variant)
{
    $capturePath = Join-Path $OutputDirectory "$Variant.png"
    $logPath = Join-Path $OutputDirectory "$Variant.log"
    Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue

    $arguments = @()
    if ([string]::IsNullOrWhiteSpace($EvaluationCaseName))
    {
        $arguments += "-AutoSelectGltfAsset"
        $arguments += (Quote-ProcessArgument $SceneName)
        $arguments += "-UseSceneDefaults"
    }
    else
    {
        $arguments += "-EvaluationCase"
        $arguments += (Quote-ProcessArgument $EvaluationCaseName)
    }
    $arguments += @(
        "-EnablePathTracing",
        "-PathTracingSamples", $Samples,
        "-PathTracingSeed", $Seed,
        "-PathTracingEnvironmentMode", $EnvironmentMode,
        "-CapturePath", (Quote-ProcessArgument $capturePath),
        "-LogToFile", (Quote-ProcessArgument $logPath),
        "-ExitAfterCapture"
    )

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
    $diagnosticLine = @($logLines | Where-Object { $_.StartsWith("[PathTracing] ") }) | Select-Object -Last 1
    if ($null -eq $diagnosticLine)
    {
        throw "Capture $Variant did not emit Path Tracing diagnostics."
    }
    $diagnostics = $diagnosticLine.Substring("[PathTracing] ".Length) | ConvertFrom-Json
    $errors = @($logLines | Where-Object { $_ -match '^\[(ERROR|CORRUPTION)\]' })
    $warnings = @($logLines | Where-Object { $_ -match '^\[WARNING\]' })

    return [ordered]@{
        variant = $Variant
        capturePath = $capturePath
        logPath = $logPath
        sha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        exitCode = $process.ExitCode
        d3d12ErrorCount = $errors.Count
        d3d12WarningCount = $warnings.Count
        d3d12Errors = $errors
        diagnostics = $diagnostics
    }
}

$captures = @(
    Invoke-Capture "a"
    Invoke-Capture "b"
)
$deterministic = $captures[0].sha256 -eq $captures[1].sha256
$d3d12ErrorCount = [int](($captures | ForEach-Object { $_.d3d12ErrorCount } | Measure-Object -Sum).Sum)
$d3d12WarningCount = [int](($captures | ForEach-Object { $_.d3d12WarningCount } | Measure-Object -Sum).Sum)

$videoControllers = @()
try
{
    $videoControllers = @(Get-CimInstance Win32_VideoController | ForEach-Object {
        [ordered]@{
            name = $_.Name
            driverVersion = $_.DriverVersion
            pnpDeviceId = $_.PNPDeviceID
        }
    })
}
catch
{
    $videoControllers = @([ordered]@{ queryError = $_.Exception.Message })
}

$gitCommit = (& git -C $repoRoot rev-parse HEAD).Trim()
$gitDirty = -not [string]::IsNullOrWhiteSpace((& git -C $repoRoot status --porcelain | Out-String))
$report = [ordered]@{
    schemaVersion = 1
    generatedUtc = [DateTime]::UtcNow.ToString("o")
    repository = [ordered]@{
        root = $repoRoot
        commit = $gitCommit
        dirty = $gitDirty
    }
    request = [ordered]@{
        scene = $SceneName
        evaluationCase = $EvaluationCaseName
        accumulatedSamples = $Samples
        randomSeed = $Seed
        environmentMode = $EnvironmentMode
    }
    hardware = [ordered]@{
        appAdapter = $captures[0].diagnostics.adapter
        videoControllers = $videoControllers
    }
    result = [ordered]@{
        deterministic = $deterministic
        d3d12ErrorCount = $d3d12ErrorCount
        sha256 = $captures[0].sha256
    }
    evaluationCase = $captures[0].diagnostics.evaluationCase
    captures = $captures
}

$jsonPath = Join-Path $OutputDirectory "path-tracing-reference.json"
$markdownPath = Join-Path $OutputDirectory "path-tracing-reference.md"
$report | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $jsonPath -Encoding utf8NoBOM

$gpuNames = ($videoControllers | ForEach-Object { $_.name } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join ", "
if ([string]::IsNullOrWhiteSpace($gpuNames))
{
    $gpuNames = $captures[0].diagnostics.adapter.name
}
$evaluationLabel = if ([string]::IsNullOrWhiteSpace($EvaluationCaseName)) { "None" } else { $EvaluationCaseName }
$markdown = @"
# Path Tracing Reference Report

- Generated UTC: $($report.generatedUtc)
- Commit: ``$gitCommit``
- Working tree dirty: ``$gitDirty``
- Scene: $SceneName
- Evaluation Case: $evaluationLabel
- Accumulated samples: $Samples
- Random seed: $Seed
- Environment mode: $EnvironmentMode
- GPU: $gpuNames
- Deterministic A/B: ``$deterministic``
- SHA-256: ``$($captures[0].sha256)``
- D3D12 errors: $d3d12ErrorCount
- D3D12 warnings: $d3d12WarningCount
- Path Tracing GPU average: $($captures[0].diagnostics.gpuTimeAverageMs) ms
- Path Tracing GPU range: $($captures[0].diagnostics.gpuTimeMinMs)-$($captures[0].diagnostics.gpuTimeMaxMs) ms
- GPU timing samples: $($captures[0].diagnostics.gpuTimingSampleCount)
- Average primary samples / second: $($captures[0].diagnostics.averagePrimarySamplesPerSecond)
- Max Ray Queries / frame: $($captures[0].diagnostics.maxRayQueriesPerFrame)

The JSON report contains the Evaluation Case ROI, Japanese comments, test items, per-capture paths, and diagnostics.
"@
$markdown | Set-Content -LiteralPath $markdownPath -Encoding utf8NoBOM

Write-Host "JSON report: $jsonPath"
Write-Host "Markdown report: $markdownPath"
Write-Host "SHA-256: $($captures[0].sha256)"
Write-Host "Deterministic: $deterministic"
Write-Host "D3D12 errors: $d3d12ErrorCount"

if (-not $deterministic)
{
    throw "Path Tracing A/B captures are not deterministic."
}
if ($d3d12ErrorCount -ne 0)
{
    throw "Path Tracing capture logged D3D12 errors."
}
