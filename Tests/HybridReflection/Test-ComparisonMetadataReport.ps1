[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReportPath,
    [string]$SourceRevision,
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$report = Get-Content -LiteralPath $ReportPath -Raw -Encoding UTF8 | ConvertFrom-Json
$failures = [System.Collections.Generic.List[string]]::new()

function Add-FailureIf([bool]$Condition, [string]$Message)
{
    if ($Condition)
    {
        $failures.Add($Message)
    }
}

function Test-PositiveSize($Size)
{
    return $null -ne $Size -and [int]$Size.width -gt 0 -and [int]$Size.height -gt 0
}

if ([string]::IsNullOrWhiteSpace($SourceRevision))
{
    $repositoryRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
    $SourceRevision = (& git -C $repositoryRoot rev-parse HEAD).Trim()
}

Add-FailureIf ($report.schemaVersion -ne 15) "schemaVersion must be 15"
Add-FailureIf ($report.signalDomain -ne "linear-hdr") "signalDomain must be linear-hdr"
Add-FailureIf ($report.reference -ne "none") "reference must remain none unless an explicit reference contract is supplied"

$metadata = $report.comparisonMetadata
Add-FailureIf ($null -eq $metadata) "comparisonMetadata is required"
if ($null -ne $metadata)
{
    Add-FailureIf ($metadata.renderingPath -notin @("forward", "deferred")) "renderingPath must be forward or deferred"
    Add-FailureIf (-not (Test-PositiveSize $report.renderSize)) "renderSize must be positive"
    Add-FailureIf (-not (Test-PositiveSize $metadata.outputSize)) "outputSize must be positive"
    Add-FailureIf (
        $metadata.signalBoundaries.evaluatedRadiance -ne "current-reflection-unweighted-linear-hdr") `
        "evaluatedRadiance comparison boundary is invalid"
    Add-FailureIf (
        $metadata.signalBoundaries.resolvedRadiance -ne "resolved-reflection-unweighted-linear-hdr") `
        "resolvedRadiance comparison boundary is invalid"
    Add-FailureIf (
        $metadata.signalBoundaries.denoisedRadiance -ne "spatial-reflection-unweighted-linear-hdr") `
        "denoisedRadiance comparison boundary is invalid"
    Add-FailureIf ($metadata.camera.projection -notin @("perspective", "orthographic")) "camera projection is invalid"
    Add-FailureIf ($null -eq $metadata.presentation.exposure) "presentation exposure is required"
    Add-FailureIf ($null -eq $metadata.presentation.toneMapOperator) "tone-map operator is required"
}

Add-FailureIf ([string]::IsNullOrWhiteSpace($report.scene)) "scene is required"
Add-FailureIf ([int]$report.measurementFrames -le 0) "measurementFrames must be positive"
Add-FailureIf ($null -eq $report.stochasticSamplingEnabled) "stochasticSamplingEnabled is required"
Add-FailureIf ($null -eq $report.hitNormalSource) "hitNormalSource is required"

$manifest = [ordered]@{
    schemaVersion = 1
    sourceRevision = $SourceRevision
    diagnosticReport = [System.IO.Path]::GetFullPath($ReportPath)
    diagnosticSchemaVersion = $report.schemaVersion
    scene = $report.scene
    signalDomain = $report.signalDomain
    renderingPath = $metadata.renderingPath
    renderSize = $report.renderSize
    outputSize = $metadata.outputSize
    exposure = $metadata.presentation.exposure
    status = if ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
    failures = @($failures)
}

if (-not [string]::IsNullOrWhiteSpace($OutputPath))
{
    $output = [System.IO.Path]::GetFullPath($OutputPath)
    $outputDirectory = Split-Path -Parent $output
    if (-not (Test-Path -LiteralPath $outputDirectory))
    {
        New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    }
    [System.IO.File]::WriteAllText(
        $output,
        ($manifest | ConvertTo-Json -Depth 6) + "`r`n",
        [System.Text.UTF8Encoding]::new($false))
}

$manifest | ConvertTo-Json -Depth 6
if ($failures.Count -ne 0)
{
    exit 1
}
