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
// Nothing is fitted yet. The pass measures and says so out loud, because a pass
// which silently did nothing would look exactly like one which found nothing.
// APPROX: 6 smooth regions left faceted, 2050 facets in all
// APPROX: the tessellation leaves at most 0.0055 to fit inside
// APPROX: band 0.0053 (typical 0.0051)
// APPROX: approximation is measuring only - all 6 regions stay faceted
// APPROX-NOT: approximation found nothing left to fit
linear_extrude(height = 20, twist = 90, $fn = 64) square([10, 4], center = true);
