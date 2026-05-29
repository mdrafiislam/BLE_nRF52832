$ErrorActionPreference = "Stop"

$emBuild = "C:\Program Files\SEGGER\SEGGER Embedded Studio for ARM 5.34\bin\emBuild.exe"
$project = ".\pca10040\s132\ses\ble_app_spis_pca10040_s132.emProject"
$config = "Debug"

Write-Host "Building nRF project..."
Write-Host "Project: $project"
Write-Host "Config:  $config"

& $emBuild -config $config -rebuild $project -verbose

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
    exit $LASTEXITCODE
}

Write-Host "Build successful."