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
    faults       what the Import Diagnostics dialog shows, read-only, at all
                 three levels: IBody2::Check3 for the body, IFace2::Check per
                 face and IEdge::Check per edge, each returning a FaultEntity
                 with a count and an error code per fault. Check and Check2 are
                 the obsolete forms and are not used.

                 The body-level call is one COM round trip and the per-entity
                 walk is one per face and one per edge, which on this kit's
                 largest bodies is several thousand. That is not a small
                 constant: walking c12-approximated-faceted, 2050 faces, took
                 over eighteen minutes and reported nothing, because a faceted
                 control is planes and has nothing to report. So the walk is
                 gated - see -MaxWalkFaces.

                 The edge count is the one to read first. Every measurement in
                 doc/step-interop-validation.md says the defect is edges that do
                 not lie on the faces they bound - 254 of 258 on one coupon, by
                 up to 0.196 - and that was measured with OpenCASCADE. This is
                 SOLIDWORKS' own verdict on the same edges, per edge, so the two
                 can be correlated rather than merely compared.

                 Nothing here repairs: the dialog offers to heal and this never
                 accepts. IPartDoc::ImportDiagnosis is the healing one and is
                 deliberately not called, because it would measure a body other
                 than the one that was imported.
    gaps         IBody2::Diagnose, which reports the gaps in a body and leaves it
                 alone. The other half of what the dialog shows.
    errors       what OpenDoc6 reports.
    faces        against the count the kit knows pythonscad wrote.
    volume/area  against the faceted control, and against the exact value where
                 the coupon has one.

  With -RoundTrip, each file is also saved straight back out as
  <coupon>_SW.STEP, and scripts/step-interop-sw-roundtrip.py reads both halves of
  that pair with OpenCASCADE. That answers what a body type cannot: whether
  SOLIDWORKS kept the surface the coupon is about, or made a solid by discarding
  it and sewing the rest. This document has already had to record one case of
  exactly that.

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

.PARAMETER ImportSettings
  A label for the state of Tools > Options > Import when the run was made, for
  instance "do-not-knit" or "try-forming-solids". It is recorded in every row.
  Nothing checks it and nothing can; the point is that a run whose settings were
  not written down cannot be compared with another one.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\step-interop-solidworks.ps1 -ImportSettings do-not-knit
