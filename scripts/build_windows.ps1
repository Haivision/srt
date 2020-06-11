param (
    [Parameter()][String]$DEVENV_PLATFORM = "x64",
    [Parameter()][String]$VS_VERSION = "2019",
    [Parameter()][String]$CONFIGURATION = "Release"
)

# if running within AppVeyor, use environment variables to set params instead of passed-in values
if($Env:APPVEYOR){ 
    if ( $Env:PLATFORM -eq 'x86' ) { $DEVENV_PLATFORM = 'Win32' } else { $DEVENV_PLATFORM = 'x64' }
    if ( $Env:APPVEYOR_BUILD_WORKER_IMAGE -eq 'Visual Studio 2019' ) { $VS_VERSION='2019' } else { $VS_VERSION='2015' }
    $CONFIGURATION = $Env:CONFIGURATION
}
$Env:VS_VERSION = $VS_VERSION
 # build SRT
if ( $VS_VERSION -eq '2019' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019' }
if ( $VS_VERSION -eq '2019' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 16 2019' }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'Win32' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015' }
if ( $VS_VERSION -eq '2015' -and $DEVENV_PLATFORM -eq 'x64' ) { $CMAKE_GENERATOR = 'Visual Studio 14 2015 Win64' }
if ( $VS_VERSION -eq '2015' ) { $ENABLE_UNITTESTS = 'ON' } else { $ENABLE_UNITTESTS = 'OFF' }
if ( $VS_VERSION -eq '2019' ) { $ENABLE_STATIC_LINK_SSL = 'false' } else { $ENABLE_STATIC_LINK_SSL = 'false' }
Remove-Item -Path ../_build -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Path ../_build -ErrorAction SilentlyContinue
Push-Location ../_build
cmake ../ -G"$CMAKE_GENERATOR" -A "$DEVENV_PLATFORM" -DCMAKE_BUILD_TYPE=$CONFIGURATION -DENABLE_STDCXX_SYNC=ON -DENABLE_UNITTESTS="$ENABLE_UNITTESTS" -DOPENSSL_USE_STATIC_LIBS="$ENABLE_STATIC_LINK_SSL"
../scripts/set-version-metadata.ps1
msbuild SRT.sln /p:Configuration=$CONFIGURATION /p:Platform=$DEVENV_PLATFORM
Pop-Location