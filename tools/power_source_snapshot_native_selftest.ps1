param([string]$OutputDirectory='G:\YeManCC-Work\Mainline\Build\Validation\foreground-acdc-20261008\native-selftest')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot
$main=[IO.File]::ReadAllText((Join-Path $root 'native\main.cpp'))
$match=[regex]::Match($main,'ipc_on\("power.sourceSnapshot", \[\]\(const json&\) -> json \{([\s\S]*?)\n    \}\);')
if(-not $match.Success){throw 'Production power.sourceSnapshot handler not found'}
$fixture=@'
#include <iostream>
#include <stdexcept>
#include "json.hpp"
using json=nlohmann::json;
struct SYSTEM_POWER_STATUS { unsigned char ACLineStatus=255; };
static constexpr int FALSE=0;
static int reads=0;
static bool available=true;
static unsigned char acLine=1;
static int GetSystemPowerStatus(SYSTEM_POWER_STATUS* status) { ++reads; status->ACLineStatus=acLine; return available?1:0; }
static json readSnapshot() {
//__PRODUCTION_HANDLER__
}
static void expect(bool condition,const char* label) { if(!condition)throw std::runtime_error(label);std::cout<<"PASS "<<label<<"\n"; }
int main() { try {
 auto read=[&] {const auto before=reads;auto result=readSnapshot();expect(reads==before+1,"every foreground snapshot performs exactly one system-power query");return result;};
 available=true;acLine=1;auto ac=read();expect(ac["known"]==true&&ac["acLine"]==1,"AC supply is reliable AC");
 acLine=0;auto dc=read();expect(dc["known"]==true&&dc["acLine"]==0,"sleep AC then wake DC reads actual DC supply rather than a remembered AC event");
 acLine=255;auto unknown=read();expect(unknown["known"]==false&&unknown["acLine"]==255,"Windows unknown is not silently converted to reliable AC");
 acLine=2;auto invalid=read();expect(invalid["known"]==false&&invalid["acLine"]==255,"unexpected system source stays unknown");
 available=false;acLine=0;auto failed=read();expect(failed["known"]==false&&failed["acLine"]==255,"failed system-power query is unknown, never a fabricated source");
 std::cout<<"FOREGROUND_POWER_SOURCE_NATIVE_OK (production handler, mock OS only)\n";return 0;
 }catch(const std::exception& error){std::cerr<<"FAILED "<<error.what()<<"\n";return 1;}}
'@
$fixture=$fixture.Replace('//__PRODUCTION_HANDLER__',$match.Groups[1].Value)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$source=Join-Path $OutputDirectory 'power-source-selftest.cpp'
[IO.File]::WriteAllText($source,$fixture,[Text.UTF8Encoding]::new($false))
$bat=Join-Path $OutputDirectory 'compile.bat'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /I"$root\deps\json" "$source" /Fo"$OutputDirectory\power-source-selftest.obj" /Fe"$OutputDirectory\power-source-selftest.exe"
if errorlevel 1 exit /b 1
"$OutputDirectory\power-source-selftest.exe"
"@ | Set-Content -LiteralPath $bat -Encoding ascii
& $bat
if($LASTEXITCODE -ne 0){throw "Foreground power-source selftest failed: $LASTEXITCODE"}