#>
[CmdletBinding()]
param(
    [string]$KitDir = "build\interop-kit",
    [string]$OutCsv,
    [string]$Redist = "C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist",
    # LoadFile4 imports with whatever is set in Tools > Options > Import, and
    # the API gives this script no way to say otherwise. Two runs of the same
    # file can therefore disagree with nothing in the CSV to show why - which is
    # what happened here between an automated run that recorded SURFACE bodies
    # and a hand import of the same file that gave solids. Label the run with
    # the settings it was made under, so a result still means something a week
    # later.
    [Parameter(Mandatory = $true)]
    [string]$ImportSettings,
    # Save each imported coupon back out as <coupon>_SW.STEP, for
    # scripts/step-interop-sw-roundtrip.py to compare against what we wrote.
    # Off by default because it writes into the kit directory and roughly
    # doubles the run; on for any run whose result will be argued about.
    [switch]$RoundTrip,
    # Restrict the run to coupons whose file name matches, the way the Python
    # kit's --only does. A full kit is 48 imports; re-asking one question of one
    # pair should not cost that.
    [string]$Only,
    # Localising a fault to its face or edge costs one COM call per entity. On a
    # body of a few hundred faces that is free; on the kit's 2000-face faceted
    # controls it is tens of minutes - c12-approximated-faceted took over
    # eighteen - to be told what the body-level check already said. So bodies
    # larger than this are walked only when IBody2::Check3 or IBody2::Diagnose
    # reports something to localise.
    #
    # The default is set above every *analytic* file in this kit and below its
    # largest faceted controls, and that is the whole reasoning: a body-level
    # count of zero does not imply the entities are clean - c06-partial-torus
    # reports zero faults while SOLIDWORKS holds a solid 14% away from the one
    # in the file - so the skip is only safe where there is independent reason
    # to expect nothing, which is what a faceted control is. A first attempt at
    # 300 skipped f05-band-fn096-analytic at 534 faces and would have skipped
    # both reference parts, which are the rows the run exists for.
    [int]$MaxWalkFaces = 1500
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
    public static bool SaveBack;
    public static int MaxWalkFaces = 300;

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
            object[] solids = null, sheets = null;
            if (part != null)
            {
                solids = part.GetBodies2((int)swBodyType_e.swSolidBody, true) as object[];
                sheets = part.GetBodies2((int)swBodyType_e.swSheetBody, true) as object[];
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

            // The two numbers the Import Diagnostics dialog shows, taken
            // read-only. IBody2::Check2 is the body's fault count, IFace2::Check
            // hands back a FaultEntity per face with its own count and error
            // codes, and IBody2::Diagnose reports gaps. None of the three
            // repairs anything; ImportDiagnosis is the one that heals and is
            // deliberately not called, because it would measure a body other
            // than the one that was imported.
            int faultyFaces = 0, faultyEdges = 0, bodyFaults = 0, gaps = 0;
            bool skippedWalk = false;
            var codes = new SortedSet<int>();
            if (part != null)
            {
                foreach (object[] set in new object[][] { solids, sheets })
                {
                    if (set == null) continue;
                    foreach (object b in set)
                    {
                        Body2 body = b as Body2;
                        if (body == null) continue;
                        // Check3, not Check/Check2: those are the obsolete forms
                        // and return a bare count where this returns the faults.
                        try
                        {
                            FaultEntity bf = body.Check3;
                            if (bf != null)
                            {
                                bodyFaults += bf.Count;
                                for (int i = 0; i < bf.Count; i++) codes.Add(bf.ErrorCode[i]);
                            }
                        }
                        catch { }
                        try
                        {
                            DiagnoseResult dr = body.Diagnose();
                            if (dr != null) gaps += dr.GetGapsCount();
                        }
                        catch { }
                        // Cheap first: the body-level count and the gap count are
                        // one call each. Walk the entities only when there is
                        // something to localise, or when the body is small
                        // enough that confirming zero costs nothing.
                        int bodyFaces = 0;
                        try { bodyFaces = body.GetFaceCount(); } catch { }
                        bool walk = bodyFaces <= MaxWalkFaces || bodyFaults > 0 || gaps > 0;
                        if (!walk) { skippedWalk = true; continue; }

                        try
                        {
                            Face2 face = body.GetFirstFace() as Face2;
                            while (face != null)
                            {
                                FaultEntity fe = face.Check;
                                if (fe != null && fe.Count > 0)
                                {
                                    faultyFaces++;
                                    for (int i = 0; i < fe.Count; i++) codes.Add(fe.ErrorCode[i]);
                                }
                                face = face.GetNextFace() as Face2;
                            }
                        }
                        catch { }
                        // And the edges, which is where this exporter's measured
                        // defect lives. See the note at the top.
                        try
                        {
                            object[] edges = body.GetEdges() as object[];
                            if (edges != null)
                            {
                                foreach (object eo in edges)
                                {
                                    Edge edge = eo as Edge;
                                    if (edge == null) continue;
                                    FaultEntity ee = edge.Check;
                                    if (ee != null && ee.Count > 0)
                                    {
                                        faultyEdges++;
                                        for (int i = 0; i < ee.Count; i++) codes.Add(ee.ErrorCode[i]);
                                    }
                                }
                            }
                        }
                        catch { }
                    }
                }
            }
            string codeList = string.Join("/", new List<int>(codes).ConvertAll(x => x.ToString()).ToArray());

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

            if (SaveBack)
            {
                // Straight back out, with whatever is set in Tools > Options >
                // Export - the same caveat as the import options, and the same
                // reason the run has to be labelled. A save that fails is
                // recorded rather than thrown: the import result is still worth
                // having without it.
                string outPath = System.IO.Path.Combine(
                    System.IO.Path.GetDirectoryName(path),
                    System.IO.Path.GetFileNameWithoutExtension(path) + "_SW.STEP");
                int saveErr = 0, saveWarn = 0;
                try
                {
                    bool ok = doc.Extension.SaveAs(outPath,
                        (int)swSaveAsVersion_e.swSaveAsCurrentVersion,
                        (int)swSaveAsOptions_e.swSaveAsOptions_Silent,
                        null, ref saveErr, ref saveWarn);
                    if (!ok) note = (note + " saveas failed err=" + saveErr).Trim();
                }
                catch (Exception ex) { note = (note + " saveas: " + ex.Message).Trim(); }
            }

            // "not-walked" rather than a zero, because a count nobody looked
            // for is not a measurement. The body-level count beside it is.
            note = (note + " faults=" + bodyFaults + " gaps=" + gaps
                    + (skippedWalk ? " faultyfaces=not-walked faultyedges=not-walked"
                                   : " faultyfaces=" + faultyFaces + " faultyedges=" + faultyEdges)
                    + (codeList.Length > 0 ? " codes=" + codeList : "")).Trim();

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

# *.stp only, so a previous -RoundTrip run's *_SW.STEP files are not themselves
# re-imported and saved back out as <coupon>_SW_SW.STEP.
$files = Get-ChildItem -Path $KitDir -Filter *.stp | Sort-Object Name
if ($Only) { $files = $files | Where-Object { $_.Name -match $Only } }
if ($files.Count -eq 0) { throw "no .stp files in $KitDir" }
Write-Host "kit: $($files.Count) files in $KitDir"

try { $revision = [SwKitRunner]::Attach() }
catch {
    throw ("Could not attach to a running SOLIDWORKS. Start it by hand, wait for the " +
           "window, then run this again. (" + $_.Exception.GetBaseException().Message + ")")
}
Write-Host ("attached to SOLIDWORKS revision " + $revision)
[SwKitRunner]::SaveBack = [bool]$RoundTrip
[SwKitRunner]::MaxWalkFaces = $MaxWalkFaces
if ($RoundTrip) { Write-Host "round trip: each coupon will be saved back as <coupon>_SW.STEP" }
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
        import_settings = $ImportSettings
    }
    $rows += $row
    if ($row.opened -eq 'yes') {
        Write-Host ("{0,-8} {1,5} faces  vol {2}  err {3}  {4}" -f $row.body_type, $row.faces, $row.volume_mm3, $row.open_errors, $row.note)
    } else {
        Write-Host ("{0} - {1}" -f $row.opened, $row.note)
    }
}

$rows | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding utf8
Write-Host ""
Write-Host "results: $OutCsv"
if ($RoundTrip) {
    Write-Host ""
    Write-Host "now compare what came back:"
    Write-Host "  python3 scripts/step-interop-sw-roundtrip.py --kitdir $KitDir"
}

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
