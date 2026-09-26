[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$pi = [Math]::PI
$pdf = 1.0 / (4.0 * $pi)
$sampleCount = 65536
$albedo = 0.6
$incidentRadiance = 2.0
$sum = 0.0
$squareSum = 0.0
for ($index = 0; $index -lt $sampleCount; ++$index)
{
    $z = 1.0 - 2.0 * (($index + 0.5) / $sampleCount)
    $estimate = ($albedo / $pi) * $incidentRadiance * [Math]::Max($z, 0.0) / $pdf
    $sum += $estimate
    $squareSum += $estimate * $estimate
}
$mean = $sum / $sampleCount
$expected = $albedo * $incidentRadiance
if ([Math]::Abs($pdf * 4.0 * $pi - 1.0) -gt 1e-12 -or
    [Math]::Abs($mean - $expected) -gt 1e-10)
{
    throw 'Uniform-sphere PDF normalization or Lambert integral failed.'
}
$variance = $squareSum / $sampleCount - $mean * $mean
if ([Math]::Abs($variance - (5.0 / 3.0) * $expected * $expected) -gt 1e-7)
{
    throw 'Uniform-sphere Lambert estimator variance failed.'
}
[pscustomobject]@{
    pdfIntegral = $pdf * 4.0 * $pi
    lambertExpected = $expected
    lambertEstimate = $mean
    variance = $variance
    status = 'passed'
}
