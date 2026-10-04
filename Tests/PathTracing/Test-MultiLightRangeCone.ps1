[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidateRange(1, 4096)]
    [int]$Samples = 1,
    [uint32]$Seed = 1,
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = Join-Path $root 'bin/x64/Debug/RtPbrSurvey.exe'
$scene = Join-Path $root 'Assets/Scenes/MultiLightValidation/scene.json'
$sourcePreset = Join-Path $root 'Assets/Scenes/MultiLightValidation/render-preset.json'
if (-not $OutputDirectory)
{
    $OutputDirectory = Join-Path $root 'bin/x64/Debug/PathTracingMultiLightRangeCone'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$variants = @('none', 'point', 'pointShortRange', 'spot', 'spotShortRange', 'spotUp')
$captures = [ordered]@{}
foreach ($variant in $variants)
{
    $preset = Get-Content -LiteralPath $sourcePreset -Raw | ConvertFrom-Json -AsHashtable
    switch ($variant)
    {
        'none' { $preset.lighting.lights = @() }
        { $_ -like 'point*' } { $preset.lighting.lights = @($preset.lighting.lights[1]) }
        { $_ -like 'spot*' } { $preset.lighting.lights = @($preset.lighting.lights[3]) }
    }
    if ($variant -eq 'pointShortRange' -or $variant -eq 'spotShortRange')
    {
        $preset.lighting.lights[0].range = 0.1
    }
    if ($variant -eq 'spotUp')
    {
        $preset.lighting.lights[0].direction = @(0, 1, 0)
    }
    $preset.lighting.primaryShadowLightId = 0
    $preset.lighting.skyboxEnabled = $false
    $preset.lighting.diffuseIblEnabled = $false
    $preset.lighting.specularIblEnabled = $false
    $preset.lighting.emissiveEnabled = $false
    $preset.pathTracing = @{
        maxBounces = 1
        directLightingEnabled = $true
        environmentEnabled = $false
        emissiveEnabled = $false
        russianRouletteEnabled = $false
    }
    $presetPath = Join-Path $OutputDirectory "$variant.json"
    $capturePath = Join-Path $OutputDirectory "$variant.pfm"
    $logPath = Join-Path $OutputDirectory "$variant.log"
    [IO.File]::WriteAllText($presetPath, ($preset | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
    Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue
    $arguments = @(
        '-SceneFile', "`"$scene`"", '-RenderPreset', "`"$presetPath`"",
        '-EnablePathTracing', '-PathTracingSamples', $Samples, '-PathTracingSeed', $Seed,
        '-CapturePath', "`"$capturePath`"", '-LogToFile', "`"$logPath`"", '-ExitAfterCapture'
    )
    $process = Start-Process -FilePath $exe -ArgumentList ($arguments -join ' ') -WorkingDirectory $root -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit($TimeoutSeconds * 1000))
    {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        throw "Timed out: $variant"
    }
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capturePath))
    {
        throw "Capture failed: $variant (exit $($process.ExitCode))"
    }
    $logLines = @(Get-Content -LiteralPath $logPath)
    if (@($logLines | Where-Object { $_ -match '^\[(ERROR|CORRUPTION)\]' }).Count -ne 0)
    {
        throw "D3D12 error: $variant"
    }
    $diagnostic = @($logLines | Where-Object { $_.StartsWith('[PathTracing] ') }) | Select-Object -Last 1
    if (-not $diagnostic)
    {
        throw "Missing Path Tracing diagnostics: $variant"
    }
    $diagnostics = $diagnostic.Substring('[PathTracing] '.Length) | ConvertFrom-Json
    if ($diagnostics.accumulatedSamples -ne $Samples -or $diagnostics.randomSeed -ne $Seed -or $diagnostics.maxBounces -ne 1)
    {
        throw "Unexpected Path Tracing settings: $variant"
    }
    $captures[$variant] = [ordered]@{
        sha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        diagnostics = $diagnostics
    }
}

$report = [ordered]@{
    samples = $Samples
    seed = $Seed
    noneEqualsPointShortRange = $captures.none.sha256 -eq $captures.pointShortRange.sha256
    noneEqualsSpotShortRange = $captures.none.sha256 -eq $captures.spotShortRange.sha256
    noneEqualsSpotUp = $captures.none.sha256 -eq $captures.spotUp.sha256
    pointContributes = $captures.none.sha256 -ne $captures.point.sha256
    spotContributes = $captures.none.sha256 -ne $captures.spot.sha256
    captures = $captures
}
$reportPath = Join-Path $OutputDirectory 'report.json'
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
Write-Output "Point active=$($report.pointContributes), short range zero=$($report.noneEqualsPointShortRange)"
Write-Output "Spot active=$($report.spotContributes), short range zero=$($report.noneEqualsSpotShortRange), up zero=$($report.noneEqualsSpotUp)"
Write-Output "Report: $reportPath"
if (-not ($report.noneEqualsPointShortRange -and $report.noneEqualsSpotShortRange -and $report.noneEqualsSpotUp -and
          $report.pointContributes -and $report.spotContributes))
{
    throw 'Point/Spot range or cone validation failed.'
}
