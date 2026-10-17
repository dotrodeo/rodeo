// Rodeo top rail: takes the back panels' top screws. It sits on the rear rail's
// two end posts and screws down into inserts in their tops. See README.md.
//
// Same coordinates as rodeo_rear_rail.scad: x along the back panel, y into the
// case, z up from the floor, panels' inner face at y = 0. Horizontal inserts at
// x = 10, 20, ... take the panel screws. Prints lying on its back (the y = 0
// face down). Longer than the bed, it prints in segments split at the same
// joints as the rear rail; the panels, screwed across each joint, hold them in line.
//
//   openscad -D cells=24 -D segment=0 -o top_rail_0.stl rodeo_top_rail.scad

include <rodeo_mount_common.scad>

/* [Size] */
// Length along the back panel, in 10 mm steps: the same as the rear rail
cells = 30;

/* [Printing] */
bed = 220;
// Same as the rear rail's joints, mm from the left end; empty = split evenly
joints = [];
part = "segment"; // [segment, all]
segment = 0;

/* [Inserts] */
insert_d = 4.0;
insert_depth = 5.0;

/* [Hidden] */
length = cells * pitch;
nseg = len(joints) > 0 ? len(joints) + 1 : ceil(length / bed);
function seg_start(i) = len(joints) > 0 ? concat([0], joints, [length])[i]
                      : (i * floor(cells / nseg) + min(i, cells % nseg)) * pitch;
post_w = 7;                                     // the rear rail's end posts
z_bot = panel_h - panel_hole_inset - boss_d / 2;    // = the posts' top
z_top = panel_h - panel_hole_inset + boss_d / 2;
screw_d = 3.4;                                  // M3 clearance into the posts' inserts
echo(str("Rodeo top rail: ", length, " mm, z ", z_bot, " to ", z_top, " mm, ", nseg, " segment(s)"));

module top_rail(x0, x1)
    difference() {
        translate([x0, 0, z_bot]) cube([x1 - x0, rail_wall_t, z_top - z_bot]);
        // panel screw inserts, horizontal, from the face on y = 0
        for (k = [1 : cells - 1]) if (k * pitch > x0 && k * pitch < x1)
            translate([k * pitch, -0.01, panel_h - panel_hole_inset]) rotate([-90, 0, 0]) cylinder(d = insert_d, h = insert_depth + 0.01);
        // screws down into the end posts, counterbored for an M3 cap head
        for (x = [if (x0 == 0) post_w / 2, if (x1 == length) length - post_w / 2]) translate([x, rail_wall_t / 2, z_bot - 1]) {
            cylinder(d = screw_d, h = z_top - z_bot + 2);
            translate([0, 0, z_top - z_bot + 1 - 3]) cylinder(d = 6, h = 3.01);
        }
    }

if (part == "all")
    for (i = [0 : nseg - 1]) translate([i * 5, 0, 0]) top_rail(seg_start(i), seg_start(i + 1));
else
    top_rail(seg_start(min(segment, nseg - 1)), seg_start(min(segment, nseg - 1) + 1));
