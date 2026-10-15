// Rodeo rear I/O rail: holds the back panel and every rear daughterboard
// (power, switch, jack board, MIDI). See README.md.
//
// Coordinates: x along the back panel, y into the case. The back panel's inner
// face is y = 0; the panel sits in a groove at y < 0. Insert bosses every 10 mm
// in x, in rows at fixed depths from the panel (default 15 and 35 mm).
// Daughterboards put their I/O edge at y = 0 and their M3 holes at those depths.
// A rail longer than the bed prints in segments; each is screwed to the floor at
// both ends, and the back panel in the groove keeps them in line.
//
//   openscad -D cells=30 -D segment=0 -o rail_0.stl rodeo_rear_rail.scad

include <rodeo_mount_common.scad>

/* [Size] */
// Length along the back panel, in 10 mm steps
cells = 30;
// Depths of the boss rows from the panel's inner face, mm
row_depths = [15, 35];

/* [Printing] */
// Longest part your printer takes, mm
bed = 220;
part = "segment"; // [segment, all]
// Which segment (0 = left)
segment = 0;

/* [Back panel] */
// Back panel thickness, mm
panel_t = 3.0;
// Panel clearance in the groove, mm
clear = 0.3;
// Groove walls: height and thickness, mm
wall_h = 6;
wall_w = 2.4;

/* [Inserts] */
insert_d = 4.0;
insert_depth = 5.0;

/* [Hidden] */
length = cells * pitch;
nseg = ceil(length / bed);
function seg_start(i) = (i * floor(cells / nseg) + min(i, cells % nseg)) * pitch;
depth = max(row_depths) + boss_d / 2;
echo(str("Rodeo rear rail: ", length, " mm, ", len(row_depths), " rows at ", row_depths,
         " mm, ", nseg, " segment(s)"));

module rail(x0, x1) {
    gw = panel_t + clear;
    xs = [for (k = [0 : cells - 1]) if ((k + 0.5) * pitch > x0 && (k + 0.5) * pitch < x1) (k + 0.5) * pitch];
    difference() {
        union() {
            // groove: inner wall ends on the panel's inner face (y = 0), outer wall behind the panel
            translate([x0, 0, 0]) cube([x1 - x0, wall_w, wall_h]);
            translate([x0, -gw - wall_w, 0]) cube([x1 - x0, wall_w, wall_h]);
            translate([x0, -gw - wall_w, 0]) cube([x1 - x0, gw + 2 * wall_w, 1.2]);      // groove floor
            for (y = row_depths) {
                rib([x0 + rib_w / 2, y, 0], [x1 - rib_w / 2, y, 0]);
                for (x = xs) translate([x, y, 0]) boss(insert_d, insert_depth);
            }
            // cross ribs every 20 mm, from the groove wall to the back row
            for (x = [x0 + 5 : 20 : x1 - 1]) rib([x, wall_w / 2, 0], [x, max(row_depths), 0]);
            rib([x0 + rib_w / 2, wall_w / 2, 0], [x0 + rib_w / 2, max(row_depths), 0]);
            rib([x1 - rib_w / 2, wall_w / 2, 0], [x1 - rib_w / 2, max(row_depths), 0]);
            // floor-screw lugs between the rows, near each end
            for (x = [x0 + 10, x1 - 10]) translate([x, (row_depths[0] + max(row_depths)) / 2, 0]) floor_lug();
        }
        for (x = xs, y = row_depths) translate([x, y, z0 - insert_depth]) cylinder(d = insert_d, h = insert_depth + 0.01);
        for (x = [x0 + 10, x1 - 10]) translate([x, (row_depths[0] + max(row_depths)) / 2, -1]) cylinder(d = floor_hole_d, h = 10);
    }
}

module rail_all() for (i = [0 : nseg - 1]) rail(seg_start(i), seg_start(i + 1));

if (part == "all")
    for (i = [0 : nseg - 1]) translate([i * 5, 0, 0]) rail(seg_start(i), seg_start(i + 1));
else
    rail(seg_start(min(segment, nseg - 1)), seg_start(min(segment, nseg - 1) + 1));
