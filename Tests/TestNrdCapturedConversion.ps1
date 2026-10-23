param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Shader,
    [Parameter(Mandatory = $true)][string]$ScratchRoot,
    [switch]$Hardware
)
$ErrorActionPreference = 'Stop'
$folder = Join-Path $ScratchRoot ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $folder -Force | Out-Null
$pixel = [float[]]@(0, 0, 1, .5, 0, 0, 1, 0, .8, .6, .2, 1, .04, .04, .04, 0,
    .2, 1.2, 1.2, 0, .5, 1, 3, 0, 2, 4, 1, 0)
$inputs = [float[]]::new(17 * 28)
for ($i = 0; $i -lt 17; ++$i)
{
    [Array]::Copy($pixel, 0, $inputs, $i * 28, 28)
}
$inputs[16 * 28 + 26] = 0
$bytes = [byte[]]::new($inputs.Length * 4)
[Buffer]::BlockCopy($inputs, 0, $bytes, 0, $bytes.Length)
$inputPath = Join-Path $folder 'input.raw'
$outputPath = Join-Path $folder 'output.raw'
[IO.File]::WriteAllBytes($inputPath, $bytes)
$arguments = @($Shader, '--input-conversion', '--input', $inputPath, '--output', $outputPath)
if ($Hardware) { $arguments += '--hardware' }
& $Exe @arguments
if ($LASTEXITCODE -ne 0) { throw 'Captured conversion failed.' }
$outputBytes = [IO.File]::ReadAllBytes($outputPath)
if ($outputBytes.Length -ne 17 * 64) { throw 'Wrong captured output size.' }
$output = [float[]]::new(17 * 16)
[Buffer]::BlockCopy($outputBytes, 0, $output, 0, $outputBytes.Length)
for ($i = 0; $i -lt 17; ++$i)
{
    $offset = $i * 16
    for ($channel = 0; $channel -lt 16; ++$channel)
    {
        if ([float]::IsNaN($output[$offset + $channel]) -or [float]::IsInfinity($output[$offset + $channel]))
        { throw 'Nonfinite captured output.' }
    }
    if ($i -eq 16)
    {
        for ($channel = 0; $channel -lt 16; ++$channel)
        {
            $expected = if ($channel -in @(11, 15)) { 4 } else { 0 }
            if ($output[$offset + $channel] -ne $expected) { throw 'Primary miss was not excluded.' }
        }
        continue
    }
    if ($output[$offset + 11] -ne 0 -or $output[$offset + 15] -ne 0 -or
        $output[$offset + 3] -ne 2 -or $output[$offset + 7] -ne 4)
    { throw 'Wrong captured conversion status or distance.' }
    for ($channel = 0; $channel -lt 3; ++$channel)
    {
        if ([Math]::Abs($output[$offset + $channel] * $output[$offset + 8 + $channel] - $pixel[16 + $channel]) -gt .00001 -or
            [Math]::Abs($output[$offset + 4 + $channel] * $output[$offset + 12 + $channel] - $pixel[20 + $channel]) -gt .00001)
        { throw 'Captured RGB round trip failed.' }
    }
}
Write-Output '17-pixel captured conversion smoke test passed, including dispatch-tail guard.'
