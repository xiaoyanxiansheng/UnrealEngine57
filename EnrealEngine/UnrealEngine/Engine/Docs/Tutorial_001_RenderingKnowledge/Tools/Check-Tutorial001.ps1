param(
    [string]$TutorialRoot = (Split-Path -Parent $PSScriptRoot)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TutorialRoot = [System.IO.Path]::GetFullPath($TutorialRoot)
$DocsRoot = Split-Path -Parent $TutorialRoot
$EngineRoot = Split-Path -Parent $DocsRoot
$RepoRoot = Split-Path -Parent $EngineRoot
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$script:Failures = New-Object 'System.Collections.Generic.List[string]'

function Add-Failure {
    param([string]$Message)
    [void]$script:Failures.Add($Message)
}

function Read-Utf8 {
    param([string]$Path)
    return [System.IO.File]::ReadAllText($Path, $Utf8NoBom)
}

function Get-DisplayPath {
    param([string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    if ($full.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($RepoRoot.Length).TrimStart('\') -replace '\\', '/'
    }
    return $full -replace '\\', '/'
}

function Remove-FencedCode {
    param([string]$Text)
    $lines = $Text -split "`r?`n"
    $kept = New-Object 'System.Collections.Generic.List[string]'
    $inFence = $false
    foreach ($line in $lines) {
        if ($line -match '^\s*```') {
            $inFence = -not $inFence
            continue
        }
        if (-not $inFence) {
            [void]$kept.Add($line)
        }
    }
    return ($kept -join "`n")
}

function Get-MarkdownLinks {
    param([string]$Text)
    $clean = Remove-FencedCode $Text
    return [regex]::Matches($clean, '(?<!!)\[[^\]\r\n]+\]\((?<target>[^)\r\n]+)\)')
}

function Resolve-LocalLink {
    param(
        [string]$SourceFile,
        [string]$Target
    )

    $targetText = $Target.Trim()
    if ($targetText.StartsWith('<') -and $targetText.EndsWith('>')) {
        $targetText = $targetText.Substring(1, $targetText.Length - 2)
    }

    if ($targetText -match '^[a-zA-Z][a-zA-Z0-9+.-]*:') {
        return $null
    }

    if ($targetText -match '\s') {
        return $null
    }

    $fragment = ''
    $filePart = $targetText
    $hashIndex = $targetText.IndexOf('#')
    if ($hashIndex -ge 0) {
        $filePart = $targetText.Substring(0, $hashIndex)
        $fragment = $targetText.Substring($hashIndex + 1)
    }

    if ($filePart -eq '') {
        $resolvedPath = [System.IO.Path]::GetFullPath($SourceFile)
    }
    else {
        $localPart = $filePart -replace '/', [System.IO.Path]::DirectorySeparatorChar
        $resolvedPath = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $SourceFile) $localPart))
    }

    return [PSCustomObject]@{
        Raw = $targetText
        Path = $resolvedPath
        Fragment = $fragment
    }
}

function Count-TopicAnchor {
    param(
        [string]$Path,
        [string]$Slug
    )
    if (-not (Test-Path -LiteralPath $Path)) {
        return 0
    }
    $text = Remove-FencedCode (Read-Utf8 $Path)
    $pattern = '<a\s+id="' + [regex]::Escape($Slug) + '"></a>'
    return [regex]::Matches($text, $pattern).Count
}

function Get-TutorialRelativePath {
    param([string]$Path)
    $full = [System.IO.Path]::GetFullPath($Path)
    return $full.Substring($TutorialRoot.Length).TrimStart('\') -replace '\\', '/'
}

function Get-SourceSymbolTokens {
    param([string]$Symbols)
    $tokens = New-Object 'System.Collections.Generic.List[string]'
    foreach ($token in ($Symbols -split '[;,]')) {
        $trimmed = $token.Trim()
        if ($trimmed.Length -gt 0) {
            [void]$tokens.Add($trimmed)
        }
    }
    return $tokens
}

function Test-SourceSymbolToken {
    param(
        [string]$Path,
        [string]$Token
    )
    $match = Select-String -LiteralPath $Path -SimpleMatch -Pattern $Token -List -ErrorAction Stop | Select-Object -First 1
    return $null -ne $match
}

function Get-LineDebtCount {
    param([string]$Text)
    $patterns = @(
        '(?i)~?\bLine\s*~?\d+(?:\s*[-–~+]\s*\d+\+?)?',
        '(?i)文件\(行号\)|文件:行号|行号',
        '(?i)\.(?:cpp|h|usf|inl|hlsl|ush)\s*:\s*\d+(?:\s*[-–~,]\s*\d+)*'
    )
    $count = 0
    foreach ($pattern in $patterns) {
        $count += [regex]::Matches($Text, $pattern).Count
    }
    return $count
}

Write-Host "Tutorial_001 check root: $(Get-DisplayPath $TutorialRoot)"

$markdownFiles = Get-ChildItem -Path $TutorialRoot -Recurse -File -Filter '*.md'
$topicSlugs = New-Object 'System.Collections.Generic.HashSet[string]'
$weakSourceTokens = @(
    'BasePass',
    'GBuffer',
    'Shadow',
    'Nanite',
    'Lumen',
    'MegaLights',
    'PostProcessing',
    'Visibility',
    'Init',
    'PreInit'
)

Write-Host 'Checking Markdown local links...'
foreach ($file in $markdownFiles) {
    $text = Read-Utf8 $file.FullName
    foreach ($match in Get-MarkdownLinks $text) {
        $link = Resolve-LocalLink -SourceFile $file.FullName -Target $match.Groups['target'].Value
        if ($null -eq $link) {
            continue
        }
        if (-not (Test-Path -LiteralPath $link.Path)) {
            Add-Failure "Broken local link in $(Get-DisplayPath $file.FullName): $($link.Raw)"
            continue
        }
        if ($link.Fragment -like 'topic-*') {
            $count = Count-TopicAnchor -Path $link.Path -Slug $link.Fragment
            if ($count -ne 1) {
                Add-Failure "Topic slug link does not resolve uniquely in $(Get-DisplayPath $file.FullName): $($link.Raw) (count=$count)"
            }
        }
    }
}

Write-Host 'Checking topic anchor uniqueness...'
$anchorsBySlug = @{}
foreach ($file in $markdownFiles) {
    $text = Remove-FencedCode (Read-Utf8 $file.FullName)
    foreach ($match in [regex]::Matches($text, '<a\s+id="(?<slug>topic-[^"]+)"></a>')) {
        $slug = $match.Groups['slug'].Value
        if (-not $anchorsBySlug.ContainsKey($slug)) {
            $anchorsBySlug[$slug] = New-Object 'System.Collections.Generic.List[string]'
        }
        [void]$anchorsBySlug[$slug].Add((Get-DisplayPath $file.FullName))
    }
}

foreach ($slug in $anchorsBySlug.Keys) {
    if ($anchorsBySlug[$slug].Count -ne 1) {
        Add-Failure "Duplicate topic slug '$slug': $($anchorsBySlug[$slug] -join ', ')"
    }
}

Write-Host 'Checking shortcut card headings and Deep links...'
$shortcutCards = $markdownFiles | Where-Object {
    $_.BaseName -match '^\d+\.\d+_' -and $_.Name -notlike '*_Deep.md'
}

foreach ($card in $shortcutCards) {
    $prefix = ([regex]::Match($card.BaseName, '^(\d+\.\d+)_')).Groups[1].Value
    $lines = (Read-Utf8 $card.FullName) -split "`r?`n"
    $firstHeading = $lines | Where-Object { $_ -match '^#\s+' } | Select-Object -First 1
    if ($null -eq $firstHeading -or $firstHeading -notmatch ('^#\s+' + [regex]::Escape($prefix) + '(\s|$)')) {
        Add-Failure "Shortcut title number mismatch in $(Get-DisplayPath $card.FullName): expected '# $prefix ...', got '$firstHeading'"
    }

    $deepLinks = New-Object 'System.Collections.Generic.List[object]'
    foreach ($match in Get-MarkdownLinks (Read-Utf8 $card.FullName)) {
        $raw = $match.Groups['target'].Value.Trim()
        $filePart = $raw
        $hashIndex = $raw.IndexOf('#')
        if ($hashIndex -ge 0) {
            $filePart = $raw.Substring(0, $hashIndex)
        }
        if ($filePart -like '*_Deep.md') {
            $link = Resolve-LocalLink -SourceFile $card.FullName -Target $raw
            if ($null -ne $link) {
                [void]$deepLinks.Add($link)
            }
        }
    }

    if ($deepLinks.Count -eq 0) {
        Add-Failure "Shortcut card has no Deep link: $(Get-DisplayPath $card.FullName)"
    }
    foreach ($link in $deepLinks) {
        if (-not (Test-Path -LiteralPath $link.Path)) {
            Add-Failure "Shortcut card Deep link is broken in $(Get-DisplayPath $card.FullName): $($link.Raw)"
        }
    }
}

Write-Host 'Checking TOPIC_INDEX Deep coverage and statuses...'
$topicIndexPath = Join-Path $TutorialRoot 'TOPIC_INDEX.md'
if (-not (Test-Path -LiteralPath $topicIndexPath)) {
    Add-Failure 'Missing TOPIC_INDEX.md'
}
else {
    $topicText = Read-Utf8 $topicIndexPath
    $allowedStatuses = @('indexed', 'needs_expansion', 'verified')
    foreach ($line in ($topicText -split "`r?`n")) {
        if ($line -match '^\|\s*`topic-[^`]+`') {
            $slugMatch = [regex]::Match($line, '^\|\s*`(?<slug>topic-[^`]+)`')
            if ($slugMatch.Success) {
                [void]$topicSlugs.Add($slugMatch.Groups['slug'].Value)
            }

            $statusMatch = [regex]::Match($line, '\|\s*`(?<status>[^`]+)`\s*\|$')
            if (-not $statusMatch.Success -or $allowedStatuses -notcontains $statusMatch.Groups['status'].Value) {
                Add-Failure "Invalid TOPIC_INDEX status row: $line"
            }
        }
    }

    if ($topicSlugs.Count -eq 0) {
        Add-Failure 'TOPIC_INDEX.md contains no topic slug rows'
    }

    foreach ($slug in $anchorsBySlug.Keys) {
        if (-not $topicSlugs.Contains($slug)) {
            Add-Failure "Topic anchor is not listed in TOPIC_INDEX.md: $slug ($($anchorsBySlug[$slug] -join ', '))"
        }
    }

    $topicDeepTargets = New-Object 'System.Collections.Generic.HashSet[string]'
    foreach ($match in Get-MarkdownLinks $topicText) {
        $raw = $match.Groups['target'].Value.Trim()
        $filePart = $raw
        $hashIndex = $raw.IndexOf('#')
        if ($hashIndex -ge 0) {
            $filePart = $raw.Substring(0, $hashIndex)
        }
        if ($filePart -like '*_Deep.md') {
            $link = Resolve-LocalLink -SourceFile $topicIndexPath -Target $raw
            if ($null -ne $link -and $link.Path.StartsWith($TutorialRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
                [void]$topicDeepTargets.Add((Get-TutorialRelativePath $link.Path))
            }
        }
    }

    $deepDocs = $markdownFiles | Where-Object { $_.Name -like '*_Deep.md' }
    foreach ($deep in $deepDocs) {
        $rel = Get-TutorialRelativePath $deep.FullName
        if (-not $topicDeepTargets.Contains($rel)) {
            Add-Failure "Deep document is not referenced by TOPIC_INDEX.md: $rel"
        }
    }
}

Write-Host 'Checking SOURCE_INDEX source paths and symbols...'
$sourceIndexPath = Join-Path $TutorialRoot 'SOURCE_INDEX.md'
if (-not (Test-Path -LiteralPath $sourceIndexPath)) {
    Add-Failure 'Missing SOURCE_INDEX.md'
}
else {
    $sourceText = Read-Utf8 $sourceIndexPath
    $sourceRows = New-Object 'System.Collections.Generic.List[object]'
    foreach ($line in ($sourceText -split "`r?`n")) {
        $rowMatch = [regex]::Match($line, '^\|\s*`(?<slug>topic-[^`]+)`\s*\|\s*`(?<path>Engine/(?:Source|Shaders)/[^`]+)`\s*\|\s*`(?<symbols>[^`]+)`\s*\|')
        if ($rowMatch.Success) {
            [void]$sourceRows.Add([PSCustomObject]@{
                Slug = $rowMatch.Groups['slug'].Value
                Path = $rowMatch.Groups['path'].Value
                Symbols = $rowMatch.Groups['symbols'].Value
            })
        }
    }

    if ($sourceRows.Count -eq 0) {
        Add-Failure 'SOURCE_INDEX.md contains no parseable source rows'
    }

    foreach ($row in $sourceRows) {
        if (-not $topicSlugs.Contains($row.Slug)) {
            Add-Failure "SOURCE_INDEX slug is not listed in TOPIC_INDEX.md: $($row.Slug)"
        }

        $relative = $row.Path -replace '/', [System.IO.Path]::DirectorySeparatorChar
        $full = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $relative))
        $sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'Engine\Source'))
        $shaderRoot = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot 'Engine\Shaders'))
        $isUnderAllowedRoot = $full.StartsWith($sourceRoot, [System.StringComparison]::OrdinalIgnoreCase) -or $full.StartsWith($shaderRoot, [System.StringComparison]::OrdinalIgnoreCase)
        if (-not $isUnderAllowedRoot) {
            Add-Failure "SOURCE_INDEX path is outside allowed roots: $($row.Path)"
        }
        elseif (-not (Test-Path -LiteralPath $full)) {
            Add-Failure "SOURCE_INDEX path does not exist: $($row.Path)"
        }
        else {
            $tokens = @(Get-SourceSymbolTokens $row.Symbols)
            if ($tokens.Count -eq 0) {
                Add-Failure "SOURCE_INDEX row has no source symbol tokens: $($row.Slug) -> $($row.Path)"
            }

            foreach ($token in $tokens) {
                if ($weakSourceTokens -ccontains $token) {
                    Add-Failure "SOURCE_INDEX uses weak source token '$token': $($row.Slug) -> $($row.Path)"
                    continue
                }

                if (-not (Test-SourceSymbolToken -Path $full -Token $token)) {
                    Add-Failure "SOURCE_INDEX source token not found: '$token' in $($row.Path) for $($row.Slug)"
                }
            }
        }
    }
}

