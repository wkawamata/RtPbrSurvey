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
    $OutputDirectory = Join-Path $root 'bin/x64/Debug/PathTracingMultiLightAdditivity'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

function Read-Pfm([string]$Path)
{
    $stream = [IO.File]::OpenRead($Path)
    try
    {
        $reader = [IO.BinaryReader]::new($stream)
        $lines = @()
        for ($lineIndex = 0; $lineIndex -lt 3; ++$lineIndex)
        {
            $bytes = [System.Collections.Generic.List[byte]]::new()
            while ($true)
            {
                $value = $reader.ReadByte()
                if ($value -eq 10) { break }
                if ($value -ne 13) { $bytes.Add($value) }
            }
            $lines += [Text.Encoding]::ASCII.GetString($bytes.ToArray())
        }
        if ($lines[0] -ne 'PF' -or $lines[2] -ne '-1.0')
        {
            throw "Unexpected PFM header: $Path"
        }
        $dimensions = $lines[1] -split ' '
        $width = [int]$dimensions[0]
        $height = [int]$dimensions[1]
        if ($width -le 0 -or $height -le 0 -or [long]$width * $height * 3 -gt [int]::MaxValue / 4)
        {
            throw "Invalid PFM dimensions: $Path"
        }
        $count = $width * $height * 3
        $bytes = $reader.ReadBytes($count * 4)
        if ($bytes.Length -ne $count * 4 -or $stream.Position -ne $stream.Length)
        {
            throw "Invalid PFM byte count: $Path"
        }
        $values = [float[]]::new($count)
        [Buffer]::BlockCopy($bytes, 0, $values, 0, $bytes.Length)
        return @{ Width = $width; Height = $height; Values = $values }
    }
    finally
    {
        $stream.Dispose()
    }
}

$base = Get-Content -LiteralPath $sourcePreset -Raw | ConvertFrom-Json -AsHashtable
$lights = @($base.lighting.lights)
if ($lights.Count -ne 4)
{
    throw 'Expected the four-light validation fixture.'
}
$variants = [ordered]@{ none = @(); directional = @(0); pointA = @(1); pointB = @(2); spot = @(3); all = @(0, 1, 2, 3) }
$captures = [ordered]@{}
foreach ($variant in $variants.Keys)
{
    $preset = Get-Content -LiteralPath $sourcePreset -Raw | ConvertFrom-Json -AsHashtable
    $preset.lighting.lights = @($variants[$variant] | ForEach-Object { $lights[$_] })
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
    $captures[$variant] = @{ Path = $capturePath; Hash = (Get-FileHash $capturePath -Algorithm SHA256).Hash; Diagnostics = $diagnostics }
}

$baseline = Read-Pfm $captures.none.Path
$combined = Read-Pfm $captures.all.Path
if ($baseline.Width -ne $combined.Width -or $baseline.Height -ne $combined.Height)
{
    throw 'PFM dimensions differ.'
}
$sampleIndices = [System.Collections.Generic.List[int]]::new()
for ($y = 0; $y -lt $baseline.Height; $y += 8)
{
    for ($x = 0; $x -lt $baseline.Width; $x += 8)
    {
        $pixel = ($y * $baseline.Width + $x) * 3
        $sampleIndices.Add($pixel)
        $sampleIndices.Add($pixel + 1)
        $sampleIndices.Add($pixel + 2)
    }
}
$expected = [double[]]::new($sampleIndices.Count)
$signal = [ordered]@{}
foreach ($variant in @('directional', 'pointA', 'pointB', 'spot'))
{
    $image = Read-Pfm $captures[$variant].Path
    if ($image.Width -ne $baseline.Width -or $image.Height -ne $baseline.Height)
    {
        throw "PFM dimensions differ: $variant"
    }
    $sum = 0.0
    for ($i = 0; $i -lt $expected.Length; ++$i)
    {
        $sourceIndex = $sampleIndices[$i]
        $difference = [double]$image.Values[$sourceIndex] - [double]$baseline.Values[$sourceIndex]
        $expected[$i] += $difference
        $sum += [Math]::Abs($difference)
    }
    $signal[$variant] = $sum / $expected.Length
}

$sumSquared = 0.0
$maxError = 0.0
$sumSignal = 0.0
for ($i = 0; $i -lt $expected.Length; ++$i)
{
    $sourceIndex = $sampleIndices[$i]
    $actual = [double]$combined.Values[$sourceIndex] - [double]$baseline.Values[$sourceIndex]
    if (-not [double]::IsFinite($actual) -or -not [double]::IsFinite($expected[$i]))
    {
        throw "Non-finite PFM value at float index $i"
    }
    $error = [Math]::Abs($actual - $expected[$i])
    $sumSquared += $error * $error
    $maxError = [Math]::Max($maxError, $error)
    $sumSignal += [Math]::Abs($expected[$i])
}
$report = [ordered]@{
    samples = $Samples
    seed = $Seed
    dimensions = @($baseline.Width, $baseline.Height)
    sampledFloatCount = $sampleIndices.Count
    rmse = [Math]::Sqrt($sumSquared / $expected.Length)
    maxAbsoluteError = $maxError
    meanAbsoluteSignal = $sumSignal / $expected.Length
    perLightMeanAbsoluteSignal = $signal
    captures = $captures
}
$reportPath = Join-Path $OutputDirectory 'report.json'
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
Write-Output "RMSE=$($report.rmse), max=$($report.maxAbsoluteError), mean signal=$($report.meanAbsoluteSignal), report=$reportPath"
foreach ($variant in $signal.Keys)
{
    if ($signal[$variant] -le 1e-6)
    {
        throw "No visible contribution from $variant; see $reportPath"
    }
}
if ($report.rmse -gt 1e-4 * [Math]::Max($report.meanAbsoluteSignal, 1e-6))
{
    throw "Direct-light additivity exceeded tolerance; see $reportPath"
}
