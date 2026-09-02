<#
.SYNOPSIS
  Import a STEP file into SOLIDWORKS and write it straight back out, for comparison.

.DESCRIPTION
  doc/step-export-status.md says of the fillet coupon that its analytic output is
  "entity for entity what SOLIDWORKS writes for the same part". That claim has
  never been checked against SOLIDWORKS - it was reasoned from what the entities
  ought to be. This checks it.

  It also answers the question a downstream user actually cares about, which the
  import test cannot: having read our CYLINDRICAL_SURFACE and SPHERICAL_SURFACE,
  does SOLIDWORKS still believe in them, or has it quietly turned them into
  splines? A file that imports as a solid but arrives as a bag of splines has
  lost the whole point of the analytic path.

  Pass the same requirement as the driver: SOLIDWORKS must already be running,
  started by hand, and every call is made from compiled C#. See
  scripts/step-interop-solidworks.ps1 for why.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\step-interop-roundtrip.ps1
#>
[CmdletBinding()]
param(
    [string]$KitDir = "build\interop-kit",
    [string]$OutDir = "build\interop-roundtrip",
    [string[]]$Coupons = @("c01-cylinder", "c03-cone", "c04-sphere", "c05-torus",
                           "c07-fillet-quadrics", "c09-rational-bspline"),
    [string]$Redist = "C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist"
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($KitDir)) { $KitDir = Join-Path $repo $KitDir }
if (-not [System.IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $repo $OutDir }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$sldworks = Join-Path $Redist "SolidWorks.Interop.sldworks.dll"
$swconst  = Join-Path $Redist "SolidWorks.Interop.swconst.dll"
$onResolve = [System.ResolveEventHandler] {
    param($s, $e)
    $short = $e.Name.Split(',')[0]
    foreach ($a in [AppDomain]::CurrentDomain.GetAssemblies()) { if ($a.GetName().Name -eq $short) { return $a } }
    return $null
}
[AppDomain]::CurrentDomain.add_AssemblyResolve($onResolve)
[Reflection.Assembly]::LoadFrom($sldworks) | Out-Null
[Reflection.Assembly]::LoadFrom($swconst)  | Out-Null

Add-Type -ReferencedAssemblies $sldworks, $swconst -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

public class SwRoundTrip
{
    static ISldWorks _sw;
    public static string Attach()
    {
        _sw = (ISldWorks)Marshal.GetActiveObject("SldWorks.Application");
        return _sw.RevisionNumber();
    }

    public static string Go(string inPath, string outPath)
    {
        int errors = 0, warnings = 0;
        object importData = null;
        try { importData = _sw.GetImportFileData(inPath); } catch { }
        IModelDoc2 doc = (IModelDoc2)_sw.LoadFile4(inPath, "r", importData, ref errors);
        if (doc == null) return "load failed (" + errors + ")";
        try
        {
            bool ok = doc.Extension.SaveAs(outPath, (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
                                           (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
                                           null, ref errors, ref warnings);
            return ok ? "written" : ("SaveAs failed, errors=" + errors + " warnings=" + warnings);
        }
        finally { try { _sw.CloseDoc(doc.GetTitle()); } catch { } }
    }
}
"@ -ErrorAction Stop

Write-Host ("attached to SOLIDWORKS revision " + [SwRoundTrip]::Attach())
foreach ($c in $Coupons) {
    $in = Join-Path $KitDir "$c-analytic.stp"
    if (-not (Test-Path $in)) { Write-Host ("  {0,-24} missing" -f $c); continue }
    $out = Join-Path $OutDir "$c-solidworks.step"
    Write-Host ("  {0,-24} " -f $c) -NoNewline
    Write-Host ([SwRoundTrip]::Go($in, $out))
}
Write-Host ""
Write-Host "written to $OutDir"
