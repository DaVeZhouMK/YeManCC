param(
    [Parameter(Mandatory)][string]$UpstreamFactory,
    [Parameter(Mandatory)][string]$UpstreamMotherboard
)
$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $PSScriptRoot
$inputs = [ordered]@{}
function Read-Pinned([string]$Name, [string]$Path, [string]$Expected) {
    $file = (Resolve-Path -LiteralPath $Path).Path
    $bytes = [IO.File]::ReadAllBytes($file)
    $sha = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant()
    if ($Expected -and $sha -cne $Expected) { throw "$Name input does not match the fixed revision" }
    $inputs[$Name] = [ordered]@{ path = $file; bytes = $bytes.Length; sha256 = $sha }
    [Text.Encoding]::UTF8.GetString($bytes).TrimStart([char]0xFEFF)
}

$roslyn = @('Microsoft.CodeAnalysis.dll', 'Microsoft.CodeAnalysis.CSharp.dll' | ForEach-Object { Join-Path $PSHOME $_ })
Add-Type -Path $roslyn
foreach ($dll in $roslyn) { $null = Read-Pinned ([IO.Path]::GetFileNameWithoutExtension($dll)) $dll }
$null = Read-Pinned 'tool' $PSCommandPath

function Get-Literals($Labels) {
    foreach ($label in $Labels) {
        if ($label -is [Microsoft.CodeAnalysis.CSharp.Syntax.CaseSwitchLabelSyntax] -and
            $label.Value -is [Microsoft.CodeAnalysis.CSharp.Syntax.LiteralExpressionSyntax] -and
            $label.Value.Token.Value -is [string]) { $label }
    }
}
function Get-ExactSelectors([string]$Factory, [string]$Board, [string]$Label) {
    $bindings = [ordered]@{
        ProductName = @('Product', 'baseboard', 'Win32_BaseBoard', 'Product', 'boardProduct')
        SystemModel = @('SystemModel', 'computer', 'Win32_ComputerSystem', 'Model', 'systemProduct')
        ManufacturerName = @('Manufacturer', 'baseboard', 'Win32_BaseBoard', 'Manufacturer', $null)
    }
    foreach ($key in $bindings.Keys) {
        $member, $cache, $type, $property = $bindings[$key][0..3]
        if (!$Factory.Contains("var $key = MotherboardInfo.$member") -or
            !$Board.Contains(('{{ "{0}", {0}Searcher }}' -f $cache)) -or
            !$Board.Contains(('SELECT * FROM {0}"' -f $type)) -or
            !$Board.Contains(('string {0} => Convert.ToString(queryCacheValue("{1}", "{2}"))' -f $member, $cache, $property))) {
            throw "$Label DMI source changed: $key"
        }
    }
    $tree = [Microsoft.CodeAnalysis.CSharp.CSharpSyntaxTree]::ParseText($Factory)
    $errors = @($tree.GetDiagnostics() | Where-Object Severity -EQ Error)
    if ($errors.Count) { throw "$Label factory parse failed: $($errors[0])" }
    $outer = @($tree.GetRoot().DescendantNodes() | Where-Object {
        $_ -is [Microsoft.CodeAnalysis.CSharp.Syntax.SwitchStatementSyntax] -and $_.Expression.ToString() -ceq 'ManufacturerName'
    })
    if ($outer.Count -ne 1) { throw "$Label manufacturer switch not unique" }
    foreach ($maker in $outer[0].Sections) {
        $manufacturers = @(Get-Literals $maker.Labels)
        if (!$manufacturers.Count) { continue }
        # Require an unconditional selector and an unconditional single assignment.
        $direct = @($maker.Statements | ForEach-Object {
            if ($_ -is [Microsoft.CodeAnalysis.CSharp.Syntax.BlockSyntax]) { $_.Statements } else { $_ }
        })
        foreach ($selector in $direct) {
            if ($selector -isnot [Microsoft.CodeAnalysis.CSharp.Syntax.SwitchStatementSyntax]) { continue }
            $field = $selector.Expression.ToString()
            if ($field -cnotin @('ProductName', 'SystemModel')) { continue }
            foreach ($part in $selector.Sections) {
                $products = @(Get-Literals $part.Labels)
                if (!$products.Count -or $part.Labels.Count -ne $products.Count -or $part.Statements.Count -ne 2 -or
                    $part.Statements[1] -isnot [Microsoft.CodeAnalysis.CSharp.Syntax.BreakStatementSyntax] -or
                    $part.Statements[0] -isnot [Microsoft.CodeAnalysis.CSharp.Syntax.ExpressionStatementSyntax]) { continue }
                $assignment = $part.Statements[0].Expression
                if ($assignment -isnot [Microsoft.CodeAnalysis.CSharp.Syntax.AssignmentExpressionSyntax] -or
                    $assignment.Left.ToString() -cne 'device' -or
                    $assignment.Right -isnot [Microsoft.CodeAnalysis.CSharp.Syntax.ObjectCreationExpressionSyntax]) { continue }
                foreach ($m in $manufacturers) { foreach ($p in $products) {
                    [pscustomobject]@{
                        manufacturer = $m.Value.Token.Value; field = $bindings[$field][4]; value = $p.Value.Token.Value
                        deviceClass = $assignment.Right.Type.ToString(); line = $tree.GetLineSpan($p.Span).StartLinePosition.Line + 1
                    }
                } }
            }
        }
    }
}

