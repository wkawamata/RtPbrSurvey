[CmdletBinding()]
param(
    [string]$OutputDirectory,
    [ValidateRange(1, 4096)]
    [int]$Samples = 4,
    [uint32]$Seed = 7,
    [ValidateRange(1, 600)]
    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = Join-Path $root 'bin/x64/Debug/RtPbrSurvey.exe'
$sourceScene = Join-Path $root 'Assets/Scenes/MultiLightValidation/scene.json'
$sourcePreset = Join-Path $root 'Assets/Scenes/MultiLightValidation/render-preset.json'
if (-not $OutputDirectory)
{
    $OutputDirectory = Join-Path $root 'bin/x64/Debug/PathTracingPointFalloff'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

function Read-CenterPfmMean([string]$Path)
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
        $dataOffset = $stream.Position
        if ($width -le 48 -or $height -le 48 -or
            [long]$width * $height * 3 * 4 -ne ($stream.Length - $dataOffset))
        {
            throw "Invalid PFM dimensions or length: $Path"
        }
        $sum = 0.0
        $count = 0
        for ($y = [int]($height / 2) - 8; $y -lt [int]($height / 2) + 8; $y += 2)
        {
            for ($x = [int]($width / 2) - 8; $x -lt [int]($width / 2) + 8; $x += 2)
            {
                $offset = $dataOffset + ([long]$y * $width + $x) * 12
                $stream.Seek($offset, [IO.SeekOrigin]::Begin) | Out-Null
                for ($channel = 0; $channel -lt 3; ++$channel)
                {
                    $value = [double]$reader.ReadSingle()
                    if (-not [double]::IsFinite($value))
                    {
                        throw "Non-finite HDR value in $Path"
                    }
                    $sum += $value
                    ++$count
                }
            }
        }
        return $sum / $count
    }
    finally
    {
        $stream.Dispose()
    }
}

$scene = Get-Content -LiteralPath $sourceScene -Raw | ConvertFrom-Json -AsHashtable
foreach ($node in $scene.nodes)
{
    if ($node.id -in @('smooth', 'rough', 'diffuse'))
    {
        $node.translation = @(100, 100, 100)
    }
}
$scene.nodes = @($scene.nodes | Where-Object { $_.id -ne 'blocker' })
$scene.camera.position = @(0, 4, -7)
$scene.camera.target = @(0, 0, 0)
$scene.renderPreset = 'near-preset.json'
$scenePath = Join-Path $OutputDirectory 'scene.json'
[IO.File]::WriteAllText($scenePath, ($scene | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))

$variants = [ordered]@{
    near = @{ Height = 2.0; Range = 20.0 }
    far = @{ Height = 4.0; Range = 20.0 }
    narrowRange = @{ Height = 2.0; Range = 5.0 }
}
$captures = [ordered]@{}
foreach ($variant in $variants.Keys)
{
    $preset = Get-Content -LiteralPath $sourcePreset -Raw | ConvertFrom-Json -AsHashtable
    $light = $preset.lighting.lights[1]
    $light.position = @(0, $variants[$variant].Height, 0)
    $light.range = $variants[$variant].Range
    $preset.lighting.lights = @($light)
    $preset.lighting.primaryShadowLightId = 0
    $preset.lighting.skyboxEnabled = $false
    $preset.lighting.diffuseIblEnabled = $false
    $preset.lighting.specularIblEnabled = $false
    $preset.lighting.emissiveEnabled = $false
    $preset.shadow.enabled = $false
    $preset.pathTracing = @{
        maxBounces = 1
        directLightingEnabled = $true
        environmentEnabled = $false
        emissiveEnabled = $false
        russianRouletteEnabled = $false
    }
    $presetPath = Join-Path $OutputDirectory "$variant-preset.json"
    $capturePath = Join-Path $OutputDirectory "$variant.pfm"
    $logPath = Join-Path $OutputDirectory "$variant.log"
    [IO.File]::WriteAllText($presetPath, ($preset | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
    Remove-Item -LiteralPath $capturePath, $logPath -Force -ErrorAction SilentlyContinue
    $arguments = @(
        '-SceneFile', "`"$scenePath`"", '-RenderPreset', "`"$presetPath`"",
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
        height = $variants[$variant].Height
        range = $variants[$variant].Range
        meanHdr = Read-CenterPfmMean $capturePath
        sha256 = (Get-FileHash -LiteralPath $capturePath -Algorithm SHA256).Hash
        diagnostics = $diagnostics
    }
}

function Falloff([double]$Height, [double]$Range)
{
    $window = [Math]::Max(0.0, 1.0 - [Math]::Pow($Height / $Range, 4))
    return $window * $window / ($Height * $Height)
}

$measuredHeightRatio = $captures.near.meanHdr / $captures.far.meanHdr
$expectedHeightRatio = (Falloff 2.0 20.0) / (Falloff 4.0 20.0)
$measuredRangeRatio = $captures.narrowRange.meanHdr / $captures.near.meanHdr
$expectedRangeRatio = (Falloff 2.0 5.0) / (Falloff 2.0 20.0)
$report = [ordered]@{
    samples = $Samples
    seed = $Seed
    roi = @(952, 532, 16, 16)
    measuredHeightRatio = $measuredHeightRatio
    expectedHeightRatio = $expectedHeightRatio
    heightRelativeError = [Math]::Abs($measuredHeightRatio / $expectedHeightRatio - 1.0)
    measuredRangeRatio = $measuredRangeRatio
    expectedRangeRatio = $expectedRangeRatio
    rangeRelativeError = [Math]::Abs($measuredRangeRatio / $expectedRangeRatio - 1.0)
    captures = $captures
}
$reportPath = Join-Path $OutputDirectory 'report.json'
[IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 100), [Text.UTF8Encoding]::new($false))
Write-Output "Height ratio: measured=$measuredHeightRatio expected=$expectedHeightRatio error=$($report.heightRelativeError)"
Write-Output "Range ratio: measured=$measuredRangeRatio expected=$expectedRangeRatio error=$($report.rangeRelativeError)"
Write-Output "Report: $reportPath"
if ($captures.near.meanHdr -le 0.0 -or $captures.far.meanHdr -le 0.0 -or
    $report.heightRelativeError -gt 0.02 -or $report.rangeRelativeError -gt 0.02)
{
    throw 'Point-light falloff ratio exceeded tolerance.'
}
