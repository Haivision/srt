################################################################################
# Windows SRT NuGet Package Creation Script
#============================
# Usable on a Windows PC with Powershell and nuget available, 
# or called by CI systems like AppVeyor
#
################################################################################

param (
    [Parameter()][String]$VERSION = "1.5.1.10199",
	[Parameter()][String]$BUILD_DIR = "_nuget"
)

# make all errors trigger a script stop, rather than just carry on
$ErrorActionPreference = "Stop"

$projectRoot = Join-Path $PSScriptRoot "/.." -Resolve
$sourceDir = Join-Path "$projectRoot" "package"
$targetDir = Join-Path "$projectRoot" "$BUILD_DIR"
$nuspecPath = Join-Path "$projectRoot" "scripts/nuget/SrtSharp/SrtSharp.nuspec"

nuget pack $nuspecPath -version $VERSION-alpha -OutputDirectory $targetDir

# if antyhing returned non-zero, throw to cause failure in CI
if( $LASTEXITCODE -ne 0 ) {
    throw
}
