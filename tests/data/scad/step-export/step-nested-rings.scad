// Two concentric tubes on a common base. Their top faces are two annuli in the
// same plane, and the hole of the inner one (r=27) lies inside the outer bound
// of both of them.
//
// mergeTriangles() keeps the last enclosing loop it finds rather than the
// innermost, so a hole can be recorded against the face further out. The face
// it really belongs to is then written without its hole and seals the opening,
// which the CAD system shows as a membrane spanning the bore.
$fn = 32;
union() {
  cylinder(h = 2, r = 40);
  difference() { cylinder(h = 20, r = 30); cylinder(h = 20, r = 27); }
  difference() { cylinder(h = 20, r = 38); cylinder(h = 20, r = 35); }
}
