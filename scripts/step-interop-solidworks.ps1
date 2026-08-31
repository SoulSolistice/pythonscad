<#
.SYNOPSIS
  Import every STEP file of an interop kit into SOLIDWORKS and record what it made of them.

.DESCRIPTION
  doc/step-interop-validation.md explains why this exists: OpenCASCADE is the only
  kernel that has ever read this exporter's output, and the failure that started
  the work was seen in SOLIDWORKS. scripts/step-interop-kit.py writes the coupons,
  each one twice - analytic, and faceted as a control.

  The control is the method. A coupon that fails to import proves nothing alone;
  a coupon whose analytic export fails while its faceted control imports cleanly
  is the finding.

  Recorded per file:

    body type    solid or surface. The most decision-relevant number there is: a
                 surface body means SOLIDWORKS read the faces but could not sew
                 them into a solid, which is the interop failure a user meets.
    errors       what OpenDoc6 reports.
    faces        against the count the kit knows pythonscad wrote.
    volume/area  against the faceted control, and against the exact value where
                 the coupon has one.

  Check Entity and feature recognition need the UI and stay manual; the procedure
  is in doc/step-interop-validation.md. Feature recognition on c07-fillet-quadrics
  is the one worth doing by hand - whether SOLIDWORKS calls a cylinder a cylinder
  is the entire point of the analytic path, and no script answers it for a user.

.NOTES
  Two things about driving SOLIDWORKS from PowerShell, both learned the hard way:

  1. It must already be running, started by hand. COM-activating it from a script
     yields a process that never finishes starting: every call returns
     TYPE_E_ELEMENTNOTFOUND for as long as you wait. A licence acquired in the
     user's own interactive session is what brings the API up.

  2. The work has to happen in compiled C#, not in PowerShell. PowerShell binds
     COM late, through IDispatch, and SOLIDWORKS 2026 answers GetIDsOfNames with
     TYPE_E_ELEMENTNOTFOUND for every name - including RevisionNumber - even
     though QueryInterface for ISldWorks succeeds. Early binding against
     SolidWorks.Interop.sldworks.dll works. The AssemblyResolve handler below is
     needed because the generated assembly references the interop by name and the
     default probing path does not include SOLIDWORKS' redist folder.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\step-interop-solidworks.ps1
