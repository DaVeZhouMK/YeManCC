[CmdletBinding()]
param(
  [string]$ProjectRoot = '',
  [ValidateSet('prepare','formal')][string]$Mode = 'prepare',
  [string]$Repository = $env:GITHUB_REPOSITORY,
  [string]$ExpectedCommit = $env:GITHUB_SHA,
  [Parameter(Mandatory=$true)][string]$EvidencePath
)
$ErrorActionPreference = 'Stop'
# This command has no write API. A successful GET is not proof of write permission.
$PSNativeCommandUseErrorActionPreference = $false
if (-not $ProjectRoot) { $ProjectRoot = Split-Path -Parent $PSScriptRoot }
$root = [IO.Path]::GetFullPath($ProjectRoot)
$blockers = [Collections.Generic.List[object]]::new()
$warnings = [Collections.Generic.List[string]]::new()
$requests = [Collections.Generic.List[object]]::new()
$receipt = [ordered]@{
  schemaVersion = 1; checkedAt = (Get-Date).ToUniversalTime().ToString('o'); mode = $Mode
  repository = $Repository; version = $null; tag = $null; commit = $null
  mainCommit = $null; mainVersion = $null; mainProtected = $null; mainRuleTypes = @()
  tagCommit = $null; existingRelease = $null
  writePermission = 'NOT_PROVEN_READ_ONLY'; releaseUpload = 'NOT_EXECUTED'
  mainManifestWrite = 'NOT_EXECUTED'; fixtureValidation = 'NOT_RUN'; packageBuild = 'NOT_RUN'
}
function Add-Blocker([string]$Code, [string]$Message) {
  $blockers.Add([ordered]@{code=$Code;message=$Message})
}
function Read-Version([string]$Value) {
  if ($Value -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw "Invalid release version: $Value" }
  foreach ($part in $Value.Split('.')) { if ([int64]$part -gt [int]::MaxValue) { throw "Version out of range: $Value" } }
  return [version]$Value
}
function Read-GitHub([string]$Endpoint, [int[]]$Allowed = @(200)) {
  # Explicit GET even for list endpoints; never use field flags which imply POST.
  for ($attempt = 1; $attempt -le 3; $attempt++) {
    $raw = (& gh api --method GET --hostname github.com --include -H 'Accept: application/vnd.github+json' $Endpoint 2>$null) -join "`n"
    $exitCode = $LASTEXITCODE
    $match = [regex]::Match($raw, '\AHTTP/\S+\s+(\d{3})')
    $status = if ($match.Success) { [int]$match.Groups[1].Value } else { 0 }
    $requests.Add([ordered]@{method='GET';endpoint=$Endpoint;status=$status;attempt=$attempt})
    if (($status -eq 0 -or $status -eq 429 -or $status -ge 500) -and $attempt -lt 3) {
      Start-Sleep -Seconds $attempt
      continue
    }
    if ($status -notin $Allowed -or ($status -eq 200 -and $exitCode -ne 0)) {
      throw "GitHub read failed (HTTP $status, exit $exitCode): $Endpoint"
    }
    $boundary = $raw.IndexOf("`n`n")
    if ($boundary -lt 0) { throw "Missing GitHub response body: $Endpoint" }
    $body = $raw.Substring($boundary + 2) | ConvertFrom-Json -NoEnumerate
    return [pscustomobject]@{status=$status;data=$body}
  }
}
try {
  if ($Repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') { throw 'Expected an explicit GitHub owner/repository.' }
  $source = Get-Content -LiteralPath (Join-Path $root 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $package = Get-Content -LiteralPath (Join-Path $root 'package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $incoming = Read-Version ([string]$source.version)
  $receipt.version = [string]$source.version
  $receipt.tag = 'v' + $receipt.version
  if ([string]$package.version -cne $receipt.version) { Add-Blocker 'VERSION_MISMATCH' 'package.json and version.json differ.' }
  if (-not (Test-Path -LiteralPath (Join-Path $root "docs/release-notes/$($receipt.tag).md") -PathType Leaf)) {
    Add-Blocker 'RELEASE_NOTES_MISSING' 'Version-specific release notes are missing.'
  }
  $commit = (& git -C $root rev-parse HEAD).Trim()
  if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[a-f0-9]{40}$') { throw 'Cannot resolve candidate Git commit.' }
  $receipt.commit = $commit
  if ($ExpectedCommit -and $ExpectedCommit -cne $commit) { Add-Blocker 'IMMUTABLE_COMMIT_MISMATCH' 'Checkout differs from the requested commit.' }
  $repo = (Read-GitHub "repos/$Repository").data
  if ($repo.archived -or $repo.disabled) { Add-Blocker 'REPOSITORY_UNWRITABLE' 'Repository is archived or disabled.' }
  $branch = (Read-GitHub "repos/$Repository/branches/main").data
  $receipt.mainCommit = [string]$branch.commit.sha
  $receipt.mainProtected = [bool]$branch.protected
  # Bind the version comparison to the observed main commit, not a moving ref.
  $manifest = (Read-GitHub "repos/$Repository/contents/version.json?ref=$($receipt.mainCommit)").data
  if ($manifest.type -cne 'file' -or $manifest.encoding -cne 'base64') { throw 'Unexpected main version manifest content response.' }
  $main = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String(($manifest.content -replace '\s',''))).TrimStart([char]0xFEFF) | ConvertFrom-Json
  $receipt.mainVersion = [string]$main.version
  if ($incoming -lt (Read-Version $receipt.mainVersion)) { Add-Blocker 'VERSION_DOWNGRADE' 'Candidate version is older than main/version.json.' }
  $refs = @((Read-GitHub "repos/$Repository/git/matching-refs/tags/$($receipt.tag)").data | Where-Object { $_.ref -ceq "refs/tags/$($receipt.tag)" })
  if ($refs.Count -gt 1) { throw 'Duplicate exact tag references returned.' }
  if ($refs.Count -eq 1) {
    $object = $refs[0].object
    for ($depth=0; $object.type -eq 'tag' -and $depth -lt 4; $depth++) {
      $object = (Read-GitHub "repos/$Repository/git/tags/$($object.sha)").data.object
    }
    if ($object.type -cne 'commit') { throw 'Release tag does not resolve to a commit.' }
    $receipt.tagCommit = [string]$object.sha
    if ($Mode -eq 'prepare') { Add-Blocker 'VERSION_TAG_OCCUPIED' 'Preparation version already has a remote tag; choose the next version before publishing.' }
    elseif ($receipt.tagCommit -cne $commit) { Add-Blocker 'REMOTE_TAG_COMMIT_MISMATCH' 'Remote release tag differs from the candidate commit.' }
  } elseif ($Mode -eq 'formal') {
    Add-Blocker 'FORMAL_TAG_MISSING' 'Formal workflow requires the version tag to already exist; the publisher must not create it implicitly.'
  }
  # Includes drafts visible to this token. Pagination prevents old collisions being hidden.
  $allReleasesRead = $false
  for ($page=1; $page -le 10; $page++) {
    $releases = @((Read-GitHub "repos/$Repository/releases?per_page=100&page=$page").data)
    $existing = @($releases | Where-Object { $_.tag_name -ceq $receipt.tag })
    if ($existing.Count) {
      $receipt.existingRelease = @($existing | ForEach-Object { [ordered]@{id=$_.id;draft=$_.draft;publishedAt=$_.published_at;assetNames=@($_.assets | ForEach-Object { $_.name })} })
      Add-Blocker 'RELEASE_ALREADY_EXISTS' 'A release or visible draft already uses this version; inspect the existing run instead of overwriting assets.'
      $allReleasesRead = $true
      break
    }
    if ($releases.Count -lt 100) { $allReleasesRead = $true; break }
  }
  if (-not $allReleasesRead) { Add-Blocker 'RELEASE_HISTORY_LIMIT' 'Release history exceeds the bounded preflight scan; absence of a collision is not proven.' }
  $rules = Read-GitHub "repos/$Repository/rules/branches/main" @(200,403,404)
  if ($rules.status -eq 200) {
    $receipt.mainRuleTypes = @($rules.data | ForEach-Object { [string]$_.type } | Sort-Object -Unique)
    # Deletion/force-push/linear-history rules permit the normal single-file commit.
    $reviewRules = @($receipt.mainRuleTypes | Where-Object { $_ -notin @('deletion','non_fast_forward','required_linear_history') })
    if ($reviewRules.Count) {
      Add-Blocker 'MAIN_RULES_REQUIRE_REVIEW' ('main update rules require actor/bypass review: ' + ($reviewRules -join ', '))
    }
    if ($receipt.mainProtected) {
      # Applicable-rules GET excludes classic branch protection; inspect it separately.
      $classic = Read-GitHub "repos/$Repository/branches/main/protection" @(200,403,404)
      if ($classic.status -ne 404) {
        Add-Blocker 'MAIN_PROTECTION_REQUIRE_REVIEW' 'Classic main protection exists or is unreadable; verify the publishing actor can commit version.json.'
      }
    }
  } elseif ($receipt.mainProtected) {
    Add-Blocker 'MAIN_RULES_UNKNOWN' 'main is protected and applicable rules could not be read by this token.'
  } else {
    $warnings.Add('Applicable rules endpoint unavailable to this token; branch reports unprotected. Write permission remains unproven.')
  }
  $warnings.Add('Read-only API access does not prove GITHUB_TOKEN release upload or contents-write permission. Invisible drafts and permission changes remain formal-run checks.')
} catch {
  Add-Blocker 'PREFLIGHT_READ_OR_SOURCE_FAILED' $_.Exception.Message
}
$receipt.status = if ($blockers.Count) { 'BLOCKED' } else { 'PASS_READONLY_PREFLIGHT' }
$receipt.blockers = $blockers.ToArray()
$receipt.warnings = $warnings.ToArray()
$receipt.requests = $requests.ToArray()
$evidence = [IO.Path]::GetFullPath($EvidencePath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($evidence)) | Out-Null
[IO.File]::WriteAllText($evidence, (($receipt | ConvertTo-Json -Depth 12) + "`n"), [Text.UTF8Encoding]::new($false))
Write-Output ("GITHUB_RELEASE_PREFLIGHT {0} version={1} mode={2}" -f $receipt.status,$receipt.version,$Mode)
foreach ($blocker in $blockers) { Write-Output ("{0}: {1}" -f $blocker.code,$blocker.message) }
if ($blockers.Count) { throw "GitHub release preflight blocked; see $evidence" }