$locked = @(Get-ExactSelectors `
    (Read-Pinned 'lockedFactory' (Join-Path $Repo 'deps/handheldcompanion-runtime/source/Devices/IDevice.cs') 'fbaed1dfd329e3328376488b8ec70c3a7fdcf97d70dcb37f1525ee33aa169c20') `
    (Read-Pinned 'lockedBoard' (Join-Path $Repo 'deps/handheldcompanion-runtime/source/Misc/MotherboardInfo.cs') '0b30eb3d65ef1ba14112c24d5311b54e04953e4f97931793d4456ea5b61a0c41') 'locked')
$upstream = @(Get-ExactSelectors `
    (Read-Pinned 'upstreamFactory' $UpstreamFactory 'f608b6f5a165e8b74a48d09d81fa5316c72273c0efd721327b1d2deccf1d5a47') `
    (Read-Pinned 'upstreamBoard' $UpstreamMotherboard '42b6fcf99721e978c04dd05530ef2a37c477a63a61b46e5fbdffe864aef0a1bc') 'upstream')
if (!$locked.Count -or !$upstream.Count) { throw 'No exact HC selectors found' }
$routes = @(Read-Pinned 'routes' (Join-Path $Repo 'native/BatteryHost/battery-routes.json') | ConvertFrom-Json)
$battery = (Read-Pinned 'catalog' (Join-Path $Repo 'capabilities/hc-device-catalog.json') | ConvertFrom-Json).domains.battery
if ($battery.ruleDocumentSha256 -cne $inputs.routes.sha256 -or $routes.Count -ne $battery.rows.Count) { throw 'Battery route/catalog mismatch' }

function Find-Witness($Selectors, $Manufacturer, $Value, $Class) {
    @($Selectors | Where-Object { $_.manufacturer -ceq $Manufacturer -and $_.value -ceq $Value -and $_.deviceClass -ceq $Class })
}
$comparisons = @(for ($i = 0; $i -lt $battery.rows.Count; $i++) {
    $row = $battery.rows[$i]
    $id = $row.identity
    $runtime = $routes[$i]
    if ($row.index -ne $i -or $row.provider -cne $runtime.route -or
        (ConvertTo-Json -Depth 12 -Compress -InputObject $id) -cne (ConvertTo-Json -Depth 12 -Compress -InputObject $runtime.identity)) {
        throw "Rule identity projection changed: $($row.ruleId)"
    }
    foreach ($m in $id.manufacturers) { foreach ($v in $id.values) {
        $older = @(Find-Witness $locked $m $v $id.deviceClass)
        $newer = @(Find-Witness $upstream $m $v $id.deviceClass)
        $status = if ($id.deviceClass -ceq 'OneXPlayerSuperX' -and $row.origin -ceq 'reviewed-native-extension' -and
            $m -ceq 'ONE-NETBOOK' -and $v -ceq 'ONEXPLAYER SUPER X' -and $id.field -ceq 'boardProduct' -and
            $id.manufacturerMatch -ceq 'ordinal-contains' -and $id.valueMatch -ceq 'ordinal-exact' -and
            !$older.Count -and !$newer.Count) { 'separate-reviewed-extension' }
            elseif ($newer.Count -eq 1 -and $newer[0].field -cne $id.field) { 'upstream-field-diverges' }
            elseif ($newer.Count -eq 1 -and $older.Count -eq 1 -and $older[0].field -ceq $id.field) { 'same-field-both-pins' }
            elseif ($newer.Count -eq 1 -and !$older.Count) { 'same-field-upstream-only' }
            else { 'requires-source-reconciliation' }
        [ordered]@{
            ruleId = $row.ruleId; model = $id.deviceClass; provider = $row.provider; manufacturer = $m; value = $v; field = $id.field
            processorPattern = $id.processorPattern; firmwareGate = $row.firmwareGate.kind; status = $status
            locked = $older; upstream = $newer
        }
    } }
})
$counts = [ordered]@{}
$comparisons | Group-Object -Property { $_.status } | ForEach-Object { $counts[$_.Name] = $_.Count }
$upstreamUnlisted = @(foreach ($selector in $upstream) {
    $classRows = @($comparisons | Where-Object { $_.model -ceq $selector.deviceClass })
    if (!$classRows.Count) { continue }
    $literal = @($classRows | Where-Object { $_.manufacturer -ceq $selector.manufacturer -and $_.value -ceq $selector.value })
    if (!$literal.Count) { $selector }
})
[ordered]@{
    schemaVersion = 1; scope = 'Exact direct factory branches and current catalog identity rows only; no fallback, provider, firmware execution or hardware.'
    inputs = $inputs; powershell = $PSVersionTable.PSVersion.ToString(); ruleCount = $routes.Count
    upstreamCommit = 'f2e58468d9917189d4be7ccfc242385ce2c3035b'; upstreamExactSelectors = $upstream.Count; lockedExactSelectors = $locked.Count
    comparisonCount = $comparisons.Count; counts = $counts; comparisons = $comparisons
    upstreamUnlistedAliasCount = $upstreamUnlisted.Count; upstreamUnlistedAliasesForRegisteredClasses = $upstreamUnlisted
} | ConvertTo-Json -Depth 12
