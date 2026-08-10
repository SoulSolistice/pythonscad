// Baseline for the STEP exporter: a single closed body without holes.
// Guards the vertex and edge sharing - every edge of the shell has to be used
// by exactly two faces, once in each direction.
cube([10, 20, 30]);