Write-Host 'LineDebtReport: fixed line-number debt (non-blocking)...'
$lineDebtTargets = $markdownFiles | Where-Object {
    $_.Name -like '*_Deep.md' -or
    $_.BaseName -match '^\d+\.\d+_' -or
    $_.FullName.StartsWith((Join-Path $TutorialRoot 'Reference'), [System.StringComparison]::OrdinalIgnoreCase)
}
$lineDebtRows = New-Object 'System.Collections.Generic.List[object]'
foreach ($file in $lineDebtTargets) {
    $count = Get-LineDebtCount (Read-Utf8 $file.FullName)
    if ($count -gt 0) {
        [void]$lineDebtRows.Add([PSCustomObject]@{
            Path = Get-DisplayPath $file.FullName
            Count = $count
        })
    }
}
if ($lineDebtRows.Count -eq 0) {
    Write-Host ' - none'
}
else {
    foreach ($row in ($lineDebtRows | Sort-Object -Property @{ Expression = 'Count'; Descending = $true }, @{ Expression = 'Path'; Ascending = $true })) {
        Write-Host " - $($row.Path): $($row.Count)"
    }
}

if ($script:Failures.Count -gt 0) {
    Write-Host ''
    Write-Host "FAILED: $($script:Failures.Count) issue(s)"
    foreach ($failure in $script:Failures) {
        Write-Host " - $failure"
    }
    exit 1
}

Write-Host ''
Write-Host 'OK: Tutorial_001 structure checks passed.'
exit 0
