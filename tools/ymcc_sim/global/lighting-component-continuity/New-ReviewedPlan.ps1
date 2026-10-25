function New-YmccLightingReviewedPlan {
 [CmdletBinding()]
 param([Parameter(Mandatory=$true)][string]$ReviewedContractPath,[Parameter(Mandatory=$true)][string]$OutputPlanPath,[Parameter(Mandatory=$true)][string]$EvidenceRoot,[string]$SourceBundleRoot,[string]$RepoRoot,[string]$InstallRoot)
 $ErrorActionPreference='Stop'
 $pin='92f820533b2744fe695029b301111bdb528c9d5449af6ffdeb164496a1829d93'
 $contractFile=[IO.Path]::GetFullPath($ReviewedContractPath)
 if(-not (Test-Path -LiteralPath $contractFile -PathType Leaf)){throw 'LIGHTING_REVIEWED_CONTRACT_MISSING'}
 if((Get-FileHash -LiteralPath $contractFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne $pin){throw 'LIGHTING_REVIEWED_CONTRACT'}
 $review=Get-Content -LiteralPath $contractFile -Raw -Encoding UTF8|ConvertFrom-Json
 $admissionRoot=Split-Path -Parent $contractFile
 if(-not $SourceBundleRoot){$SourceBundleRoot=[IO.Path]::GetFullPath((Join-Path $admissionRoot $review.sourceBundleRelativePath))}
 $SourceBundleRoot=[IO.Path]::GetFullPath($SourceBundleRoot).TrimEnd('\')
 if(-not $RepoRoot){$RepoRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))}
 $planFile=[IO.Path]::GetFullPath($OutputPlanPath);$output=[IO.Path]::GetFullPath($EvidenceRoot)
 foreach($immutable in @($admissionRoot,$SourceBundleRoot)){if($planFile.StartsWith($immutable+'\',[StringComparison]::OrdinalIgnoreCase) -or $output.StartsWith($immutable+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'LIGHTING_OUTPUT_INSIDE_APPROVED_BUNDLE'}}
 if(Test-Path -LiteralPath $planFile){throw 'Fresh lighting plan required'}
 $template=Join-Path $admissionRoot $review.templatePath
 if((Get-FileHash -LiteralPath $template -Algorithm SHA256).Hash.ToLowerInvariant() -cne $review.templateSha256){throw 'LIGHTING_PLAN_TEMPLATE'}
 $replacements=@{'@source'=$SourceBundleRoot;'@evidence'=$admissionRoot;'@output'=$output;'@repo'=[IO.Path]::GetFullPath($RepoRoot);'@contract'=$contractFile;'@contract-sha'=$pin;'@admission-sha'=$review.admissionManifestSha256}
 function Resolve($Value){
  if($Value -is [string]){foreach($key in $replacements.Keys){if($Value -ceq $key){return $replacements[$key]};if($Value.StartsWith($key+'/',[StringComparison]::Ordinal)){return Join-Path $replacements[$key] $Value.Substring($key.Length+1)}};return $Value}
  if($Value -is [array]){for($n=0;$n -lt $Value.Count;$n++){$Value[$n]=Resolve $Value[$n]};return ,$Value}
  if($Value -is [pscustomobject]){foreach($property in $Value.PSObject.Properties){$property.Value=Resolve $property.Value};return $Value}
  return $Value
 }
 $plan=Resolve (Get-Content -LiteralPath $template -Raw -Encoding UTF8|ConvertFrom-Json)
 if($InstallRoot){$plan.installRoot=[IO.Path]::GetFullPath($InstallRoot)}
 New-Item -ItemType Directory -Path (Split-Path -Parent $planFile) -Force|Out-Null
 [IO.File]::WriteAllText($planFile,($plan|ConvertTo-Json -Depth 60),[Text.UTF8Encoding]::new($false))
 return $plan
}