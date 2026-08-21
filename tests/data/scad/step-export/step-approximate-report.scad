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
// where the boolean took it.
//
// Nothing here is fitted, and that is the point of keeping this fixture once
// fitting exists. The approximation pass fits cylinders, and a helicoid is not
// one at any tolerance, so all four regions are tried and all four refused. The
// pass says both numbers out loud - how many it took and how many it left -
// because a pass which quietly wrote nothing would look exactly like one which
// found nothing to write.
// APPROX: approximation fitted 0 of 4 uncovered regions as cylinders
// APPROX: 4 smooth regions left faceted, 2048 facets in all
// APPROX: the tessellation leaves at most 0.0055 to fit inside
// APPROX: band 0.0053 (typical 0.0051)
// APPROX: grid 100% regular over 330 interior vertices at valence 6
// APPROX: the generator's ordering survives, a fit could be made
// APPROX: 4 regions stay faceted, no fit having been found for them
// APPROX-NOT: approximation found nothing left to fit
linear_extrude(height = 20, twist = 90, $fn = 64) square([10, 4], center = true);
