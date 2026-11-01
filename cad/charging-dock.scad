// UberClicker M5 dock - MIT. Millimetres. Partly measured fit prototype.
// X = device width, Y = depth (+ rear), Z = up. Device bottom rests on floor_z.

/* [View] */
part = "dock"; // [assembly,dock,coupon]
bays = 2; // [1:2]
show_device = false;

/* [M5 base - user measurements] */
device_w = 52.2;
device_d = 24.6; // Maximum lower-body thickness below the screen bulge.
lower_body_h = 21.3; // Height from bottom where the deeper screen section begins.
device_h = 82; // Manufacturer nominal height; not yet measured.
corner_r = 4; // Assumed plan-view radius of the bottom of the case.
clearance = 0.3; // Per side; intended snug sliding fit, not yet tested.
pocket_depth = 14; // Check this stays below the lowest side button.

/* [Rounded block and stable base] */
wall = 6;
floor_z = 34; // Space beneath the pocket for the plug and cable bend.
outer_r = 4; // Plan-view corner radius; bottom stays flat on the print bed.
base_scale = 1.25; // Bottom width AND depth relative to the unchanged top.

/* [USB port and cable - plug dimensions still assumed] */
port_x = 0; // Width-centred until measured.
port_from_back = 13.2; // User-measured from the rear body face.
plug_w = 12;
plug_d = 7;
plug_body_h = 16;
plug_clearance = 0.2;
socket_w = 16; // Maximum overmould envelope; independent of the preview cable.
socket_d = 9;
metal_w = 8.4;
metal_d = 2.6;
metal_h = 6.5;
plug_protrusion = 5; // Metal above the pocket floor.
cable_slot_w = 10;
cable_slot_h = 12; // Nominal arch apex, before rounding its crown.
cable_roof_r = 0.8;

/* [Cable hold-down tabs] */
cable_d = 4;
cable_bend_r = 10; // Assumed centerline radius of the 90-degree bend.
tab_t = 3;
tab_w = 4;
tab_root_flare = 2; // Wider attachment toward the gap between the tabs.
tab_overlap = 0.5; // Tab tip extends this far past the channel center.
tab_clearance = 0.4;

/* [Plug clamp - measure selected heat-set hardware] */
insert_l = 6; // [6,8]
insert_hole_d = 4.4; // Assumed pilot diameter; use insert supplier's recommendation.
screw_l = 10; // Under-head length; never more than 10mm.
screw_clear = 3.4;
insert_web = 0.8; // Plastic between insert end and cable cavity.
access_d = 7; // Recess admits the screw head and heat-setting tool.

/* [Hidden] */
include <lib/dock-geometry.scad>
