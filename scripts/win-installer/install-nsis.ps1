#!/usr/bin/env PowerShell
#-----------------------------------------------------------------------------
#
#  SRT - Secure, Reliable, Transport
#  Copyright (c) 2021, Thierry Lelegard
# 
#  This Source Code Form is subject to the terms of the Mozilla Public
#  License, v. 2.0. If a copy of the MPL was not distributed with this
#  file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
#-----------------------------------------------------------------------------

<#
 .DESCRIPTION

  Download, expand and install NSIS, the NullSoft Installer Scripting.

 .PARAMETER ForceDownload

  Force a download even if NSIS is already downloaded. By default the
  installer file is not redownloaded, if already found, although the
  MD5 sum will still be checked.

 .PARAMETER NoInstall

  Do not install the NSIS package. By default, NSIS is installed.

 .PARAMETER NoPause

  Do not wait for the user to press <enter> at end of execution. By default,
  execute a "pause" instruction at the end of execution, which is useful
  when the script was run from Windows Explorer.
#>
[CmdletBinding(SupportsShouldProcess=$true)]
param(
    [switch]$ForceDownload = $false,
    [switch]$NoInstall = $false,
    [switch]$NoPause = $false,
	[switch]$Latest = $false
)

# A function to exit this script.
function Exit-Script([string]$Message = "")
{
    $Code = 0
    if ($Message -ne "") {
        Write-Output "ERROR: $Message"
        $Code = 1
    }
    if (-not $NoPause) {
        pause
    }
    exit $Code
}

# Local file names.
$RootDir = $PSScriptRoot
$TmpDir = "$RootDir\tmp"

# Create the directory for external products when necessary.
[void] (New-Item -Path $TmpDir -ItemType Directory -Force)

# Without this, Invoke-WebRequest is awfully slow.
$ProgressPreference = 'SilentlyContinue'

# 1. Define Project URLs
$ProjectUrl  = "https://sourceforge.net/projects/nsis/files/NSIS%203/"
$UserAgent   = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
$DownloadHead = "https://prdownloads.sourceforge.net/nsis/"

# Latest download, on-demand only. Default is a hardcoded version.
if ($Latest) {
	# 2. Find the Latest Version Folder
	Write-Host "Checking for the latest NSIS version..."
	$ProjectPage = Invoke-WebRequest -Uri $ProjectUrl -UserAgent $UserAgent -UseBasicParsing
	# Matches version folders like "3.10", "3.12", etc.
	$Versions    = [regex]::matches($ProjectPage.Content, 'title="([\d\.]+)"') |
		ForEach-Object { $_.Groups[1].Value } |
		Sort-Object {[version]$_} -Descending
	$LatestVer   = $Versions[0]
	Write-Host "Latest version found: $LatestVer"

	# 3. Build Folder and File Target URLs
	$FolderUrl   = "${ProjectUrl}${LatestVer}/"
	$FolderPage  = Invoke-WebRequest -Uri $FolderUrl -UserAgent $UserAgent -UseBasicParsing

	# Find the exact .exe installer filename (e.g., nsis-3.12-setup.exe)
	$FileMatch   = [regex]::match($FolderPage.Content, "nsis-${LatestVer}-setup\.exe")
	if (-not $FileMatch.Success) {
		throw "Could not find the setup.exe file for version $LatestVer"
	}
	$FileName    = $FileMatch.Value

	# 4. Extract the Official MD5 Checksum
	Write-Host "Extracting official MD5 checksum..."
	# SourceForge stores file metadata in a JSON-like 'data-files' attribute or specific table rows
	# This regex extracts the MD5 hash associated directly with the target filename
	#$HashPattern = 'tr[^>]*?data-name="' + [regex]::Escape($FileName) + '"[^>]*?md5">([^<]+)'
	#$MD5Match    = [regex]::match($FolderPage.Content, $HashPattern)

	$filedata_inpage = [regex]::match($FolderPage.Content, "net.sf.files = ([^;]+);")
	if (-not $filedata_inpage.Success) {
		Exit-Script "net.sf.files not found"
	}

	$filedata_json = $filedata_inpage.Groups[1].Value.Trim().ToLower()

	$filedata = ConvertFrom-Json $filedata_json

	$ExpectedMD5 = $filedata.$FileName.md5

	if ($ExpectedMD5 -eq $null) {
		Exit-Script "MD5 not found for $FileName"
	}

	Write-Host "MD5: $ExpectedMD5"
} else {
	Write-Host "USING PREDEFINED VERSION with hardcoded MD5: 3.12"
	Write-Host "Use -Latest to force latest version; note that this can be prone to MITM attacks."

	$ExpectedMD5 = "d5d54c2a96c1bcb25764adc9f9ff97f2"
	$FileName = "nsis-3.12-setup.exe"
}

# 5. Download the File
$DownloadUrl = "$DownloadHead$FileName" + "?download"
$OutputPath  = "$TmpDir\$FileName"

Write-Host "Installer path: $OutputPath"

if (-not $ForceDownload -and (Test-Path $OutputPath)) {
    Write-Host "Already downloaded, use -ForceDownload to download anyway."
} else {
    Write-Host "Downloading from: $DownloadUrl"
    Invoke-WebRequest -Uri $DownloadUrl -UserAgent Download -UseBasicParsing -OutFile $OutputPath
}

# 6. Verify the MD5 Checksum
Write-Host "Verifying checksum..."
$LocalMD5 = (Get-FileHash -Path $OutputPath -Algorithm MD5).Hash.ToLower()

if ($LocalMD5 -eq $ExpectedMD5) {
    Write-Host "Checksum OK." -ForegroundColor Green
} else {
    Write-Error "ERROR: Checksum mismatch!"
    Write-Host "Expected: $ExpectedMD5" -ForegroundColor Red
    Write-Host "Got:      $LocalMD5" -ForegroundColor Red
    Remove-Item -Path $OutputPath -Force
    Write-Host "Corrupted file removed."
	Exit-Script "Installation not possible"
}

if (-not $NoInstall) {
    Write-Host "Installing $FileName"
    Start-Process -FilePath $OutputPath -ArgumentList @("/S") -Wait
} else {
	Write-Host "Installation not requested."
}

Exit-Script



