[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
function Get-Weight([double]$A, [double]$B)
{
    $scale = [Math]::Max($A, $B)
    if ($scale -le 0.0) { return 0.0 }
    $x = $A / $scale
    $y = $B / $scale
    return $x * $x / ($x * $x + $y * $y)
}
foreach ($pair in @(@(0.0, 1.0), @(1.0, 0.0), @(0.1, 0.9), @(1e30, 1e-30)))
{
    $sum = (Get-Weight $pair[0] $pair[1]) + (Get-Weight $pair[1] $pair[0])
    if ([Math]::Abs($sum - 1.0) -gt 1e-12) { throw 'MIS weights do not form a partition of unity' }
}
$samples = 65536
$lightMean = 0.0
$bsdfMean = 0.0
$albedo = 0.6
$radiance = 2.0
$lightPdf = 1.0 / (4.0 * [Math]::PI)
for ($i = 0; $i -lt $samples; ++$i)
{
    $u = ($i + 0.5) / $samples
    $cosLight = [Math]::Max(1.0 - 2.0 * $u, 0.0)
    $bsdfPdfAtLight = $cosLight / [Math]::PI
    $lightMean += $albedo * $radiance * $bsdfPdfAtLight / $lightPdf * (Get-Weight $lightPdf $bsdfPdfAtLight)
    $cosBsdf = [Math]::Sqrt(1.0 - $u)
    $bsdfPdf = $cosBsdf / [Math]::PI
    $bsdfMean += $albedo * $radiance * (Get-Weight $bsdfPdf $lightPdf)
}
$estimate = ($lightMean + $bsdfMean) / $samples
if ([Math]::Abs($estimate - $albedo * $radiance) -gt 1e-6)
{
    throw 'Two-technique Lambert integral is biased or double-counted'
}
[pscustomobject]@{ expected = $albedo * $radiance; misEstimate = $estimate; status = 'passed' }
