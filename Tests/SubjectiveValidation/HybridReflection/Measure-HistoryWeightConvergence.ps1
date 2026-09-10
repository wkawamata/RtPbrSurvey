param(
    [string]$CaptureDirectory = (Join-Path $PSScriptRoot "captures"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "material-variance-history-weight.json")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$frames = @(195, 210, 225, 240, 255, 270, 285, 300)
$variants = @(
    @{ id = "evaluated"; fileVariant = "evaluated"; historyWeight = $null },
    @{ id = "w0"; fileVariant = "w0"; historyWeight = 0.0 },
    @{ id = "w50"; fileVariant = "w50"; historyWeight = 0.5 },
    @{ id = "w90"; fileVariant = "resolved"; historyWeight = 0.9 },
    @{ id = "w98"; fileVariant = "w98"; historyWeight = 0.98 }
)
$windows = @(
    @{ id = "full"; indices = @(0, 1, 2, 3, 4, 5, 6, 7) },
    @{ id = "early"; indices = @(0, 1, 2) },
    @{ id = "late"; indices = @(5, 6, 7) }
)
$regions = @(
    @{ id = "rearward_surface"; x = 895; y = 278; width = 75; height = 85 },
    @{ id = "underside_pipes"; x = 805; y = 585; width = 125; height = 135 }
)

function Get-Luminance([System.Drawing.Color]$Color)
{
    return (0.2126 * $Color.R + 0.7152 * $Color.G + 0.0722 * $Color.B) / 255.0
}

function Measure-Window($Images, $Indices, $Region)
{
    $pixelMeans = [System.Collections.Generic.List[double]]::new()
    $pixelDeviations = [System.Collections.Generic.List[double]]::new()

    for ($y = $Region.y; $y -lt $Region.y + $Region.height; ++$y)
    {
        for ($x = $Region.x; $x -lt $Region.x + $Region.width; ++$x)
        {
            $samples = [System.Collections.Generic.List[double]]::new()
            foreach ($index in $Indices)
            {
                $samples.Add((Get-Luminance $Images[$index].GetPixel($x, $y)))
            }

            $mean = ($samples | Measure-Object -Average).Average
            if ($mean -le 0.01)
            {
                continue
            }

            $sumSquaredDifference = 0.0
            foreach ($sample in $samples)
            {
                $difference = $sample - $mean
                $sumSquaredDifference += $difference * $difference
            }
            $pixelMeans.Add($mean)
            $pixelDeviations.Add([Math]::Sqrt($sumSquaredDifference / $samples.Count))
        }
    }

    if ($pixelDeviations.Count -eq 0)
    {
        throw "Region $($Region.id) contains no non-black pixels."
    }

    return [ordered]@{
        validPixelCount = $pixelDeviations.Count
        meanDisplayedLuminance = ($pixelMeans | Measure-Object -Average).Average
        meanTemporalStandardDeviation = ($pixelDeviations | Measure-Object -Average).Average
    }
}

$images = @{}
foreach ($variant in $variants)
{
    $variantImages = @()
    foreach ($frame in $frames)
    {
        $path = Join-Path $CaptureDirectory "material-variance-series-$($variant.fileVariant)-f$frame.png"
        if (-not (Test-Path -LiteralPath $path))
        {
            throw "Missing capture: $path"
        }
        $variantImages += [System.Drawing.Bitmap]::new($path)
    }
    $images[$variant.id] = $variantImages
}

try
{
    $regionReports = @()
    foreach ($region in $regions)
    {
        $variantReports = @()
        foreach ($variant in $variants)
        {
            $windowReports = [ordered]@{}
            foreach ($window in $windows)
            {
                $windowReports[$window.id] = Measure-Window $images[$variant.id] $window.indices $region
            }

            $early = $windowReports.early
            $late = $windowReports.late
            $variantReports += [ordered]@{
                id = $variant.id
                historyWeight = $variant.historyWeight
                full = $windowReports.full
                early = $early
                late = $late
                lateToEarlyDeviationRatio = $late.meanTemporalStandardDeviation / $early.meanTemporalStandardDeviation
                lateMinusEarlyMeanDisplayedLuminance = $late.meanDisplayedLuminance - $early.meanDisplayedLuminance
            }
        }

        $regionReports += [ordered]@{
            id = $region.id
            rectangle = [ordered]@{
                x = $region.x
                y = $region.y
                width = $region.width
                height = $region.height
            }
            variants = $variantReports
        }
    }

    $report = [ordered]@{
        version = 1
        measurement = "history-weight display-space temporal convergence"
        frames = $frames
        windows = [ordered]@{
            early = @($frames[0], $frames[1], $frames[2])
            late = @($frames[5], $frames[6], $frames[7])
        }
        notes = @(
            "The camera stops at frame 180; all measured frames use the same fixed view.",
            "The metric uses screenshot RGB after display mapping and is not HDR radiance variance.",
            "Early and late temporal deviations measure stability, while luminance drift helps expose slow settling.",
            "The evaluated variant has no history weight and is included as the unaccumulated reference."
        )
        regions = $regionReports
    }

    $json = $report | ConvertTo-Json -Depth 10
    [System.IO.File]::WriteAllText($OutputPath, $json, [System.Text.UTF8Encoding]::new($false))

    foreach ($regionReport in $report.regions)
    {
        Write-Output $regionReport.id
        $regionReport.variants |
            ForEach-Object { [PSCustomObject]$_ } |
            Select-Object id, historyWeight,
                @{ Name = "fullDeviation"; Expression = { $_.full.meanTemporalStandardDeviation } },
                @{ Name = "earlyDeviation"; Expression = { $_.early.meanTemporalStandardDeviation } },
                @{ Name = "lateDeviation"; Expression = { $_.late.meanTemporalStandardDeviation } },
                lateToEarlyDeviationRatio,
                lateMinusEarlyMeanDisplayedLuminance |
            Format-Table -AutoSize
    }
}
finally
{
    foreach ($variantId in $images.Keys)
    {
        foreach ($image in $images[$variantId])
        {
            $image.Dispose()
        }
    }
}
