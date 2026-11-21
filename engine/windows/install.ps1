param(
	[string]$Zip = "",
	[string]$Version = "latest",
	[string]$Destination = (Join-Path $env:LOCALAPPDATA "Programs\Quickshell"),
	[switch]$NoPath,
	[switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$Repo = "nunreasonable/quickshell-windows"

function Assert-OwnFolder {
	if (-not (Test-Path $Destination)) { return }
	if (Test-Path (Join-Path $Destination "qs.exe")) { return }
	if (Get-ChildItem -Force $Destination | Select-Object -First 1) {
		throw "$Destination has other files in it and no qs.exe, so it isn't a Quickshell install. Pick another -Destination."
	}
}

function Stop-Installed {
	Get-Process qs, qsw -ErrorAction SilentlyContinue |
		Where-Object { $_.Path -and $_.Path.StartsWith($Destination, [StringComparison]::OrdinalIgnoreCase) } |
		Stop-Process -Force
	Start-Sleep -Milliseconds 500
}

function Update-UserPath([bool]$Add) {
	$key = "HKCU:\Environment"
	$current = (Get-Item $key).GetValue("Path", "", [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
	$target = $Destination.TrimEnd("\")
	$has = @($current -split ";" | Where-Object { $_.TrimEnd("\") -eq $target }).Count -gt 0
	if ($Add -eq $has) { return }
	if ($Add) {
		$value = if (-not $current) { $Destination } elseif ($current.EndsWith(";")) { $current + $Destination } else { $current + ";" + $Destination }
	} else {
		$value = (@($current -split ";" | Where-Object { $_.TrimEnd("\") -ne $target }) -join ";")
	}
	Set-ItemProperty -Path $key -Name Path -Value $value -Type ExpandString
	$sig = '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, UIntPtr w, string l, uint f, uint t, out UIntPtr r);'
	$type = Add-Type -MemberDefinition $sig -Name QsEnv -Namespace QsInstall -PassThru
	$out = [UIntPtr]::Zero
	[void]$type::SendMessageTimeout([IntPtr]0xffff, 0x1A, [UIntPtr]::Zero, "Environment", 2, 5000, [ref]$out)
}

if ($Uninstall) {
	Assert-OwnFolder
	Stop-Installed
	if (Test-Path $Destination) { Remove-Item -Recurse -Force $Destination }
	Update-UserPath $false
	Write-Host "Quickshell removed from $Destination. Your configs in $env:LOCALAPPDATA\quickshell were left alone."
	return
}

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$temp = Join-Path ([IO.Path]::GetTempPath()) ("quickshell-install-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force $temp | Out-Null

try {
	if (-not $Zip) {
		$headers = @{ "User-Agent" = "quickshell-windows-install"; "Accept" = "application/vnd.github+json" }
		$api = if ($Version -eq "latest") { "https://api.github.com/repos/$Repo/releases/latest" } else { "https://api.github.com/repos/$Repo/releases/tags/$Version" }
		$release = Invoke-RestMethod -Uri $api -Headers $headers
		$asset = $release.assets | Where-Object { $_.name -like "quickshell-*-windows-x64.zip" } | Select-Object -First 1
		if (-not $asset) { throw "No Windows package in release $($release.tag_name)." }
		$Zip = Join-Path $temp $asset.name
		Write-Host "Downloading $($asset.name) ($([Math]::Round($asset.size / 1MB, 1)) MB)..."
		Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $Zip -UseBasicParsing
		$sumAsset = $release.assets | Where-Object { $_.name -eq "$($asset.name).sha256" } | Select-Object -First 1
		if ($sumAsset) {
			$sumFile = Join-Path $temp $sumAsset.name
			Invoke-WebRequest -Uri $sumAsset.browser_download_url -OutFile $sumFile -UseBasicParsing
			$expected = ((Get-Content $sumFile -Raw).Trim() -split "\s+")[0]
			$actual = (Get-FileHash $Zip -Algorithm SHA256).Hash
			if ($actual -ne $expected.ToUpperInvariant()) { throw "Checksum mismatch for $($asset.name)." }
		}
	}
	if (-not (Test-Path $Zip)) { throw "Package not found: $Zip" }

	$unpacked = Join-Path $temp "unpacked"
	Expand-Archive -Path $Zip -DestinationPath $unpacked -Force
	if (-not (Test-Path (Join-Path $unpacked "qs.exe"))) { throw "$Zip doesn't look like a Quickshell for Windows package." }

	Assert-OwnFolder
	Stop-Installed
	New-Item -ItemType Directory -Force $Destination | Out-Null
	robocopy $unpacked $Destination /MIR /NFL /NDL /NJH /NJS /NP | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "Copying files failed (robocopy $LASTEXITCODE)." }
	$global:LASTEXITCODE = 0

	if (-not $NoPath) { Update-UserPath $true }

	$installed = (Get-Content (Join-Path $Destination "VERSION") -ErrorAction SilentlyContinue)
	Write-Host ""
	Write-Host "Quickshell $installed installed in $Destination"
	if (-not $NoPath) { Write-Host "qs and qsw are on your PATH (open a new terminal)." }
	Write-Host ""
	Write-Host "Try the example bar:"
	Write-Host "  qs -p `"$Destination\examples\bar\shell.qml`""
	Write-Host "Your own configs go in $env:LOCALAPPDATA\quickshell\<name>\shell.qml and run with: qs -c <name>"
	Write-Host "qsw.exe does the same without a console window (for shortcuts and autostart)."
}
finally {
	Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}
