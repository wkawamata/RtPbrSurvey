[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$cellCount = 128
$solidAngle = 4.0 * [Math]::PI / $cellCount
foreach ($scenario in 'black', 'constant', 'hotspot')
{
    $weights = [double[]]::new($cellCount)
    for ($i = 0; $i -lt $cellCount; ++$i)
    {
        $weights[$i] = if ($scenario -eq 'black') { 0.0 } elseif ($scenario -eq 'hotspot' -and $i -eq 37) { 1000.0 } else { 1.0 }
    }
    $total = ($weights | Measure-Object -Sum).Sum
    $cdf = [double[]]::new($cellCount + 1)
    for ($i = 0; $i -lt $cellCount; ++$i)
    {
        $probability = if ($total -gt 0.0) { 0.95 * $weights[$i] / $total + 0.05 / $cellCount } else { 1.0 / $cellCount }
        $cdf[$i + 1] = $cdf[$i] + $probability
    }
    $cdf[$cellCount] = 1.0
    $normalization = 0.0
    $integral = 0.0
    $secondMoment = 0.0
    $uniformSecondMoment = 0.0
    for ($i = 0; $i -lt $cellCount; ++$i)
    {
        $probability = $cdf[$i + 1] - $cdf[$i]
        if ($probability -le 0.0) { throw "$scenario has an unreachable cell" }
        $pdf = $probability / $solidAngle
        $normalization += $pdf * $solidAngle
        $integral += $probability * $weights[$i] / $pdf
        $secondMoment += $probability * [Math]::Pow($weights[$i] / $pdf, 2)
        $uniformSecondMoment += [Math]::Pow($weights[$i] * 4.0 * [Math]::PI, 2) / $cellCount
    }
    if ([Math]::Abs($normalization - 1.0) -gt 1e-12 -or
        [Math]::Abs($integral - $total * $solidAngle) -gt 1e-8)
    {
        throw "$scenario PDF normalization or piecewise radiance integral failed"
    }
    if ($scenario -eq 'hotspot' -and $secondMoment -ge $uniformSecondMoment)
    {
        throw 'Hotspot importance sampling did not reduce the second moment'
    }
    [pscustomobject]@{ scenario = $scenario; pdfIntegral = $normalization; radianceIntegral = $integral; status = 'passed' }
}
