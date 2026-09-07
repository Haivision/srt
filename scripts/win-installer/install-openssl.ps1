#!/usr/bin/env PowerShell
#-----------------------------------------------------------------------------
#
#  SRT - Secure, Reliable, Transport
#  Copyright (c) 2021-2024, Thierry Lelegard
# 
#  This Source Code Form is subject to the terms of the Mozilla Public
#  License, v. 2.0. If a copy of the MPL was not distributed with this
#  file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
#-----------------------------------------------------------------------------

<#
 .SYNOPSIS

  Download, expand and install OpenSSL for Windows.

 .PARAMETER ForceDownload

  Force a download even if the OpenSSL installers are already downloaded.

 .PARAMETER NoInstall

  Do not install the OpenSSL packages. By default, OpenSSL is installed.

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

Write-Output "OpenSSL download and installation procedure"

# The list of OpenSSL packages is available in a JSON file. See more details in README.md.
$PackageList = "https://github.com/slproweb/opensslhashes/raw/master/win32_openssl_hashes.json"

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

if ($Latest) {

	# Get the JSON configuration file for OpenSSL downloads.
	$status = 0
	$message = ""
	try {
		Write-Output "Getting file index: $PackageList"
		$response = Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $PackageList
		$status = [int] [Math]::Floor($response.StatusCode / 100)
	}
	catch {
		$message = $_.Exception.Message
	}
	if ($status -ne 1 -and $status -ne 2) {
		if ($message -eq "" -and (Test-Path variable:response)) {
			Exit-Script "Status code $($response.StatusCode), $($response.StatusDescription)"
		}
		else {
			Exit-Script "#### Error accessing ${PackageList}: $message"
		}
	}
	$config = ConvertFrom-Json $Response.Content

	Write-Output "Getting installer file with .light and .installer =~ 'exe' and .arch =~ 'Universal'"

	# Find the URL of the latest "universal" installer in the JSON config file.
	#$Url =
	$have = 0
	$config.files | Get-Member | ForEach-Object {
		$name = $_.name
		$info = $config.files.$($_.name)
		if (-not $info.light -and $info.installer -like "exe" -and $info.arch -like "universal") {
			$found_info = $info
			$have = 1
		}
	} # | Select-Object -Last 1
	if (-not $have) {
		Exit-Script "#### No universal installer found"
	}

	$Url = $found_info.url
	$xsum = $found_info.md5
} else {

	Write-Host "USING PREDEFINED VERSION with hardcoded MD5: 4.0.1"
	Write-Host "Use -Latest to force latest version; note that this can be prone to MITM attacks."
	
	$Url = "https://slproweb.com/download/WinUniversalOpenSSL-4_0_1.exe"
	$xsum = "34051060e0e9f48e63cfc1f3356191f6"
}

$ExeName = (Split-Path -Leaf $Url)
$ExePath = "$TmpDir\$ExeName"

if (-not $ForceDownload -and (Test-Path $ExePath)) {
    Write-Output "$ExeName already downloaded, use -ForceDownload to download again"
}
else {
    Write-Output "Downloading $Url ..."
    Invoke-WebRequest -UseBasicParsing -UserAgent Download -Uri $Url -OutFile $ExePath
}

if (-not (Test-Path $ExePath)) {
    Exit-Script "$Url download failed"
}

Write-Output "Will check tmp\$ExeName for MD5: $xsum ..."

$filehash = get-filehash $ExePath -algorithm md5

if ($filehash.Hash -ne $xsum) {
	Exit-Script "$ExePath MD5: $filehash - NOT MATCHING"
}

if (-not $NoInstall) {
    Write-Output "CHECKSUM MATCHES. Installing $ExeName"
    Start-Process -FilePath $ExePath -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/ALLUSERS") -Wait
} else {
	Write-Output "Installation not requested."
}


Exit-Script
