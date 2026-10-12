# Removes 會意注音, as administrator:
#   powershell -ExecutionPolicy Bypass -File "C:\Program Files\Huiyi\uninstall.ps1"
# The personal memory (%USERPROFILE%\.huiyi) is kept; delete that folder too to forget everything typed.
param([string]$Target = "$env:ProgramFiles\Huiyi")
$ErrorActionPreference = 'Continue'
$clsid = '{A532764A-4ED6-44D9-A303-F8EA759DACA6}'; $prof = '{35C74BD7-A144-441B-AD5F-C8E0D321969A}'
Add-Type -Namespace Huiyi -Name Input -MemberDefinition '[DllImport("input.dll", CharSet = CharSet.Unicode)] public static extern bool InstallLayoutOrTip(string psz, uint flags);'
[void][Huiyi.Input]::InstallLayoutOrTip("0404:$clsid$prof", 1)          # ILOT_UNINSTALL
Remove-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'HuiyiEngine' -ErrorAction SilentlyContinue
Remove-Item "$env:ProgramData\Microsoft\Windows\Start Menu\Programs\會意注音 設定.lnk" -Force -ErrorAction SilentlyContinue
Get-Process HuiyiSettings -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Target\*" } | Stop-Process -Force
Get-Process HuiyiEngine -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Target\*" } | Stop-Process -Force   # only the installed one
if (Test-Path "$Target\x86\HuiyiTSF.dll") { Start-Process -FilePath "$env:WINDIR\SysWOW64\regsvr32.exe" -ArgumentList "/u /s `"$Target\x86\HuiyiTSF.dll`"" -Wait -WindowStyle Hidden }
if (Test-Path "$Target\HuiyiTSF.dll") { Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList "/u /s `"$Target\HuiyiTSF.dll`"" -Wait -WindowStyle Hidden }
Remove-Item -Recurse -Force $Target -ErrorAction SilentlyContinue
if (Test-Path $Target) { "some files are still in use; sign out and delete $Target" } else { "removed" }
