# The PowerShell side of scripts/step-dev.sh.
#
# Calling MSYS2's bash from PowerShell is where sessions lose time: a bare
# `bash.exe -lc` loses System32 from PATH, forgetting MSYSTEM loses /ucrt64/bin,
# and any argument with a space - lid10's `-P "New set 1"` - is mangled crossing
# PowerShell's quoting and then bash's. This sets the environment once and hands
# the arguments over untouched, so the command a session types is just:
#
#   powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 build
#   powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 test
#   powershell -ExecutionPolicy Bypass -File scripts\step-dev.ps1 export lid10
#
# See scripts/step-dev.sh for every subcommand.
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Rest)

$env:MSYSTEM = 'UCRT64'
$env:CHERE_INVOKING = '1'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$script = (Join-Path $here 'step-dev.sh') -replace '\\', '/'
# E:/x/y -> /e/x/y, which is how MSYS2 names a Windows drive.
if ($script -match '^([A-Za-z]):/(.*)$') { $script = '/' + $Matches[1].ToLower() + '/' + $Matches[2] }

# Each argument single-quoted for bash, so nothing in it is re-split or expanded.
$quoted = @($Rest | ForEach-Object { "'" + ($_ -replace "'", "'\''") + "'" }) -join ' '
& 'C:\msys64\usr\bin\bash.exe' -lc "bash '$script' $quoted"
exit $LASTEXITCODE
