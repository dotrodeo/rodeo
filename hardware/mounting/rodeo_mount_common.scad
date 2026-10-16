// Shared dimensions for the Rodeo mounting parts (see README.md).
// Everything sits on the enclosure's solid floor; Z = 0 is the floor.

pitch = 10;          // node spacing along the back panel
boss_d = 8;          // insert boss
z0 = 6;              // boss top: the board datum (boards sit on M3 x 8 mm standoffs above it)
rib_w = 2.4;         // open-frame ribs (3 perimeters of a 0.4 mm nozzle)
rib_h = 4;
floor_hole_d = 3.4;  // M3 screw to the floor, countersunk from the top
csk_d = 6.4;
$fn = 40;

// boss with an insert hole from the top
module boss(insert_d, insert_depth)
    difference() {
        cylinder(d = boss_d, h = z0);
        translate([0, 0, z0 - insert_depth]) cylinder(d = insert_d, h = insert_depth + 0.01);
    }

// screw lug: a boss with a countersunk through-hole for an M3 flat-head into the floor
module floor_lug()
    difference() {
        cylinder(d = boss_d + 1, h = rib_h);
        translate([0, 0, -1]) cylinder(d = floor_hole_d, h = rib_h + 2);
        translate([0, 0, rib_h - (csk_d - floor_hole_d) / 2]) cylinder(d1 = floor_hole_d, d2 = csk_d, h = (csk_d - floor_hole_d) / 2 + 0.01);
    }

// rib between two points, on the floor
module rib(p, q, h = rib_h, w = rib_w)
    hull() { translate(p) cylinder(d = w, h = h, $fn = 12); translate(q) cylinder(d = w, h = h, $fn = 12); }
