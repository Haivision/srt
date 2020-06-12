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

# make all errors trigger a script stop, rather than just carry on
$ErrorActionPreference = "Stop"
$projectRoot = (Join-Path $PSScriptRoot "/.." -Resolve)

# if running within AppVeyor, use environment variables to set params instead of passed-in values
if ( $Env:APPVEYOR ) { 
    if ( $Env:PLATFORM -eq 'x86' ) { $DEVENV_PLATFORM = 'Win32' } else { $DEVENV_PLATFORM = 'x64' }
    if ( $Env:APPVEYOR_BUILD_WORKER_IMAGE -eq 'Visual Studio 2019' ) { $VS_VERSION='2019' } else { $VS_VERSION='2015' }
    $CONFIGURATION = $Env:CONFIGURATION
}

# persist VS_VERSION so it can be used in an artifact name later
$Env:VS_VERSION = $VS_VERSION 

# select the appropriate cmake generator string given the environment
if ( $VS_VERSION -eq '2019' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019'; $MSBUILDVER = "16.0" }
if ( $VS_VERSION -eq '2019' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019'; $MSBUILDVER = "16.0" }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015'; $MSBUILDVER = "14.0" }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015 Win64'; $MSBUILDVER = "14.0" }

# clear any previous build and create & enter the build directory
$buildDir = Join-Path "$projectRoot" "_build"
Write-Host "Creating (or cleaning if already existing) the folder $buildDir for project files and outputs"
Remove-Item -Path $buildDir -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
New-Item -ItemType Directory -Path $buildDir -ErrorAction SilentlyContinue | Out-Null
Push-Location $buildDir

# check cmake is installed
if ($null -eq (Get-Command "cmake.exe" -ErrorAction SilentlyContinue)) 
{ 
    $installCmake = Read-Host "Unable to find cmake in your PATH - would you like to download and install automatically? [yes/no]"
   
    if ($installCmake -eq "y" -or $installCmake -eq "yes") {
        #download cmake 3.17.3 64-bit and run MSI for user
        $client = New-Object System.Net.WebClient        
        $tempDownloadFile = New-TemporaryFile
        $cmakeMsiFile = "$tempDownloadFile.cmake-3.17.3-win64-x64.msi"
        Write-Host "Downloading cmake (temporary file location $cmakeMsiFile)"
        Write-Host "Note: select the option to add cmake to path for this script to operate"
        $client.DownloadFile("https://github.com/Kitware/CMake/releases/download/v3.17.3/cmake-3.17.3-win64-x64.msi", "$cmakeMsiFile")
        Start-Process $cmakeMsiFile -Wait
        Remove-Item $cmakeMsiFile
        Write-Host "Cmake should have installed, this script will now exit because of path updates - please now re-run this script"
        exit
    }
    else{
        Write-Host "Quitting because cmake is required"     
        exit
    }
}

# choose some different options depending on VS version
if ( $VS_VERSION -eq '2019' ) {
    # fire cmake to build project files ready for VS2019 msbuild
    cmake ../ -G"$CMAKE_GENERATOR" `
        -A "$DEVENV_PLATFORM" `
        -DCMAKE_BUILD_TYPE=$CONFIGURATION `
        -DENABLE_STDCXX_SYNC=ON `
        -DENABLE_APPS=OFF `
        -DOPENSSL_USE_STATIC_LIBS=ON

    if($LASTEXITCODE -ne 0) {
        exit
    }
}
else {
    # get pthreads (still using pthreads in VS2015)
    if ( $DEVENV_PLATFORM -eq 'Win32' ) { 
        nuget install cinegy.pthreads-win32-2015 -version 2.9.1.24 -OutputDirectory ../_packages
    }
    else {        
        nuget install cinegy.pthreads-win64-2015 -version 2.9.1.24 -OutputDirectory ../_packages
    }
    #fire cmake to build project files ready for VS2015 msbuild
    cmake ../ -G"$CMAKE_GENERATOR" `
    -DCMAKE_BUILD_TYPE=$CONFIGURATION `
    -DENABLE_STDCXX_SYNC=OFF `
    -DENABLE_UNITTESTS=ON
    
    if($LASTEXITCODE -ne 0) {
        exit
    }
}

# run the set-version-metadata script to inject build numbers into appveyors console and the resulting DLL
. $PSScriptRoot/set-version-metadata.ps1

# execute msbuild to perform compilation and linking - if outside of appveyor, use vswhere to find msbuild
if($Env:APPVEYOR) { 
    msbuild SRT.sln /p:Configuration=$CONFIGURATION /p:Platform=$DEVENV_PLATFORM
}
else {
    # check vswhere has been added to path, or is in the well-known location (true if VS2017 Update 2 or later installed)
    if ($null -eq (Get-Command "vswhere.exe" -ErrorAction SilentlyContinue)) { 
        if ($null -eq (Get-Command "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -ErrorAction SilentlyContinue)) {
            Write-Host "Cannot find vswhere (used to locate msbuild). Please install VS2017 update 2 (or later) or add vswhere to your path and try again"
            exit
        }
        else {
            $path = & "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -version $MSBUILDVER `
                        -requires Microsoft.Component.MSBuild `
                        -find MSBuild\**\Bin\MSBuild.exe | select-object -first 1
        }
    }
    else{
        $path = vswhere -version $MSBUILDVER -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | select-object -first 1
    }
    
    if ($path) {
       & $path SRT.sln /p:Configuration=$CONFIGURATION /p:Platform=$DEVENV_PLATFORM
    }
}

# return to the directory previously occupied before running the script
Pop-Location

# if msbuild returned non-zero, throw to cause failure in CI
if($LASTEXITCODE -ne 0){
    throw
}
