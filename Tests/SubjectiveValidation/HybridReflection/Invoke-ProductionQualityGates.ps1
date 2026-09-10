[CmdletBinding()]
param(
    [string]$ExecutablePath = (Join-Path $PSScriptRoot "..\..\..\bin\x64\Debug\RtPbrSurvey.exe"),
    [string]$ProfilesPath = (Join-Path $PSScriptRoot "production-quality-gate-profiles.json"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "reports\production-quality-gates.json"),
    [string[]]$ProfileId,
    [ValidateSet(64, 256, 1024)]
    [int]$MeasurementFrames = 64,
    [int]$WarmupFrames = 32
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$profilesDocument = Get-Content -LiteralPath $ProfilesPath -Raw -Encoding UTF8 | ConvertFrom-Json
$selectedProfiles = @($profilesDocument.profiles)
if ($null -ne $ProfileId -and $ProfileId.Count -gt 0)
{
    $requested = @($ProfileId | Sort-Object -Unique)
    $selectedProfiles = @($selectedProfiles | Where-Object { $requested -contains $_.id })
    $missing = @($requested | Where-Object { $_ -notin $selectedProfiles.id })
    if ($missing.Count -gt 0)
    {
        throw "Unknown production quality gate profile(s): $($missing -join ', ')."
    }
}

if ($selectedProfiles.Count -eq 0)
{
    throw "No production quality gate profiles were selected."
}

$output = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $output
if (-not (Test-Path -LiteralPath $outputDirectory))
{
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

$pairScript = Join-Path $PSScriptRoot "Measure-HdrVariancePair.ps1"
$runDirectory = Join-Path $outputDirectory ([System.IO.Path]::GetFileNameWithoutExtension($output))
if (-not (Test-Path -LiteralPath $runDirectory))
{
    New-Item -ItemType Directory -Path $runDirectory | Out-Null
}

function Get-RelativeChange([double]$Before, [double]$After)
{
    $denominator = [Math]::Max([Math]::Abs($Before), 1.0e-12)
    return 100.0 * ($After - $Before) / $denominator
}

$results = @()
foreach ($profile in $selectedProfiles)
{
    $pairPath = Join-Path $runDirectory "$($profile.id)-$MeasurementFrames-paired.json"
    $cameraDistanceScaleProperty = $profile.PSObject.Properties["cameraDistanceScale"]
    $cameraDistanceScale = if ($null -ne $cameraDistanceScaleProperty)
    {
        [double]$cameraDistanceScaleProperty.Value
    }
    else
    {
        1.0
    }

    & $pairScript `
        -ExecutablePath $ExecutablePath `
        -OutputPath $pairPath `
        -WarmupFrames $WarmupFrames `
        -MeasurementFrames $MeasurementFrames `
        -RoiX $profile.roi.x `
        -RoiY $profile.roi.y `
        -RoiWidth $profile.roi.width `
        -RoiHeight $profile.roi.height `
        -Scene $profile.scene `
        -CameraDistanceScale $cameraDistanceScale `
        -CompareSpatiotemporalPolicy

    $pair = Get-Content -LiteralPath $pairPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $result = $pair.result
    $invariantChecks = [ordered]@{
        sampleAndTemporalIndexSequencesMatch =
            [bool]$pair.pairedConditions.sampleAndTemporalIndexSequencesMatch
        resolvedControlVarianceMatches =
            [Math]::Abs([double]$result.resolvedControlVarianceRelativeDifference) -le
            [double]$profilesDocument.thresholds.maximumResolvedControlVarianceRelativeDifference
        meanPreserved =
            [double]$result.denoisedMeasurementMeanRelativeDifference -le
            [double]$profilesDocument.thresholds.maximumMeanRelativeDifference
    }
    $passed = @($invariantChecks.Values | Where-Object { -not $_ }).Count -eq 0

    $results += [ordered]@{
        profileId = $profile.id
        description = $profile.description
        scene = $profile.scene
        roi = $profile.roi
        cameraDistanceScale = $cameraDistanceScale
        status = if ($passed) { "PASS" } else { "FAIL" }
        invariantChecks = $invariantChecks
        observations = [ordered]@{
            meanRelativeDifference = [double]$result.denoisedMeasurementMeanRelativeDifference
            varianceChangeBRelativeToAPercent =
                Get-RelativeChange $result.filterOffDenoised.temporalVariance $result.filterOnDenoised.temporalVariance
            frameDifferenceMeanChangeBRelativeToAPercent =
                Get-RelativeChange $result.filterOffDenoised.frameAbsoluteDifferenceMean $result.filterOnDenoised.frameAbsoluteDifferenceMean
            frameDifferenceP95ChangeBRelativeToAPercent =
                Get-RelativeChange $result.filterOffDenoised.frameAbsoluteDifferenceP95 $result.filterOnDenoised.frameAbsoluteDifferenceP95
            frameDifferenceP99ChangeBRelativeToAPercent =
                Get-RelativeChange $result.filterOffDenoised.frameAbsoluteDifferenceP99 $result.filterOnDenoised.frameAbsoluteDifferenceP99
        }
        pairedReport = [System.IO.Path]::GetRelativePath($outputDirectory, $pairPath)
    }
}

$failedProfiles = @($results | Where-Object { $_.status -eq "FAIL" })
$report = [ordered]@{
    schemaVersion = 1
    generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    comparison = "fixed-spatial-filter-vs-bounded-spatiotemporal-policy"
    signalDomain = "linear-hdr"
    physicalReference = $false
    warmupFrames = $WarmupFrames
    measurementFrames = $MeasurementFrames
    thresholds = $profilesDocument.thresholds
    overallStatus = if ($failedProfiles.Count -eq 0) { "PASS" } else { "FAIL" }
    results = $results
    limitations = @(
        "Invariant PASS establishes paired scheduling, unchanged resolved input, and bounded mean change only.",
        "Variance and frame-difference changes are observations and require a declared quality interpretation.",
        "The current-estimator mean is not a physical ground truth."
    )
}

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($output, ($report | ConvertTo-Json -Depth 10), $utf8NoBom)
$report | ConvertTo-Json -Depth 5

if ($failedProfiles.Count -gt 0)
{
    exit 1
}
