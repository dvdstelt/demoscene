<#
.SYNOPSIS
Downloads the original MS-DOS releases from scene.org and puts the files the remakes read into place,
checking every file's SHA-256.

.DESCRIPTION
  fetch-originals.ps1          all demos (next to a released program: the demo of that program)
  fetch-originals.ps1 303      only 303 by Acme (X'97)
  fetch-originals.ps1 paper    only Paper by Psychic Link (Wired '96)
  fetch-originals.ps1 jizz     only JIZZ by The Black Lotus (Wired '97)
  fetch-originals.ps1 stars    only STARS by NoooN (Assembly '95)
  fetch-originals.ps1 dope     only DOPE by Complex (The Gathering '95)
  fetch-originals.ps1 clone    only clone meets clone by Acme and Fudge (Takeover '98)
  fetch-originals.ps1 inside   only Inside by CNCD (The Gathering '96)

In the repository the files go into <repo>\<demo>\original\. Anywhere else (next to a released program such
as acme303.exe, paper-remake.exe, jizz-remake.exe, stars-remake.exe, dope-remake.exe, clone-remake.exe or
inside-remake.exe) they go
into an original\ folder next to this script, and without an argument only the demo whose program is there is
fetched.

A file that is already there and verifies is never touched. A file that is there but differs is renamed to
<name>.old before the release version takes its place. Needs Windows PowerShell 5.1 or PowerShell 7.

.EXAMPLE
powershell -ExecutionPolicy Bypass -File fetch-originals.ps1

.EXAMPLE
powershell -ExecutionPolicy Bypass -File scripts\fetch-originals.ps1 paper
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('303', 'paper', 'jizz', 'stars', 'dope', 'clone', 'inside', 'all')]
    [string]$Demo
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # the progress bar makes Invoke-WebRequest very slow in 5.1
try {
    # Windows PowerShell 5.1 may not offer TLS 1.2 by default
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch { }

# ---------------------------------------------------------------- the releases
# The files are checked, not the zips, so any mirror or repack with the same files is accepted.
$Releases = [ordered]@{
    '303' = @{
        Title   = "303 by Acme (X'97, 1997)"
        ZipName = 'acme_303.zip'
        ZipPath = 'demos/groups/acme/demos/acme_303.zip'
        Program = 'acme303'
        Files   = [ordered]@{
            '303.EXE'      = 'a3f8422a5da12ac114b8306c66577381ecf314cacb3a13bc768415f015081f22'
            'MUSTARD.PAK'  = '8510449684e017107546a66047fccac8e51189bf40836a351a7dec09b4ac40ae'
            'GARLIC.PAK'   = 'c864f6057301342926f3771163e00004aabda3c602ddc53c6248ee451fd275e8'
            'KETCHUP.PAK'  = '875f6d5fe9579438000e6bbcf05ea847be861d66ab2c31b8e429fee2ff650d8e'
            'ALEX.LZW'     = '3c5edb084bc3b7d6021e9fbdb7df410ce1fa4150b84ea0a2423b55b48e91b2a6'
            'HARL.LZW'     = '89800ce6ad6b8b142ce703b741790d6e8a7cf405f696c9adb2922386281b6a29'
            'VS.LZW'       = '79094754209309eaa26faf686c8721a1901fb35b2118a6a1905a516b4ce2c730'
            'PARANOID.LZW' = '2662410562ac779fce3381adb9a696a3c49b330ad6c5e8e84cce37893a196fef'
        }
    }
    'paper' = @{
        Title   = "Paper by Psychic Link (Wired '96, 1996)"
        ZipName = 'paper.zip'
        ZipPath = 'parties/1996/wired96/in64/paper.zip'
        Program = 'paper-remake'
        Files   = [ordered]@{
            'PAPER.EXE' = 'e60c3eccc4b38df0461d78fd884758886de9b4c1fa31505d6556874ec6b8ef4d'
        }
    }
    'jizz' = @{
        Title   = "JIZZ by The Black Lotus (Wired '97, 1997)"
        ZipName = 'jizz.zip'
        ZipPath = 'demos/groups/tbl/pc/jizz.zip'
        Program = 'jizz-remake'
        Files   = [ordered]@{
            'JIZZ.EXE' = '8bf022ea25fc6c02504e915441cc40ec0a2226d21eda4ffe1a1a46d2dca99593'
        }
    }
    'stars' = @{
        Title   = "STARS: Wonders of the World by NoooN (Assembly '95, 1995; final release)"
        ZipName = 'nooonstf.zip'
        ZipPath = 'demos/groups/nooon/nooonstf.zip'
        Program = 'stars-remake'
        Files   = [ordered]@{
            'STARS.EXE' = '148aa9240f6a29a5678a0812cf295a38039560f05d26dcfae8c829f8cafa97fc'
        }
    }
    'dope' = @{
        Title   = "DOPE by Complex (The Gathering '95, 1995)"
        ZipName = 'clx_dope.zip'
        ZipPath = 'demos/groups/complex/clx_dope.zip'
        Program = 'dope-remake'
        Files   = [ordered]@{
            'DOPE.EXE' = '0c54549d741858ecddd23ac4a92d32b0d4823a537a45dcf1722613dcd6de97d0'
            'DOPE.MOD' = 'b2440ad6d713b38d5ca4c21a4a4ab4ab974e2f951175291769191c39ff7d93ce'
        }
    }
    'clone' = @{
        Title   = "clone meets clone by Acme and Fudge (Takeover '98, 1998)"
        ZipName = 'acme_cln.zip'
        ZipPath = 'demos/groups/fudge/acme_cln.zip'
        Program = 'clone-remake'
        Files   = [ordered]@{
            'CLONE.EXE' = '11ed22222af2cc34dbca2f8c7683354a530d2a062363d7eb298bcaf399942275'
        }
    }
    'inside' = @{
        Title   = "Inside by CNCD (The Gathering '96, 1996)"
        ZipName = 'inside.zip'
        ZipPath = 'parties/1996/gathering96/demo/inside.zip'
        Program = 'inside-remake'
        Files   = [ordered]@{
            'INSIDE.EXE' = '15ac35f8ea1c4cb32cd70b1fb7ba425af5bb60cb6c81794da26497c8ceeb53ef'
            'INSIDE.DAT' = '9026946bd6988f3f375559a47982c7483a33817ef89d725f4ea194df84e5a865'
            'EYE.AMF'    = 'bf6172d3b1728da542b71f355ef4b58f9573a76fec02fd9ea99a801b5886d01f'
            'SDIGUS.DSD' = '9eebd39b89135d55039cd7b8de33a032555c0c6859a26efe2ecc07e36737eef1'
        }
    }
}

function Get-Urls([string]$ZipPath) {
    # scene.org itself, two https mirrors, then the scene.org redirector (picks any mirror)
    @(
        "https://ftp.scene.org/pub/$ZipPath",
        "https://mirror.netcologne.de/scene.org/$ZipPath",
        "https://mirror.scenesat.com/scene.org/$ZipPath",
        "https://files.scene.org/get/$ZipPath"
    )
}

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-Verified([string]$Path, [string]$Hash) {
    (Test-Path -LiteralPath $Path -PathType Leaf) -and ((Get-Sha256 $Path) -eq $Hash)
}

# ---------------------------------------------------------------- where the files go
$ScriptDir = $PSScriptRoot
$RepoRoot = Split-Path -Parent $ScriptDir
$InRepo = $false
if ($RepoRoot) {
    foreach ($d in $Releases.Keys) {
        if (Test-Path -LiteralPath (Join-Path (Join-Path $RepoRoot $d) 'remake') -PathType Container) { $InRepo = $true }
    }
}

function Get-Dest([string]$Name) {
    if ($InRepo) { Join-Path (Join-Path $RepoRoot $Name) 'original' } else { Join-Path $ScriptDir 'original' }
}

# ---------------------------------------------------------------- one demo
function Get-Original([string]$Name) {
    $r = $Releases[$Name]
    $dest = Get-Dest $Name
    Write-Host "== $($r.Title)"
    Write-Host "   into $dest"

    $need = @()
    foreach ($f in $r.Files.Keys) {
        if (Test-Verified (Join-Path $dest $f) $r.Files[$f]) {
            Write-Host "   ok         $f (already there)"
        } else {
            $need += $f
        }
    }
    if ($need.Count -eq 0) {
        Write-Host '   all files present and verified, nothing to download'
        return $true
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ('fetch-originals-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        $zip = Join-Path $work $r.ZipName
        $x = Join-Path $work 'files'

        # download, unpack and verify; on any failure try the next mirror
        $found = $null
        foreach ($url in (Get-Urls $r.ZipPath)) {
            Write-Host "   download   $url"
            Remove-Item -LiteralPath $zip, $x -Recurse -Force -ErrorAction SilentlyContinue
            try {
                Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing -TimeoutSec 300
            } catch {
                Write-Host "              failed ($($_.Exception.Message)), trying the next mirror"
                continue
            }
            try {
                Expand-Archive -LiteralPath $zip -DestinationPath $x -Force
                # a release split into zips inside the zip (Inside: inside1.zip and inside2.zip): unpack those too
                $i = 0
                foreach ($inner in @(Get-ChildItem -LiteralPath $x -Recurse -File -Filter '*.zip')) {
                    $i++
                    Expand-Archive -LiteralPath $inner.FullName -DestinationPath (Join-Path $x "inner$i") -Force
                }
            } catch {
                Write-Host '              not a zip file, trying the next mirror'
                continue
            }
            $found = @{}
            foreach ($f in $need) {
                $file = Get-ChildItem -LiteralPath $x -Recurse -File | Where-Object { $_.Name -eq $f } | Select-Object -First 1
                if ($null -eq $file -or (Get-Sha256 $file.FullName) -ne $r.Files[$f]) {
                    Write-Host "              $f in this download does not match the known release"
                    $found = $null
                    break
                }
                $found[$f] = $file
            }
            if ($null -ne $found) { break }
            Write-Host '              trying the next mirror'
        }
        if ($null -eq $found) {
            Write-Host "error: could not get a verified copy of $($r.ZipName) from any mirror; nothing was changed in $dest" -ForegroundColor Red
            return $false
        }

        # install the verified files, named as in the zip
        New-Item -ItemType Directory -Path $dest -Force | Out-Null
        foreach ($f in $need) {
            $src = $found[$f]
            $target = Join-Path $dest $src.Name
            if (Test-Path -LiteralPath $target) {
                $old = "$target.old"
                $n = 1
                while (Test-Path -LiteralPath $old) { $n++; $old = "$target.old$n" }
                Move-Item -LiteralPath $target -Destination $old
                Write-Host "   replaced   $($src.Name) (it differed from the release; the old file is now $(Split-Path -Leaf $old))"
            } else {
                Write-Host "   added      $($src.Name)"
            }
            Copy-Item -LiteralPath $src.FullName -Destination "$target.part" -Force
            Move-Item -LiteralPath "$target.part" -Destination $target -Force
            if (-not (Test-Verified $target $r.Files[$f])) { throw "$target does not verify after copying" }
        }
        Write-Host '   all files present and verified'
        return $true
    } finally {
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}

# ---------------------------------------------------------------- which demos
$names = @()
if ($Demo -eq 'all') {
    $names = @($Releases.Keys)
} elseif ($Demo) {
    $names = @($Demo)
} else {
    if (-not $InRepo) {
        # next to a released program: fetch what that program needs
        foreach ($k in $Releases.Keys) {
            $p = Join-Path $ScriptDir $Releases[$k].Program
            if ((Test-Path -LiteralPath "$p.exe") -or (Test-Path -LiteralPath $p)) { $names += $k }
        }
    }
    if ($names.Count -eq 0) { $names = @($Releases.Keys) }
}

$failed = $false
foreach ($n in $names) {
    try {
        if (-not (Get-Original $n)) { $failed = $true }
    } catch {
        Write-Host "error: $($_.Exception.Message)" -ForegroundColor Red
        $failed = $true
    }
}
if ($failed) {
    Write-Host 'error: not all originals are in place (see above)' -ForegroundColor Red
    exit 1
}
Write-Host 'done'
