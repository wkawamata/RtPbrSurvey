param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\..\..\bin\x64\Debug\RtPbrSurvey.exe"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "hdr-variance-paired-report.json"),
    [int]$WarmupFrames = 32,
    [ValidateSet(64, 256, 1024)]
    [int]$MeasurementFrames = 64,
    [int]$RoiX = 895,
    [int]$RoiY = 278,
    [int]$RoiWidth = 75,
    [int]$RoiHeight = 85,
    [double]$TemporalWeight = 0.9
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$executable = (Resolve-Path -LiteralPath $ExecutablePath).Path
$output = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $outputDirectory))
{
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$outputStem = [System.IO.Path]::GetFileNameWithoutExtension($output)
$offPath = Join-Path $outputDirectory "$outputStem-off.json"
$onPath = Join-Path $outputDirectory "$outputStem-on.json"

function Invoke-Diagnostic([string]$ReportPath, [bool]$FilterEnabled)
{
    if (Test-Path -LiteralPath $ReportPath)
    {
        Remove-Item -LiteralPath $ReportPath -Force
    }
    $arguments = @(
        "-ReflectionHdrDiagnostics", "`"$ReportPath`"",
        "-ReflectionHdrDiagnosticsWarmupFrames", $WarmupFrames,
        "-ReflectionHdrDiagnosticsFrames", $MeasurementFrames,
        "-ReflectionHdrDiagnosticsRoi", $RoiX, $RoiY, $RoiWidth, $RoiHeight,
        "-ReflectionStochasticSampling",
        "-ReflectionTemporalWeight", $TemporalWeight
    )
    if ($FilterEnabled)
    {
        $arguments += "-ReflectionSurfaceVarianceFilter"
    }

    $process = Start-Process -FilePath $executable -ArgumentList $arguments -PassThru -Wait
    if ($process.ExitCode -ne 0)
    {
        throw "HDR diagnostic process failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $ReportPath))
    {
        throw "HDR diagnostic process did not create $ReportPath."
    }
    return Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
}

$off = Invoke-Diagnostic $offPath $false
$on = Invoke-Diagnostic $onPath $true

$offSampling = @($off.frames | ForEach-Object { $_.samplingFrameIndex })
$onSampling = @($on.frames | ForEach-Object { $_.samplingFrameIndex })
$offTemporal = @($off.frames | ForEach-Object { $_.temporalFrameIndex })
$onTemporal = @($on.frames | ForEach-Object { $_.temporalFrameIndex })
$samplingDifferences = @(Compare-Object $offSampling $onSampling)
$temporalDifferences = @(Compare-Object $offTemporal $onTemporal)
$sampleSequenceMatches =
    $offSampling.Count -eq $onSampling.Count -and
    $samplingDifferences.Count -eq 0 -and
    $temporalDifferences.Count -eq 0

$offResolved = $off.statistics.resolvedRadiance
$onResolved = $on.statistics.resolvedRadiance
$offMean = [double]$offResolved.temporalMeanLuminance
$onMean = [double]$onResolved.temporalMeanLuminance
$meanDenominator = [Math]::Max([Math]::Abs($offMean), 1.0e-6)
$offVariance = [double]$offResolved.temporalVariance
$onVariance = [double]$onResolved.temporalVariance

$pairedReport = [ordered]@{
    schemaVersion = 1
    comparison = "surface-variance-filter-off-on"
    signalDomain = "linear-hdr"
    reference = "none"
    currentEstimatorMeanBaseline = "not-generated"
    pairedConditions = [ordered]@{
        separateProcessReset = $true
        cameraAndAnimationFixed = $true
        warmupFrames = $WarmupFrames
        measurementFrames = $MeasurementFrames
        sampleAndTemporalIndexSequencesMatch = $sampleSequenceMatches
        temporalWeight = $TemporalWeight
        stochasticSamplingEnabled = $true
        onlyIntentionalDifference = "surfaceVarianceFilterEnabled"
    }
    roi = $off.roi
    filterOffReport = [System.IO.Path]::GetFileName($offPath)
    filterOnReport = [System.IO.Path]::GetFileName($onPath)
    result = [ordered]@{
        filterOffResolved = $offResolved
        filterOnResolved = $onResolved
        resolvedVarianceReductionPercent = if ($offVariance -gt 0.0) {
            100.0 * (1.0 - $onVariance / $offVariance)
        } else { $null }
        resolvedMeasurementMeanAbsoluteDifference = [Math]::Abs($onMean - $offMean)
        resolvedMeasurementMeanRelativeDifference = [Math]::Abs($onMean - $offMean) / $meanDenominator
        evaluatedMeanAbsoluteDifference = [Math]::Abs(
            [double]$on.statistics.evaluatedRadiance.temporalMeanLuminance -
            [double]$off.statistics.evaluatedRadiance.temporalMeanLuminance)
    }
    limitations = @(
        "The comparison is relative to the current approximate estimator, not a physical ground truth.",
        "A matching sample-index sequence establishes paired scheduling but does not establish estimator correctness.",
        "The 64-frame level is a development gate; 256 frames remain the standard PR evaluation."
    )
}

$json = $pairedReport | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText($output, $json + "`r`n", [System.Text.UTF8Encoding]::new($false))

[PSCustomObject]@{
    sampleSequenceMatches = $sampleSequenceMatches
    varianceReductionPercent = $pairedReport.result.resolvedVarianceReductionPercent
    meanRelativeDifference = $pairedReport.result.resolvedMeasurementMeanRelativeDifference
    report = $output
} | Format-List

if (-not $sampleSequenceMatches)
{
    throw "Filter off/on sample or temporal index sequences do not match."
}