#>
[CmdletBinding()]
param(
    [string]$KitDir = "build\interop-kit",
    [string]$OutCsv,
    [string]$Redist = "C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist"
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
if (-not [System.IO.Path]::IsPathRooted($KitDir)) { $KitDir = Join-Path $repo $KitDir }
if (-not (Test-Path $KitDir)) { throw "kit directory not found: $KitDir. Run scripts/step-interop-kit.py first." }
if (-not $OutCsv) { $OutCsv = Join-Path $KitDir "solidworks-results.csv" }

$sldworks = Join-Path $Redist "SolidWorks.Interop.sldworks.dll"
$swconst  = Join-Path $Redist "SolidWorks.Interop.swconst.dll"
foreach ($dll in @($sldworks, $swconst)) {
    if (-not (Test-Path $dll)) { throw "SOLIDWORKS interop assembly not found: $dll" }
}

# Return the already-loaded assembly; calling LoadFrom in here re-enters the
# resolver for the same name and overflows the stack.
$onResolve = [System.ResolveEventHandler] {
    param($s, $e)
    $short = $e.Name.Split(',')[0]
    foreach ($a in [AppDomain]::CurrentDomain.GetAssemblies()) {
        if ($a.GetName().Name -eq $short) { return $a }
    }
    return $null
}
[AppDomain]::CurrentDomain.add_AssemblyResolve($onResolve)
[Reflection.Assembly]::LoadFrom($sldworks) | Out-Null
[Reflection.Assembly]::LoadFrom($swconst)  | Out-Null

Add-Type -ReferencedAssemblies $sldworks, $swconst -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.InteropServices;
using SolidWorks.Interop.sldworks;
using SolidWorks.Interop.swconst;

public class SwKitRunner
{
    // The interface never leaves this class. Handing it back to PowerShell turns
    // it into a System.__ComObject which will not convert to ISldWorks again, and
    // any call PowerShell makes on it binds late - which is the thing SOLIDWORKS
    // 2026 refuses. So keep it here and expose parameterless statics.
    static ISldWorks _sw;

    public static string Attach()
    {
        object o = Marshal.GetActiveObject("SldWorks.Application");
        _sw = (ISldWorks)o;
        return _sw.RevisionNumber();
    }

    // One CSV-ish record per file: file,opened,errors,warnings,body_type,
    // solids,surfaces,faces,volume_mm3,area_mm2,note
    public static string Run(string path)
    {
        int errors = 0, warnings = 0;
        string note = "";
        IModelDoc2 doc = null;
        try
        {
            // A .stp is not a native SOLIDWORKS document, so OpenDoc6 will not
            // touch it - it returns null with no error at all. Neutral formats
            // come in through LoadFile4, with the import options the file type
            // provides.
            object importData = null;
            try { importData = _sw.GetImportFileData(path); } catch { }
            doc = (IModelDoc2)_sw.LoadFile4(path, "r", importData, ref errors);
            if (doc == null)
                return Join(path, "NO", errors, warnings, "", 0, 0, 0, 0, 0,
                            "LoadFile4 returned null (swFileLoadError_e " + errors + ")");

            PartDoc part = doc as PartDoc;
            int nSolid = 0, nSheet = 0, faces = 0;
            if (part != null)
            {
                object[] solids = part.GetBodies2((int)swBodyType_e.swSolidBody, true) as object[];
                object[] sheets = part.GetBodies2((int)swBodyType_e.swSheetBody, true) as object[];
                if (solids != null) nSolid = solids.Length;
                if (sheets != null) nSheet = sheets.Length;
                foreach (object[] set in new object[][] { solids, sheets })
                {
                    if (set == null) continue;
                    foreach (object b in set)
                    {
                        Body2 body = b as Body2;
                        if (body != null) faces += body.GetFaceCount();
                    }
                }
            }
            else note = "not a PartDoc";

            string bodyType = (nSolid > 0 && nSheet == 0) ? "solid"
                            : (nSolid == 0 && nSheet > 0) ? "SURFACE"
                            : (nSolid > 0) ? "mixed" : "none";

            double volume = 0, area = 0;
            try
            {
                IMassProperty mp = doc.Extension.CreateMassProperty();
                if (mp != null)
                {
                    // SOLIDWORKS works in metres; the kit is written in millimetres.
                    volume = mp.Volume * 1e9;
                    area = mp.SurfaceArea * 1e6;
                }
            }
            catch (Exception ex) { note = (note + " massprop: " + ex.Message).Trim(); }

            return Join(path, "yes", errors, warnings, bodyType, nSolid, nSheet, faces, volume, area, note);
        }
        catch (Exception ex)
        {
            return Join(path, "ERROR", errors, warnings, "", 0, 0, 0, 0, 0, ex.Message);
        }
        finally
        {
            if (doc != null) { try { _sw.CloseDoc(doc.GetTitle()); } catch { } }
        }
    }

    static string Join(string path, string opened, int errors, int warnings, string bodyType,
                       int nSolid, int nSheet, int faces, double volume, double area, string note)
    {
        var c = CultureInfo.InvariantCulture;
        return string.Join("\t", new string[] {
            System.IO.Path.GetFileName(path), opened, errors.ToString(c), warnings.ToString(c),
            bodyType, nSolid.ToString(c), nSheet.ToString(c), faces.ToString(c),
            volume.ToString("F4", c), area.ToString("F4", c), (note ?? "").Replace("\t", " ")
        });
    }
}
"@ -ErrorAction Stop

$files = Get-ChildItem -Path $KitDir -Filter *.stp | Sort-Object Name
if ($files.Count -eq 0) { throw "no .stp files in $KitDir" }
Write-Host "kit: $($files.Count) files in $KitDir"

try { $revision = [SwKitRunner]::Attach() }
catch {
    throw ("Could not attach to a running SOLIDWORKS. Start it by hand, wait for the " +
           "window, then run this again. (" + $_.Exception.GetBaseException().Message + ")")
}
Write-Host ("attached to SOLIDWORKS revision " + $revision)
Write-Host ""

$rows = @()
foreach ($f in $files) {
    Write-Host ("  {0,-34} " -f $f.Name) -NoNewline
    $line = [SwKitRunner]::Run($f.FullName)
    $p = $line -split "`t"
    $row = [pscustomobject][ordered]@{
        file = $p[0]; opened = $p[1]; open_errors = $p[2]; open_warnings = $p[3]
        body_type = $p[4]; solid_bodies = $p[5]; surface_bodies = $p[6]
        faces = $p[7]; volume_mm3 = $p[8]; area_mm2 = $p[9]; note = $p[10]
    }
    $rows += $row
    if ($row.opened -eq 'yes') {
        Write-Host ("{0,-8} {1,5} faces  vol {2}  err {3}" -f $row.body_type, $row.faces, $row.volume_mm3, $row.open_errors)
    } else {
        Write-Host ("{0} - {1}" -f $row.opened, $row.note)
    }
}

$rows | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding utf8
Write-Host ""
Write-Host "results: $OutCsv"

Write-Host ""
Write-Host "=== analytic against its own faceted control ==="
$byCoupon = @{}
foreach ($r in $rows) {
    if ($r.file -match '^(.*)-(analytic|faceted)\.stp$') {
        $name = $Matches[1]; $mode = $Matches[2]
        if (-not $byCoupon.ContainsKey($name)) { $byCoupon[$name] = @{} }
        $byCoupon[$name][$mode] = $r
    }
}
$findings = 0
foreach ($name in ($byCoupon.Keys | Sort-Object)) {
    $a = $byCoupon[$name]['analytic']; $c = $byCoupon[$name]['faceted']
    if (-not $a -or -not $c) { continue }
    if ($a.body_type -eq 'solid' -and [int]$a.open_errors -eq 0) { $verdict = 'pass' }
    elseif ($c.body_type -ne 'solid') { $verdict = 'INCONCLUSIVE - the control failed too' }
    else { $verdict = 'FINDING - analytic failed where the control imported'; $findings++ }
    "{0,-24} analytic={1,-8} control={2,-8} {3}" -f $name, $a.body_type, $c.body_type, $verdict
}
Write-Host ""
Write-Host "findings: $findings"
