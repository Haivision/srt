################################################################################
# Windows SRT Build Script
#============================
# Usable on a Windows PC with Powershell and Visual studio, 
# or called by CI systems like AppVeyor
#
# By default produces a VS2019 64-bit Release binary using C++11 threads, without
# encryption or unit tests enabled, but including test apps.
# Before enabling any encryption options, please install OpenSSL (or customize)
################################################################################

param (
    [Parameter()][String]$VS_VERSION = "2019",
    [Parameter()][String]$CONFIGURATION = "Release",
    [Parameter()][String]$DEVENV_PLATFORM = "x64",
    [Parameter()][String]$ENABLE_ENCRYPTION = "OFF",
    [Parameter()][String]$STATIC_LINK_SSL = "OFF",
    [Parameter()][String]$CXX11 = "ON",
    [Parameter()][String]$BUILD_APPS = "ON",
    [Parameter()][String]$UNIT_TESTS = "OFF"
)

# cmake can be optionally installed (useful when running interactively on a developer station).
# The URL for automatic download is defined later in the script, but it should be possible to just vary the 
# specific version set below and the URL should be stable enough to still work - you have been warned.
$cmakeVersion = "3.17.3"

# make all errors trigger a script stop, rather than just carry on
$ErrorActionPreference = "Stop"

$projectRoot = Join-Path $PSScriptRoot "/.." -Resolve

# if running within AppVeyor, use environment variables to set params instead of passed-in values
if ( $Env:APPVEYOR ) { 
    if ( $Env:PLATFORM -eq 'x86' ) { $DEVENV_PLATFORM = 'Win32' } else { $DEVENV_PLATFORM = 'x64' }
    if ( $Env:APPVEYOR_BUILD_WORKER_IMAGE -eq 'Visual Studio 2019' ) { $VS_VERSION='2019' }
    if ( $Env:APPVEYOR_BUILD_WORKER_IMAGE -eq 'Visual Studio 2015' ) { $VS_VERSION='2015' }
    if ( $Env:APPVEYOR_BUILD_WORKER_IMAGE -eq 'Visual Studio 2013' ) { $VS_VERSION='2013' }

    #if not statically linking OpenSSL, set flag to gather the specific openssl package from the build server into package
    if ( $STATIC_LINK_SSL -eq 'OFF' ) { $Env:GATHER_SSL_INTO_PACKAGE = $true }

    #if unit tests are on, set flag to actually execute ctest step
    if ( $UNIT_TESTS -eq 'ON' ) { $Env:RUN_UNIT_TESTS = $true }
    
    $CONFIGURATION = $Env:CONFIGURATION

    #appveyor has many openssl installations - place the latest one in the default location unless VS2013
    if( $VS_VERSION -ne '2013' ) {
        Remove-Item -Path "C:\OpenSSL-Win32" -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
        Remove-Item -Path "C:\OpenSSL-Win64" -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
        Copy-Item -Path "C:\OpenSSL-v111-Win32" "C:\OpenSSL-Win32" -Recurse | Out-Null
        Copy-Item -Path "C:\OpenSSL-v111-Win64" "C:\OpenSSL-Win64" -Recurse | Out-Null
    }
}

# persist VS_VERSION so it can be used in an artifact name later
$Env:VS_VERSION = $VS_VERSION

