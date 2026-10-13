// Rodeo motherboard frame: four insert bosses on the motherboard's 82 x 104 mm
// hole pattern, joined by an open rib frame, with lugs to screw it to the floor.
// Place it anywhere; the motherboard has no rear I/O. See README.md.
//
//   openscad -o motherboard_frame.stl rodeo_motherboard_frame.scad

include <rodeo_mount_common.scad>

/* [Hole pattern] */
// Hole spacing in x and y, mm (Rev B motherboard: 82 x 104)
span = [82, 104];

/* [Inserts] */
insert_d = 4.0;
insert_depth = 5.0;

/* [Hidden] */
corners = [[0, 0], [span[0], 0], [0, span[1]], [span[0], span[1]]];
lugs = [[span[0] / 2, 0], [span[0] / 2, span[1]], [0, span[1] / 2], [span[0], span[1] / 2]];

difference() {
    union() {
        for (c = corners) translate(c) boss(insert_d, insert_depth);
        rib(corners[0], corners[1]); rib(corners[2], corners[3]);
        rib(corners[0], corners[2]); rib(corners[1], corners[3]);
        rib(corners[0], corners[3]); rib(corners[1], corners[2]);
        for (l = lugs) translate(l) floor_lug();
    }
    for (c = corners) translate([c[0], c[1], z0 - insert_depth]) cylinder(d = insert_d, h = insert_depth + 0.01);
    for (l = lugs) translate([l[0], l[1], -1]) cylinder(d = floor_hole_d, h = 10);
}
