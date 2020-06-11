################################################################################
# Windows SRT Build Script
#============================
# Usable on a Windows PC with Powershell and Visual studio, 
# or called by CI systems like AppVeyor
#
# By default produces a VS2019 64-bit Release binary using C++11 threads
################################################################################

param (
    [Parameter()][String]$DEVENV_PLATFORM = "x64",
    [Parameter()][String]$VS_VERSION = "2019",
    [Parameter()][String]$CONFIGURATION = "Release"
)
$ErrorActionPreference = "Stop"

# if running within AppVeyor, use environment variables to set params instead of passed-in values
if($Env:APPVEYOR){ 
    if ( $Env:PLATFORM -eq 'x86' ) { $DEVENV_PLATFORM = 'Win32' } else { $DEVENV_PLATFORM = 'x64' }
    if ( $Env:APPVEYOR_BUILD_WORKER_IMAGE -eq 'Visual Studio 2019' ) { $VS_VERSION='2019' } else { $VS_VERSION='2015' }
    $CONFIGURATION = $Env:CONFIGURATION
}
# persist VS_VERSION so it can be used in an artifact name later
$Env:VS_VERSION = $VS_VERSION 

# select the appropriate cmake generator string given the environment
if ( $VS_VERSION -eq '2019' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019' }
if ( $VS_VERSION -eq '2019' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019' }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015' }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015 Win64' }

# clear any previous build and create & enter the build directory
Remove-Item -Path $PSScriptRoot/../_build -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path $PSScriptRoot/../_build -ErrorAction SilentlyContinue
Push-Location $PSScriptRoot/../_build

# choose some different options depending on VS version
if ( $VS_VERSION -eq '2019' ) {
    # fire cmake to build project files ready for VS2019 msbuild
    cmake ../ -G"$CMAKE_GENERATOR" `
        -A "$DEVENV_PLATFORM" `
        -DCMAKE_BUILD_TYPE=$CONFIGURATION `
        -DENABLE_STDCXX_SYNC=ON `
        -DOPENSSL_USE_STATIC_LIBS=OFF
}
else {
    # get pthreads (still using pthreads in VS2015)
    if ( $DEVENV_PLATFORM -eq 'Win32' ) { 
        nuget install cinegy.pthreads-win32-2015 -version 2.9.1.24 -OutputDirectory ../_packages
    }
    else{        
        nuget install cinegy.pthreads-win64-2015 -version 2.9.1.24 -OutputDirectory ../_packages
    }
    #fire cmake to build project files ready for VS2015 msbuild
    cmake ../ -G"$CMAKE_GENERATOR" `
    -DCMAKE_BUILD_TYPE=$CONFIGURATION `
    -DENABLE_STDCXX_SYNC=OFF `
    -DENABLE_UNITTESTS=ON
}

# run the set-version-metadata script to inject build numbers into appveyors console and the resulting DLL
. $PSScriptRoot/set-version-metadata.ps1

# execute msbuild to perform compilation and linking
msbuild SRT.sln /p:Configuration=$CONFIGURATION /p:Platform=$DEVENV_PLATFORM

# return to the directory previously occupied before running the script
Pop-Location
