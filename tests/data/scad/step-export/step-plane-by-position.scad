// A face on a declared plane by position alone.
//
// Two cubes of side 10, twenty apart along x, declare eight planes between them:
// their tops, bottoms, fronts and backs coincide - z = 0 and 10, y = 0 and 10,
// one record each - and their ends are x = 0, 10, 20 and 30. A polyhedron()
// block of the same size at x = 40 declares nothing. Four of its faces lie on
// the cubes' planes anyway, z = 0 and 10 and y = 0 and 10; its ends, x = 40 and
// 50, lie on none.
//
// Position derives: such a face is written on the declared plane - the exact
// coefficients of the plane it lies on - at D1, and it is not the cube's face
// (checklist E8: nothing unnamed stands in for D0). Until 2026-09-30 the census
// counted it on a declared plane with the cubes' own faces, and it would have
// marked a declaration written for it alone (open item 39, R1). The cubes'
// twelve faces are on their planes by name; the block's four by position; its
// two ends on the planes of their own corners, which the census counts with
// the tessellation.
//
// EXPECT: 8 declared planes: 8 written as a face
// EXPECT: 12 on a declared plane, 4 on one by position alone, 0 on a surface the
// EXPECT: approximation fitted, 2 left to the tessellation
// EXPECT: 16 planar faces written on a plane the model declared
// EXPECT: 12 named for it, 4 on it by position alone
//
// Three cubes of side 10.
// VOLUME: 3000
// VOLUME-APPROX: 3000

cube(10);
translate([20, 0, 0]) cube(10);
translate([40, 0, 0])
  polyhedron(points = [[0, 0, 0], [10, 0, 0], [10, 10, 0], [0, 10, 0],
                       [0, 0, 10], [10, 0, 10], [10, 10, 10], [0, 10, 10]],
             faces = [[0, 1, 2, 3], [4, 5, 1, 0], [7, 6, 5, 4], [5, 6, 2, 1], [6, 7, 3, 2],
                      [7, 4, 0, 3]]);
