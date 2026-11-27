param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
)

$ErrorActionPreference = "Stop"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure {
    param([string]$Message)
    $script:failures.Add($Message) | Out-Null
}

function Get-RelativeMarkdown {
    param([string]$Root)
    Get-ChildItem -Path $Root -Recurse -Filter "*.md" |
        ForEach-Object {
            $_.FullName.Substring($Root.Length).TrimStart("\", "/") -replace "\\", "/"
        } |
        Sort-Object
}

$enRoot = Join-Path $RepoRoot "docs/en"
$zhRoot = Join-Path $RepoRoot "docs/zh"
$apiPage = "docs/api/index.md"

if (-not (Test-Path -PathType Leaf (Join-Path $RepoRoot $apiPage))) {
    Add-Failure "Missing C API navigation page $apiPage"
} elseif (Test-Path (Join-Path $RepoRoot ".git")) {
    git -C $RepoRoot ls-files --error-unmatch -- $apiPage 2>$null | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "The C API navigation page $apiPage must be tracked in Git"
    }
}

$enPages = @(Get-RelativeMarkdown $enRoot)
$zhPages = @(Get-RelativeMarkdown $zhRoot)

$missingZh = @($enPages | Where-Object { $_ -notin $zhPages })
$missingEn = @($zhPages | Where-Object { $_ -notin $enPages })

foreach ($page in $missingZh) {
    Add-Failure "Missing Chinese page for docs/en/$page"
}
foreach ($page in $missingEn) {
    Add-Failure "Missing English page for docs/zh/$page"
}

foreach ($page in $enPages) {
    $enFile = Join-Path $enRoot ($page -replace "/", [System.IO.Path]::DirectorySeparatorChar)
    $zhFile = Join-Path $zhRoot ($page -replace "/", [System.IO.Path]::DirectorySeparatorChar)
    if (-not (Test-Path $zhFile)) {
        continue
    }

    $enHeadingLevels = @(
        Select-String -Path $enFile -Pattern "^(#{1,6}) " |
            ForEach-Object { $_.Matches[0].Groups[1].Value.Length }
    )
    $zhHeadingLevels = @(
        Select-String -Path $zhFile -Pattern "^(#{1,6}) " |
            ForEach-Object { $_.Matches[0].Groups[1].Value.Length }
    )

    if (($enHeadingLevels -join ",") -ne ($zhHeadingLevels -join ",")) {
        Add-Failure "Heading level structure differs for docs/en/$page and docs/zh/$page"
    }
}

$scanRoots = @("README.md", "docs", "examples", "include/xgl/xgl.h") |
    ForEach-Object { Join-Path $RepoRoot $_ }

$scanFiles = @()
foreach ($root in $scanRoots) {
    if (Test-Path -Path $root -PathType Leaf) {
        $scanFiles += $root
    } elseif (Test-Path -Path $root -PathType Container) {
        $scanFiles += @(Get-ChildItem -Path $root -Recurse -File |
            Where-Object { $_.Extension -in @(".md", ".h", ".c", ".yml", ".yaml") } |
            ForEach-Object { $_.FullName })
    }
}

$forbiddenPatterns = @(
    "config\.enable_fragmentation",
    "config\.max_retry_count",
    "config\.ack_timeout_ms",
    "config\.window_size",
    "config\.max_frame_size",
    "config\.tx_pool_size",
    "config\.rx_buffer_size",
    "xgl_route_add",
    "xgl_route_remove",
    "xgl_route_update_metric",
    "ACK/NACK",
    "stats\.tx_packets",
    "stats\.rx_packets",
    "stats\.tx_errors",
    "stats\.rx_errors"
)

foreach ($pattern in $forbiddenPatterns) {
    $matches = @(Select-String -Path $scanFiles -Pattern $pattern -ErrorAction SilentlyContinue)
    foreach ($match in $matches) {
        Add-Failure "Forbidden stale docs/API pattern '$pattern' in $($match.Path):$($match.LineNumber)"
    }
}

$todoFiles = @(Get-ChildItem -Path (Join-Path $RepoRoot "docs"), (Join-Path $RepoRoot "examples") -Recurse -File |
    Where-Object { $_.Extension -in @(".md", ".h", ".c") } |
    ForEach-Object { $_.FullName })
$todoMatches = @(Select-String -Path $todoFiles -Pattern "TODO\(xgen-link\)" -ErrorAction SilentlyContinue)
foreach ($match in $todoMatches) {
    if ($match.Line -notmatch "TODO\(xgen-link\): confirm ") {
        Add-Failure "Non-standard TODO format in $($match.Path):$($match.LineNumber)"
    }
}

$requiredProtocolRefs = @{
    "wire-format.md" = "test/unit/wire/test_wire.cpp"
    "extensions.md" = "test/unit/wire/test_wire.cpp"
    "reliability.md" = "test/unit/transport/test_reliable.cpp"
    "security.md" = "test/unit/security/test_security.cpp"
    "routing.md" = "test/unit/network/test_network.cpp"
}

foreach ($entry in $requiredProtocolRefs.GetEnumerator()) {
    if (-not (Test-Path -PathType Leaf (Join-Path $RepoRoot $entry.Value))) {
        Add-Failure "Missing protocol test source $($entry.Value)"
    }
    foreach ($language in @("en", "zh")) {
        $page = "docs/$language/reference/protocol/v1/$($entry.Key)"
        $file = Join-Path $RepoRoot $page
        if (-not (Test-Path -PathType Leaf $file)) {
            Add-Failure "Missing protocol page $page"
            continue
        }
        $content = Get-Content -Raw -Encoding UTF8 $file
        if ($content -notmatch [regex]::Escape($entry.Value)) {
            Add-Failure "$page does not reference $($entry.Value)"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Documentation QA failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Documentation QA passed."
