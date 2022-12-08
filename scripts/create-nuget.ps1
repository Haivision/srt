################################################################################
# Windows SRT NuGet Package Creation Script
#============================
# Usable on a Windows PC with Powershell and nuget available, 
# or called by CI systems like AppVeyor
#
################################################################################

param (
    [Parameter()][String]$PACKAGEFOLDER = "package",
    [Parameter()][String]$VERSION = "1.5.1.0"
)

# make all errors trigger a script stop, rather than just carry on
$ErrorActionPreference = "Stop"

$packageDir = Join-Path $PSScriptRoot "../" $PACKAGEFOLDER -Resolve

Get-ChildItem $packageDir -Filter *.zip | Foreach-Object {
   Expand-Archive -Force -Path $_.FullName -DestinationPath $(Join-Path $packageDir "extracted")
}

nuget pack .\nuget\SrtSharp\SrtSharp.nuspec -version $VERSION

# if antyhing returned non-zero, throw to cause failure in CI
if( $LASTEXITCODE -ne 0 ) {
    throw
}
