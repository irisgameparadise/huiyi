# Installs 會意注音 (Huiyi) for the current user's machine.  Run from the staged folder, as administrator:
#   powershell -ExecutionPolicy Bypass -File install.ps1
# 1. copies this folder to C:\Program Files\Huiyi (files still loaded somewhere are renamed aside first)
# 2. registers the text service, 64-bit and 32-bit (regsvr32)
# 3. adds "會意注音" to this user's keyboards (zh-TW)
# 4. starts the engine at logon (HKCU Run); until then the first app that uses it starts it
# An AquaType install is removed first (the two would share the keyboard list); its settings come over.
param([string]$Target = "$env:ProgramFiles\Huiyi")
$ErrorActionPreference = 'Stop'
$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw "run this as administrator" }
$src = $PSScriptRoot
$clsid = '{A532764A-4ED6-44D9-A303-F8EA759DACA6}'; $prof = '{35C74BD7-A144-441B-AD5F-C8E0D321969A}'
$stamp = Get-Date -Format yyyyMMddHHmmss

$aqua = "$env:ProgramFiles\AquaType\uninstall.ps1"
if (Test-Path $aqua) { & powershell -NoProfile -ExecutionPolicy Bypass -File $aqua | Out-Null }
if ((Test-Path 'HKCU:\Software\AquaType') -and -not (Test-Path 'HKCU:\Software\Huiyi')) { Copy-Item 'HKCU:\Software\AquaType' 'HKCU:\Software\Huiyi' }

Get-Process HuiyiEngine -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Target\*" } | Stop-Process -Force   # only the installed one
New-Item -ItemType Directory -Force $Target, "$Target\x86" | Out-Null
# Loaded files cannot be overwritten, but they can be renamed; old renamed copies go once nothing holds them.
foreach ($f in "$Target\HuiyiTSF.dll", "$Target\x86\HuiyiTSF.dll", "$Target\HuiyiEngine.exe", "$Target\aquadecoder.dll", "$Target\aquadecoder_avx2.dll", "$Target\aquadecoder_sse.dll", "$Target\vcomp140.dll", "$Target\HuiyiSettings.exe") {
    if (Test-Path $f) { try { Remove-Item $f -Force } catch { Rename-Item $f ((Split-Path $f -Leaf) + ".old-$stamp") } }
}
Get-ChildItem $Target -Recurse -Filter '*.old-*' -ErrorAction SilentlyContinue | ForEach-Object { try { Remove-Item $_.FullName -Force } catch {} }
# Only what changed is copied: the model and the dictionary (~190 MB) are the same from one build to the next.  A file
# counts as unchanged when its size and time match (Copy-Item keeps the source's time).
Get-ChildItem $src -Recurse -File | ForEach-Object {
    $to = Join-Path $Target $_.FullName.Substring($src.Length + 1)
    $have = Get-Item -LiteralPath $to -ErrorAction SilentlyContinue
    if (-not $have -or $have.Length -ne $_.Length -or $have.LastWriteTimeUtc -ne $_.LastWriteTimeUtc) {
        New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
        Copy-Item -LiteralPath $_.FullName $to -Force
    }
}

$dll = "$Target\HuiyiTSF.dll"; $dll32 = "$Target\x86\HuiyiTSF.dll"
$p = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList "/s `"$dll`"" -Wait -PassThru -WindowStyle Hidden
if ($p.ExitCode -ne 0) { throw "regsvr32 failed ($($p.ExitCode))" }
if (Test-Path $dll32) {                                 # the 32-bit copy, for 32-bit apps
    $p = Start-Process -FilePath "$env:WINDIR\SysWOW64\regsvr32.exe" -ArgumentList "/s `"$dll32`"" -Wait -PassThru -WindowStyle Hidden
    if ($p.ExitCode -ne 0) { Write-Warning "32-bit registration failed ($($p.ExitCode)); 32-bit apps will not have 會意注音" }
}

Add-Type -Namespace Huiyi -Name Input -MemberDefinition '[DllImport("input.dll", CharSet = CharSet.Unicode)] public static extern bool InstallLayoutOrTip(string psz, uint flags);'
if (-not [Huiyi.Input]::InstallLayoutOrTip("0404:$clsid$prof", 0)) { Write-Warning "could not add the keyboard; add it in Settings > Time & language > Language > 中文(台灣) > Keyboards" }

$engine = "$Target\HuiyiEngine.exe"; $data = "$Target\data"; $log = "$env:LOCALAPPDATA\Huiyi\engine.log"
New-Item -ItemType Directory -Force (Split-Path $log) | Out-Null
$cmd = "`"$engine`" --command serve-pipe --decoder-dir `"$data\decoder`" --char-to-keys `"$data\char_to_keys.json`" --fallback-aqds `"$data\dict_store\dictionary_fallback.v1.aqds`" --fallback-manifest `"$data\dict_store\dictionary_fallback.v1.aqds.manifest.json`" --log `"$log`""
Set-ItemProperty 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run' -Name 'HuiyiEngine' -Value $cmd
# the settings, in the Start menu too (also: right click on the taskbar's 中/英, or Windows Settings' 「選項」)
$lnk = (New-Object -ComObject WScript.Shell).CreateShortcut("$env:ProgramData\Microsoft\Windows\Start Menu\Programs\會意注音 設定.lnk")
$lnk.TargetPath = "$Target\HuiyiSettings.exe"; $lnk.WorkingDirectory = $Target; $lnk.Save()
# Not started from here: this script runs elevated, and the engine must not.  The first app that switches to 會意注音
# starts it (at normal integrity), and so does every logon from now on.
"installed to $Target; switch input methods with Win+Space and choose 會意注音"
