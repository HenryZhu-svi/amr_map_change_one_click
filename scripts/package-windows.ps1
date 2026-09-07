param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,

    [Parameter(Mandatory = $true)]
    [string]$VCRedistPath,

    [string]$Version = "0.2.0",
    [string]$InnoSetupPath = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
)

$ErrorActionPreference = "Stop"
$projectDir = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildDir = Join-Path $projectDir "build\package-windows"
$stageDir = Join-Path $projectDir "dist\windows\stage"
$outputDir = Join-Path $projectDir "dist\windows"
$qtDeploy = Join-Path $QtRoot "bin\windeployqt.exe"
$appExe = Join-Path $buildDir "Release\amr-map-manager.exe"

foreach ($requiredFile in @($qtDeploy, $VCRedistPath, $InnoSetupPath)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Required file not found: $requiredFile"
    }
}

cmake -S $projectDir -B $buildDir -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_PREFIX_PATH=$QtRoot"
cmake --build $buildDir --config Release --parallel

if (-not (Test-Path -LiteralPath $appExe -PathType Leaf)) {
    throw "Release executable was not created: $appExe"
}

$resolvedOutput = [System.IO.Path]::GetFullPath($outputDir)
$resolvedStage = [System.IO.Path]::GetFullPath($stageDir)
if (-not $resolvedStage.StartsWith($resolvedOutput, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe staging path: $resolvedStage"
}
if (Test-Path -LiteralPath $resolvedStage) {
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $resolvedStage | Out-Null

Copy-Item -LiteralPath $appExe -Destination $resolvedStage
& $qtDeploy --release --no-translations --no-compiler-runtime --dir $resolvedStage $appExe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

$portablePath = Join-Path $resolvedOutput "AMRMapManager-$Version-win64-portable.zip"
if (Test-Path -LiteralPath $portablePath) { Remove-Item -LiteralPath $portablePath -Force }
Compress-Archive -Path (Join-Path $resolvedStage "*") -DestinationPath $portablePath -CompressionLevel Optimal

Copy-Item -LiteralPath $VCRedistPath -Destination (Join-Path $resolvedStage "VC_redist.x64.exe")
$issFile = Join-Path $projectDir "packaging\windows\amr-map-manager.iss"
$appIcon = Join-Path $projectDir "assets\icons\svi-logo.ico"
& $InnoSetupPath "/DAppVersion=$Version" "/DSourceDir=$resolvedStage" `
    "/DOutputDir=$resolvedOutput" "/DAppIcon=$appIcon" $issFile
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed with exit code $LASTEXITCODE" }

$installerPath = Join-Path $resolvedOutput "AMRMapManager-$Version-win64-setup.exe"
$hashes = Get-FileHash -Algorithm SHA256 $installerPath, $portablePath
$hashLines = $hashes | ForEach-Object { "$($_.Hash.ToLower())  $([System.IO.Path]::GetFileName($_.Path))" }
[System.IO.File]::WriteAllLines((Join-Path $resolvedOutput "SHA256SUMS.txt"), $hashLines)
$hashes | Format-Table -AutoSize
