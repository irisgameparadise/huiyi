# Restores the build inputs that are kept out of git (the model weights, the frequency shortlist and the fallback
# dictionary store) from a released Huiyi zip, and checks them against packaging/payload/SHA256SUMS.
#   powershell -NoProfile -File windows\install\restore_payload.ps1 -Zip Huiyi-0.1.0-win-x64.zip
#   powershell -NoProfile -File windows\install\restore_payload.ps1 -Release v0.1.0      (downloads it from GitHub)
param([string]$Zip, [string]$Release)
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..\..").Path
$tmp = Join-Path $env:TEMP ("huiyi-payload-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force $tmp | Out-Null
try {
    if ($Release) {
        $ver = $Release.TrimStart('v')
        $Zip = Join-Path $tmp "Huiyi-$ver-win-x64.zip"
        Invoke-WebRequest -UseBasicParsing "https://github.com/irisgameparadise/huiyi/releases/download/$Release/Huiyi-$ver-win-x64.zip" -OutFile $Zip
    }
    if (-not $Zip) { throw "give -Zip <file> or -Release <tag>" }
    Expand-Archive -LiteralPath $Zip -DestinationPath "$tmp\x"
    $data = Get-ChildItem "$tmp\x" -Recurse -Directory -Filter data | Select-Object -First 1
    if (-not $data) { throw "no data folder inside $Zip" }
    $pay = "$repo\packaging\payload"
    New-Item -ItemType Directory -Force "$pay\decoder", "$pay\dict_store" | Out-Null
    Copy-Item "$($data.FullName)\decoder\model.safetensors", "$($data.FullName)\decoder\norm_rank.bin" "$pay\decoder"
    Copy-Item "$($data.FullName)\dict_store\dictionary_fallback.v1.aqds" "$pay\dict_store"
    $bad = 0
    foreach ($line in Get-Content "$pay\SHA256SUMS") {
        $hash, $file = $line -split '\s+', 2
        $got = (Get-FileHash -Algorithm SHA256 (Join-Path $pay $file)).Hash.ToLower()
        if ($got -ne $hash) { "MISMATCH $file"; $bad++ } else { "ok $file" }
    }
    if ($bad) { throw "$bad file(s) do not match SHA256SUMS" }
} finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
