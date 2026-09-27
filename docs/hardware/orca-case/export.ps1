# Rebuild the four printable STLs from orca_bottlenose_case.scad
$ErrorActionPreference = "Stop"
$exe = "C:\Program Files\OpenSCAD\openscad.com"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $here "orca_bottlenose_case.scad"
if (-not (Test-Path $exe)) { throw "OpenSCAD CLI not found: $exe" }

$map = @{
    1 = "orca_case_top.stl"
    2 = "orca_case_bottom.stl"
    3 = "orca_case_boot_plunger.stl"
    4 = "orca_case_reset_plunger.stl"
}
foreach ($n in 1..4) {
    $out = Join-Path $here $map[$n]
    Write-Host "part=$n -> $out"
    & $exe -o $out -D "part=$n" $src
    if ($LASTEXITCODE -ne 0) { throw "OpenSCAD failed for part $n" }
}
$oldSlider = Join-Path $here "orca_case_boot_slider.stl"
if (Test-Path $oldSlider) {
    Remove-Item $oldSlider
    Write-Host "removed $oldSlider"
}
Write-Host "done"
