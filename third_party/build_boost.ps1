param(
    [ValidateSet("static", "shared")]
    [string]$LinkType = "static",
    [string]$BoostVersion = "1.86.0"
)

$ErrorActionPreference = "Stop"

$RootDir = Split-Path -Parent $PSScriptRoot
$ThirdPartyDir = Join-Path $RootDir "third_party"
$SrcDir = Join-Path $ThirdPartyDir "src"
$BuildDir = Join-Path $ThirdPartyDir "build/boost"
$InstallDir = Join-Path $ThirdPartyDir "install/boost"

$BoostVersionUnderscore = $BoostVersion -replace "\.", "_"
$BoostArchive = "boost_$BoostVersionUnderscore.zip"
$BoostUrl = "https://archives.boost.io/release/$BoostVersion/source/$BoostArchive"
$BoostSrcDir = Join-Path $SrcDir "boost_$BoostVersionUnderscore"

New-Item -ItemType Directory -Force -Path $SrcDir | Out-Null
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$ArchivePath = Join-Path $SrcDir $BoostArchive
if (-not (Test-Path $ArchivePath)) {
    Write-Host "[boost] downloading $BoostArchive"
    Invoke-WebRequest -Uri $BoostUrl -OutFile $ArchivePath
}

if (-not (Test-Path $BoostSrcDir)) {
    Write-Host "[boost] extracting sources"
    Expand-Archive -Path $ArchivePath -DestinationPath $SrcDir
}

Push-Location $BoostSrcDir
try {
    Write-Host "[boost] bootstrapping b2"
    .\bootstrap.bat

    $RuntimeLink = if ($LinkType -eq "static") { "static" } else { "shared" }

    Write-Host "[boost] building and installing to $InstallDir"
    .\b2.exe `
      "--build-dir=$BuildDir" `
      "--prefix=$InstallDir" `
      "address-model=64" `
      "link=$LinkType" `
      "runtime-link=$RuntimeLink" `
      "variant=release" `
      "threading=multi" `
      "cxxstd=17" `
      "--layout=system" `
      "--with-program_options" `
      install
}
finally {
    Pop-Location
}

Write-Host "[boost] done"
Write-Host "BOOST_ROOT=$InstallDir"
