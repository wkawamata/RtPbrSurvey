param(
    [string]$CaptureDirectory = (Join-Path $PSScriptRoot "captures"),
    [string]$OutputPath = (Join-Path $PSScriptRoot "material-variance-measurement.json"),
    [string]$AnnotatedOutputPath = (Join-Path $PSScriptRoot "captures\material-variance-rois.png")
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$frames = @(195, 210, 225, 240, 255, 270, 285, 300)
$regions = @(
    @{ id = "rearward_surface"; x = 895; y = 278; width = 75; height = 85 },
    @{ id = "underside_pipes"; x = 805; y = 585; width = 125; height = 135 }
)

function Get-Luminance([System.Drawing.Color]$Color)
{
    return (0.2126 * $Color.R + 0.7152 * $Color.G + 0.0722 * $Color.B) / 255.0
}

function Get-Percentile([double[]]$Values, [double]$Percentile)
{
    $sorted = $Values | Sort-Object
    $index = [Math]::Min($sorted.Count - 1, [Math]::Floor($Percentile * $sorted.Count))
    return $sorted[$index]
}

$images = @{}
foreach ($variant in @("evaluated", "resolved"))
{
    $variantImages = @()
    foreach ($frame in $frames)
    {
        $path = Join-Path $CaptureDirectory "material-variance-series-$variant-f$frame.png"
        if (-not (Test-Path -LiteralPath $path))
        {
            throw "Missing capture: $path"
        }
        $variantImages += [System.Drawing.Bitmap]::new($path)
    }
    $images[$variant] = $variantImages
}

try
{
    $measurements = @()
    foreach ($region in $regions)
    {
        $variantMeasurements = @{}
        foreach ($variant in @("evaluated", "resolved"))
        {
            $pixelMeans = [System.Collections.Generic.List[double]]::new()
            $pixelStandardDeviations = [System.Collections.Generic.List[double]]::new()

            for ($y = $region.y; $y -lt $region.y + $region.height; ++$y)
            {
                for ($x = $region.x; $x -lt $region.x + $region.width; ++$x)
                {
                    $samples = [System.Collections.Generic.List[double]]::new()
                    foreach ($image in $images[$variant])
                    {
                        $samples.Add((Get-Luminance $image.GetPixel($x, $y)))
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
                    $standardDeviation = [Math]::Sqrt($sumSquaredDifference / $samples.Count)
                    $pixelMeans.Add($mean)
                    $pixelStandardDeviations.Add($standardDeviation)
                }
            }

            if ($pixelStandardDeviations.Count -eq 0)
            {
                throw "Region $($region.id) contains no non-black pixels for $variant."
            }

            $variantMeasurements[$variant] = [ordered]@{
                validPixelCount = $pixelStandardDeviations.Count
                meanDisplayedLuminance = ($pixelMeans | Measure-Object -Average).Average
                meanTemporalStandardDeviation = ($pixelStandardDeviations | Measure-Object -Average).Average
                p95TemporalStandardDeviation = Get-Percentile $pixelStandardDeviations.ToArray() 0.95
            }
        }

        $evaluatedDeviation = $variantMeasurements.evaluated.meanTemporalStandardDeviation
        $resolvedDeviation = $variantMeasurements.resolved.meanTemporalStandardDeviation
        $measurements += [ordered]@{
            id = $region.id
            rectangle = [ordered]@{
                x = $region.x
                y = $region.y
                width = $region.width
                height = $region.height
            }
            evaluated = $variantMeasurements.evaluated
            resolved = $variantMeasurements.resolved
            resolvedToEvaluatedDeviationRatio = $resolvedDeviation / $evaluatedDeviation
            temporalDeviationReductionPercent = 100.0 * (1.0 - $resolvedDeviation / $evaluatedDeviation)
        }
    }

    $report = [ordered]@{
        version = 1
        measurement = "display-space luminance temporal standard deviation"
        frames = $frames
        nonBlackLuminanceThreshold = 0.01
        notes = @(
            "Rectangles are fixed for the 1920x1080 DamagedHelmet capture at camera distance scale 0.5.",
            "The metric uses screenshot RGB after display mapping; it is a repeatable symptom metric, not HDR radiance variance.",
            "Evaluated and resolved masks are computed independently from non-black pixels within each rectangle."
        )
        regions = $measurements
    }

    $json = $report | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText($OutputPath, $json, [System.Text.UTF8Encoding]::new($false))
    $annotation = [System.Drawing.Bitmap]::new($images.resolved[-1])
    try
    {
        $graphics = [System.Drawing.Graphics]::FromImage($annotation)
        try
        {
            $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::Yellow, 3.0)
            $font = [System.Drawing.Font]::new("Arial", 18.0, [System.Drawing.FontStyle]::Bold)
            $brush = [System.Drawing.Brushes]::Yellow
            try
            {
                foreach ($region in $regions)
                {
                    $graphics.DrawRectangle($pen, $region.x, $region.y, $region.width, $region.height)
                    $graphics.DrawString($region.id, $font, $brush, $region.x, $region.y - 24)
                }
            }
            finally
            {
                $pen.Dispose()
                $font.Dispose()
            }
        }
        finally
        {
            $graphics.Dispose()
        }
        $annotation.Save($AnnotatedOutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally
    {
        $annotation.Dispose()
    }

    $report.regions |
        ForEach-Object { [PSCustomObject]$_ } |
        Select-Object id, temporalDeviationReductionPercent, resolvedToEvaluatedDeviationRatio |
        Format-Table -AutoSize
}
finally
{
    foreach ($variant in $images.Keys)
    {
        foreach ($image in $images[$variant])
        {
            $image.Dispose()
        }
    }
}
