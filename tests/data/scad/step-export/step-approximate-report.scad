// What the exact passes leave behind, and how much room there is to fit it.
//
// A twisted extrusion sweeps a helicoid. It is exactly describable and it is
// not a quadric, so the exact pass refuses it - correctly, and that refusal has
// its own fixture in step-extrude-refusals. This one is about the question that
// comes after: given that four walls are going out as 2050 facets, what would
// it take to write them as surfaces instead, and how wrong could that be?
//
// The answer is the band. The mesh does not say where the true surface is; it
// says the surface passes through these vertices and cannot stray further from
// the facets than their own sagitta. For two facets meeting at a dihedral theta
// across a chord c, that is (c/2)*tan(theta/4). A fitted surface inside the band
// asserts nothing the mesh does not already allow; one outside it is inventing
// geometry.
//
// This model is the clean case, and it is here to be compared against a dirty
// one. Every wall is a uniformly tessellated sweep, so the widest band over any
// edge and the band over the typical edge are the same to within 4% - there are
// no bad edges. A boolean-trimmed body is not like that: the bayonet's hose
// thread measures a band of 0.78 against a typical 0.075, a ten-fold tail from
// the facets where the sweep was cut against the socket wall. Same measure, and
// it tells the two apart, which is the whole reason for reporting both.
//
// The band says how *accurate* a fit could be. Whether a fit is available at all
// is a separate question, and it is the one that decides this feature's fate.
// Fitting needs the facets' ordering - which follows which along the sweep - and
// nothing can recover that from an unordered set. A mesh straight from a
// generator still has it: a swept quad grid split into triangles the same way
// everywhere gives every interior vertex a valence of exactly 6, and this model
// measures 100% of 330. A mesh that has been through a boolean does not. The
// bayonet's thread, trimmed against its socket wall, measures 36%.
//
// So the two models answer the two halves differently, and together they say
// where each route applies: fit where the generator's grid survives, declare
// where the boolean took it. This one is the first half, and the numbers above
// are what the approximation pass now acts on rather than only reports.
//
// And now it is fitted, which is what those two measurements were for. A
// helicoid is neither a cylinder nor a surface of revolution, so both quadric
// routes refuse all four regions - the second of them because a vertex is off
// the ring its height puts it on, which is the twist saying so. The grid
// recovery then takes all four, because 100% regularity means the ordering is
// still there to recover. 2048 facets become 4 faces.
//
// The recovered grid is handed to the recogniser as a declaration, so what
// happens to it afterwards is what happens to one the model made: the same
// membership test, the same boundary walk, the same emission. A wall comes back
// as a 24x17 cubic sweep with a tessellation band of 0.0107.
//
// OpenCASCADE reads the result as one solid of one shell, volume 798.716
// against the faceted export's 800.717. Over the walls' 570 square units that
// is an average normal displacement of 0.0035, well inside the band - the
// smooth surface cutting the corners the chords left standing.
// APPROX: approximation took 4 of 4 uncovered regions - 0 as cylinders, 0 as rings of a turned surface, 4 as swept grids
// APPROX: 4 regions are not turned surfaces because a vertex is off the ring its height puts it on
// APPROX: a declared 24x17 cubic sweep claims 736 facets whole
// APPROX: 4 declared sweeps written as one face each, replacing 2048 facets
// APPROX: approximation found nothing left to fit
// APPROX-NOT: regions stay faceted
linear_extrude(height = 20, twist = 90, $fn = 64) square([10, 4], center = true);
