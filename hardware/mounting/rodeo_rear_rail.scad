// Rodeo rear I/O rail: holds every rear daughterboard (power, switch, jack
// board, MIDI) and the bottom edge of their back panels. See README.md.
//
// Coordinates: x along the back panel, y into the case. The back panels' inner
// face is y = 0. Insert bosses every 10 mm in x (at x = 5, 15, ...), in rows at
// fixed depths from the panel (default 15 and 35 mm); daughterboards put their
// I/O edge at y = 0 and their M3 holes at those depths. A wall along y = 0 takes
// the panels' bottom screws: horizontal inserts at x = 10, 20, ... At each end a
// post carries the top rail (rodeo_top_rail.scad). A rail longer than the bed
// prints in segments; each is screwed to the floor at both ends.
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
// Where to split a rail too long for the bed, mm from the left end (multiples of
// 10). Put each joint behind the middle of a panel: a joint can't take a panel
// screw. Empty = split evenly. Use the same joints for the top rail.
joints = [];
part = "segment"; // [segment, all]
// Which segment (0 = left)
segment = 0;

/* [Panel wall] */
// Height of the wall behind the panels' bottom edge, mm
wall_h = 9;

/* [Inserts] */
insert_d = 4.0;
insert_depth = 5.0;

/* [Hidden] */
length = cells * pitch;
post_w = 7;                                       // end posts, x
post_top = panel_h - panel_hole_inset - boss_d / 2;   // the top rail sits on the posts
nseg = len(joints) > 0 ? len(joints) + 1 : ceil(length / bed);
function seg_start(i) = len(joints) > 0 ? concat([0], joints, [length])[i]
                      : (i * floor(cells / nseg) + min(i, cells % nseg)) * pitch;
depth = max(row_depths) + boss_d / 2;
echo(str("Rodeo rear rail: ", length, " mm, ", len(row_depths), " rows at ", row_depths,
         " mm, ", nseg, " segment(s)"));

module rail(x0, x1) {
    xs = [for (k = [0 : cells - 1]) if ((k + 0.5) * pitch > x0 && (k + 0.5) * pitch < x1) (k + 0.5) * pitch];
    px = [for (k = [1 : cells - 1]) if (k * pitch > x0 && k * pitch < x1) k * pitch];   // panel screws
    ends = [if (x0 == 0) 0, if (x1 == length) length - post_w];
    difference() {
        union() {
            // wall behind the panels' bottom edge, its face on y = 0
            translate([x0, 0, 0]) cube([x1 - x0, rail_wall_t, wall_h]);
            // end posts for the top rail
            for (x = ends) translate([x, 0, 0]) cube([post_w, rail_wall_t, post_top]);
            for (y = row_depths) {
                rib([x0 + rib_w / 2, y, 0], [x1 - rib_w / 2, y, 0]);
                for (x = xs) translate([x, y, 0]) boss(insert_d, insert_depth);
            }
            // cross ribs every 20 mm, from the wall to the back row
            for (x = [x0 + 5 : 20 : x1 - 1]) rib([x, rail_wall_t, 0], [x, max(row_depths), 0]);
            rib([x0 + rib_w / 2, rail_wall_t, 0], [x0 + rib_w / 2, max(row_depths), 0]);
            rib([x1 - rib_w / 2, rail_wall_t, 0], [x1 - rib_w / 2, max(row_depths), 0]);
            // floor-screw lugs between the rows, near each end
            for (x = [x0 + 10, x1 - 10]) translate([x, (row_depths[0] + max(row_depths)) / 2, 0]) floor_lug();
        }
        for (x = xs, y = row_depths) translate([x, y, z0 - insert_depth]) cylinder(d = insert_d, h = insert_depth + 0.01);
        for (x = [x0 + 10, x1 - 10]) translate([x, (row_depths[0] + max(row_depths)) / 2, -1]) cylinder(d = floor_hole_d, h = 10);
        // panel screw inserts, horizontal, from the wall's face
        for (x = px) translate([x, -0.01, panel_hole_inset]) rotate([-90, 0, 0]) cylinder(d = insert_d, h = insert_depth + 0.01);
        // top-rail screw inserts in the post tops
        for (x = ends) translate([x + post_w / 2, rail_wall_t / 2, post_top - insert_depth]) cylinder(d = insert_d, h = insert_depth + 0.01);
    }
}

module rail_all() for (i = [0 : nseg - 1]) rail(seg_start(i), seg_start(i + 1));

if (part == "all")
    for (i = [0 : nseg - 1]) translate([i * 5, 0, 0]) rail(seg_start(i), seg_start(i + 1));
else
    rail(seg_start(min(segment, nseg - 1)), seg_start(min(segment, nseg - 1) + 1));
