#! /usr/bin/pwsh

################################################################################
# Linux SRT Build Script
#============================
# Usable on a Linux machine (or container), or called by CI systems like AppVeyor
#
# By default produces a 64-bit Release binary using C++11 threads, without
# encryption or unit tests enabled, but including test apps.
################################################################################

param (    
    [Parameter()][String]$CONFIGURATION = "Release",
    [Parameter()][String]$BUILDCONTAINER = "srtselfbuildcontainer",
    [Parameter()][String]$DEVENV_PLATFORM = "x64",
    [Parameter()][String]$ENABLE_ENCRYPTION = "OFF",
    [Parameter()][String]$STATIC_LINK_SSL = "OFF",
    [Parameter()][String]$CXX11 = "ON",
    [Parameter()][String]$BUILD_APPS = "ON",
    [Parameter()][String]$UNIT_TESTS = "OFF",
    [Parameter()][String]$BUILD_DIR = "_build-linux",
    [Parameter()][String]$BONDING = "ON",
    [Parameter()][String]$ENABLE_SWIG = "ON",
    [Parameter()][String]$ENABLE_SWIG_CSHARP = "ON"
)

$projectRoot = Join-Path $PSScriptRoot "/.." -Resolve

if($BUILDCONTAINER -eq "srtselfbuildcontainer") {
    # the default build container name was passed - so rebuild this self-contained build environment first
    # on a CI system, there may be a known-suitable build container already existing, so this can speed up builds
    # but if this is on default, we can just self-serve
    $execVar = "docker build -t $BUILDCONTAINER -f $($projectRoot)/scripts/Dockerfile.linux ."
    Write-Output $execVar
    Invoke-Expression "& $execVar"
}

# clear any previous build and create & enter the build directory
$buildDir = Join-Path "$projectRoot" "$BUILD_DIR"
Write-Output "Creating (or cleaning if already existing) the folder $buildDir for project files and outputs"
Remove-Item -Path $buildDir -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
New-Item -ItemType Directory -Path $buildDir -ErrorAction SilentlyContinue | Out-Null

# build the cmake command flags from arguments
$cmakeFlags = "-DCMAKE_BUILD_TYPE=$CONFIGURATION " + 
                "-DENABLE_STDCXX_SYNC=$CXX11 " + 
                "-DENABLE_APPS=$BUILD_APPS " + 
                "-DENABLE_ENCRYPTION=$ENABLE_ENCRYPTION " +
                "-DENABLE_UNITTESTS=$UNIT_TESTS " +
                "-DENABLE_BONDING=$BONDING " + 
                "-DENABLE_SWIG=$ENABLE_SWIG " +
                "-DENABLE_SWIG_CSHARP=$ENABLE_SWIG_CSHARP"

# generate command for docker run of cmake generation step
$execVar = "docker run --rm -v $($projectRoot):/srt -w /srt/$BUILD_DIR $BUILDCONTAINER cmake -S ../ $cmakeFlags"
Write-Output $execVar
Invoke-Expression "& $execVar"

# generate command for docker run of cmake build
$execVar = "docker run --rm -v $($projectRoot):/srt -w /srt/$BUILD_DIR $BUILDCONTAINER cmake --build ./"
Write-Output $execVar
Invoke-Expression "& $execVar"
