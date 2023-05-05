rem Bundle CI NuGet package for VS2015 Win64 Release only build (true package should contain multi-arch composite output)
@echo off

rem If running in AppVeyor, only create a NuGet package on VS2015 x64 Release
IF DEFINED APPVEYOR_BUILD_VERSION (
    IF "%PLATFORM%"=="x64" (
        IF "%VS_VERSION%"=="2015" (
            IF "%CONFIGURATION%"=="Release" (
                echo "Building NuPkg for this build (VS2015, x64 Release)"
                nuget pack .\scripts\nuget\SrtSharp\SrtSharp.nuspec -version %APPVEYOR_BUILD_VERSION%-2015-beta
                appveyor PushArtifact SrtSharp.%APPVEYOR_BUILD_VERSION%-2015-beta.nupkg
                exit 0
            )
        )        
        IF "%VS_VERSION%"=="2019" (
            IF "%CONFIGURATION%"=="Release" (
                echo "Building NuPkg for this build (VS2019, x64 Release)"
                nuget pack .\scripts\nuget\SrtSharp\SrtSharp.nuspec -version %APPVEYOR_BUILD_VERSION%-beta
                appveyor PushArtifact SrtSharp.%APPVEYOR_BUILD_VERSION%-beta.nupkg
                exit 0
            )
        )
    )
    echo "Skipping NuPkg build (not VS2015/2019, x64 Release)"
    exit 0
) ELSE (
    rem probably running on a local workstation, so use the first given argument parameter as the version number and don't push or exit
    nuget pack .\scripts\nuget\SrtSharp\SrtSharp.nuspec -version %1
)