# select the appropriate cmake generator string given the environment
if ( $VS_VERSION -eq '2019' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019'; $MSBUILDVER = "16.0"; }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015'; $MSBUILDVER = "14.0"; }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015 Win64'; $MSBUILDVER = "14.0"; }
if ( $VS_VERSION -eq '2013' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 12 2013'; $MSBUILDVER = "12.0"; }
if ( $VS_VERSION -eq '2013' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 12 2013 Win64'; $MSBUILDVER = "12.0"; }

# clear any previous build and create & enter the build directory
$buildDir = Join-Path "$projectRoot" "_build"
Write-Output "Creating (or cleaning if already existing) the folder $buildDir for project files and outputs"
Remove-Item -Path $buildDir -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
New-Item -ItemType Directory -Path $buildDir -ErrorAction SilentlyContinue | Out-Null
Push-Location $buildDir

# check cmake is installed
if ( $null -eq (Get-Command "cmake.exe" -ErrorAction SilentlyContinue) ) { 
    $installCmake = Read-Host "Unable to find cmake in your PATH - would you like to download and install automatically? [yes/no]"
   
    if ( $installCmake -eq "y" -or $installCmake -eq "yes" ) {
        # download cmake and run MSI for user
        $client = New-Object System.Net.WebClient        
        $tempDownloadFile = New-TemporaryFile
        
        $cmakeUrl = "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/cmake-$cmakeVersion-win64-x64.msi"
        $cmakeMsiFile = "$tempDownloadFile.cmake-$cmakeVersion-win64-x64.msi"
        Write-Output "Downloading cmake from $cmakeUrl (temporary file location $cmakeMsiFile)"
        Write-Output "Note: select the option to add cmake to path for this script to operate"
        $client.DownloadFile("$cmakeUrl", "$cmakeMsiFile")
        Start-Process $cmakeMsiFile -Wait
        Remove-Item $cmakeMsiFile
        Write-Output "Cmake should have installed, this script will now exit because of path updates - please now re-run this script"
        exit
    }
    else{
        Write-Output "Quitting because cmake is required"     
        exit
    }
}

if ( $CXX11 -eq "OFF" ) {
    # get pthreads (this is legacy, and is only availble in nuget for VS2015 and VS2013)
    if ( $VS_VERSION -gt 2015 ) { 
        Write-Output "Pthreads is not recommended for use beyond VS2015 and is not supported by this build script - aborting build"
        exit
    }
    if ( $DEVENV_PLATFORM -eq 'Win32' ) { 
        nuget install cinegy.pthreads-win32-$VS_VERSION -version 2.9.1.24 -OutputDirectory ../_packages
    }
    else {        
        nuget install cinegy.pthreads-win64-$VS_VERSION -version 2.9.1.24 -OutputDirectory ../_packages
    }
}

if ( $STATIC_LINK_SSL -eq "ON" ) {
    if ( $ENABLE_ENCRYPTION -eq "OFF" ) {
        # requesting a static link implicitly requires encryption support
        Write-Output "Static linking to OpenSSL requested, will force encryption feature ON"
        $ENABLE_ENCRYPTION = "ON"
    }
}

# build the cmake command flags from arguments
$cmakeFlags = "-DCMAKE_BUILD_TYPE=$CONFIGURATION " + 
                "-DENABLE_STDCXX_SYNC=$CXX11 " + 
                "-DENABLE_APPS=$BUILD_APPS " + 
                "-DENABLE_ENCRYPTION=$ENABLE_ENCRYPTION " +
                "-DOPENSSL_USE_STATIC_LIBS=$STATIC_LINK_SSL " + 
                "-DENABLE_UNITTESTS=$UNIT_TESTS"

# cmake uses a flag for architecture from vs2019, so add that as a suffix
if ( $VS_VERSION -eq '2019' ) {    
    $cmakeFlags += " -A `"$DEVENV_PLATFORM`""
}

# fire cmake to build project files
$execVar = "cmake ../ -G`"$CMAKE_GENERATOR`" $cmakeFlags"
Write-Output $execVar

# Reset reaction to Continue for cmake as it sometimes tends to print
# things on stderr, which is understood by PowerShell as error. The
# exit code from cmake will be checked anyway.
$ErrorActionPreference = "Continue"
Invoke-Expression "& $execVar"

# check build ran OK, exit if cmake failed
if( $LASTEXITCODE -ne 0 ) {
    return $LASTEXITCODE
}

$ErrorActionPreference = "Stop"

# run the set-version-metadata script to inject build numbers into appveyors console and the resulting DLL
. $PSScriptRoot/set-version-metadata.ps1

# look for msbuild
$msBuildPath = Get-Command "msbuild.exe" -ErrorAction SilentlyContinue
if ( $null -eq $msBuildPath ) { 
    # no mbsuild in the path, so try to locate with 'vswhere'
    $vsWherePath = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue
    if ( $null -eq $vsWherePath ) { 
        # no vswhere in the path, so check the Microsoft published location (true since VS2017 Update 2)
        $vsWherePath = Get-Command "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -ErrorAction SilentlyContinue
        if ( $null -eq $vsWherePath ) {
            Write-Output "Cannot find vswhere (used to locate msbuild). Please install VS2017 update 2 (or later) or add vswhere to your path and try again"
            exit
        }
    }    
    $msBuildPath = & $vsWherePath -version $MSBUILDVER -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | select-object -first 1
}

& $msBuildPath SRT.sln /p:Configuration=$CONFIGURATION /p:Platform=$DEVENV_PLATFORM

# return to the directory previously occupied before running the script
Pop-Location

# if msbuild returned non-zero, throw to cause failure in CI
if( $LASTEXITCODE -ne 0 ) {
    throw
}
