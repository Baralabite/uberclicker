// UberClicker M5 dock - MIT. Millimetres. Unmeasured visual/fit prototype.
// X = device width, Y = depth (+ rear), Z = up. Device bottom rests on floor_z.

/* [View] */
part = "dock"; // [assembly,dock,coupon]
bays = 2; // [1:2]
show_device = false;

/* [M5 base - measure after reviewing the shape] */
device_w = 51.6;
device_d = 26.3;
device_h = 82;
corner_r = 4; // Assumed plan-view radius of the bottom of the case.
clearance = 0.3; // Per side; intended snug sliding fit, not yet tested.
pocket_depth = 14; // Check this stays below the lowest side button.

/* [Rounded block and stable base] */
wall = 6;
floor_z = 34; // Space beneath the pocket for the plug and cable bend.
outer_r = 4; // Plan-view corner radius; bottom stays flat on the print bed.
base_scale = 1.25; // Bottom width AND depth relative to the unchanged top.

/* [USB cable - assumed dimensions, not a printed connector] */
port_x = 0;
port_y = 0;
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
$fn = 48;
eps = 0.02;
pocket_w = device_w + 2*clearance;
pocket_d = device_d + 2*clearance;
pitch = pocket_w + wall;
block_w = bays*pocket_w + (bays+1)*wall;
block_d = pocket_d + 2*wall;
block_h = floor_z + pocket_depth;
base_w = block_w*base_scale;
base_d = block_d*base_scale;
shoulder_z = floor_z + plug_protrusion - metal_h;
socket_half_d = socket_d/2 + plug_clearance;
clamp_z = shoulder_z - plug_body_h/2;
insert_entry = socket_half_d + insert_web + insert_l;
cable_z = tab_t + tab_clearance + cable_d/2;
tab_y1 = socket_half_d + tab_w/2 + 0.5;
tab_y2 = base_d/2 - port_y - tab_w/2 - 1;

assert(part == "assembly" || part == "dock" || part == "coupon" || part == "cable-clearance", "Unknown part");
assert(bays == 1 || bays == 2, "Choose one or two bays");
assert(min(device_w,device_d,device_h,plug_w,plug_d,plug_body_h) > 0);
assert(corner_r > 0 && corner_r < min(device_w,device_d)/2);
assert(clearance >= 0 && wall >= 2 && pocket_depth > 0 && pocket_depth < device_h);
assert(base_scale >= 1 && outer_r > 0 && outer_r <= wall);
assert(plug_protrusion > 0 && plug_protrusion < min(metal_h,pocket_depth));
assert(shoulder_z > plug_body_h && cable_slot_h < shoulder_z);
assert(socket_w >= plug_w && socket_d >= plug_d, "Preview cable exceeds socket envelope");
assert(abs(port_x)+(socket_w+2*plug_clearance)/2 < device_w/2);
assert(abs(port_y)+(socket_d+2*plug_clearance)/2 < device_d/2);
assert(metal_w+2*plug_clearance < plug_w && metal_d+2*plug_clearance < plug_d);
assert(cable_slot_w > 0 && cable_slot_w < socket_w);
assert(cable_roof_r > 0 && cable_roof_r < cable_slot_w/4);
assert(cable_slot_h > cable_slot_w/2+cable_roof_r);
assert(cable_d > 0 && cable_bend_r >= cable_d && tab_t >= 1.5 && tab_w >= 2);
assert(tab_root_flare >= 0);
assert(tab_overlap > 0 && tab_clearance >= 0.2);
assert(cable_slot_w/2-tab_overlap >= cable_d+tab_clearance,
       "Widen the channel for sideways cable insertion beside the tabs");
assert(cable_z+cable_d/2+tab_clearance < cable_slot_h);
assert(cable_slot_h >= cable_z+(cable_slot_w/2+tab_overlap)/2+cable_d/sqrt(2)+tab_clearance,
       "Raise the arch to leave room for sideways cable loading");
assert(shoulder_z-plug_body_h >= cable_z+cable_bend_r,
       "Raise floor_z to accommodate this cable bend radius");
assert(tab_y2-tab_y1 >= tab_w+2*tab_root_flare+1,
       "Not enough channel between the reinforced tab roots");
assert(insert_l == 6 || insert_l == 8, "Choose a 6 or 8mm insert");
assert(screw_l > insert_l && screw_l <= 10, "Screw must engage the insert and be at most 10mm");
assert(screw_l+eps >= insert_l+insert_web+socket_half_d-plug_d/2,
       "Screw too short to reach centered plug; use a 6mm insert or thicker plug");
assert(insert_web >= 0.8 && insert_hole_d > screw_clear && access_d > insert_hole_d+1);
assert(insert_entry+2 < block_d/2-abs(port_y), "Insert needs more depth around the cavity");
assert(clamp_z-access_d/2 > cable_slot_h+2 && clamp_z+access_d/2 < shoulder_z-1,
       "Clamp bore needs solid plastic above and below");

function bay_x(i) = (i-(bays-1)/2)*pitch;

module base_hull(w,d,r,h) {
    // Convex hull of four vertical cylinders approximates the M5's base outline.
    hull() for(x=[-w/2+r,w/2-r],y=[-d/2+r,d/2-r])
        translate([x,y,0]) cylinder(r=r,h=h);
}

module dock_body() {
    difference() {
        // Each printed layer shrinks inward; no rounded underside or external overhang.
        hull() {
            base_hull(base_w,base_d,outer_r*base_scale,eps);
            translate([0,0,block_h-eps]) base_hull(block_w,block_d,outer_r,eps);
        }
        for(i=[0:bays-1]) translate([bay_x(i),0,0]) {
            translate([0,0,floor_z])
                base_hull(pocket_w,pocket_d,corner_r+clearance,pocket_depth+eps);
            // Load a real USB cable from below; its shoulder meets this ledge.
            translate([port_x-socket_w/2-plug_clearance,port_y-socket_d/2-plug_clearance,-eps])
                cube([socket_w+2*plug_clearance,socket_d+2*plug_clearance,shoulder_z+eps]);
            translate([port_x,port_y,shoulder_z-eps])
                base_hull(metal_w+2*plug_clearance,metal_d+2*plug_clearance,
                          (metal_d+2*plug_clearance)/2-eps,metal_h+2*eps);
            cable_channel_cut();
            clamp_cuts();
        }
    }
}

module cable_channel_cut() {
    // 45-degree shoulders meet a small rounded crown, instead of a broad flat bridge.
    translate([port_x,base_d/2+eps,0]) rotate([90,0,0])
        linear_extrude(base_d/2-port_y+eps) hull() {
            translate([-cable_slot_w/2,-eps])
                square([cable_slot_w,cable_slot_h-cable_slot_w/2+eps]);
            translate([0,cable_slot_h-cable_roof_r*sqrt(2)]) circle(r=cable_roof_r);
        }
}

module cable_tabs() {
    // Opposite-side ledges: one near the bend, the other near the cable exit.
    for(i=[0:1]) translate([0,i == 0 ? tab_y1 : tab_y2,0])
        scale([i == 0 ? 1 : -1,i == 0 ? 1 : -1,1])
            linear_extrude(tab_t) polygon([
                [-cable_slot_w/2-1,-tab_w/2],
                [tab_overlap,-tab_w/2],
                [tab_overlap,tab_w/2],
                [-cable_slot_w/2+1,tab_w/2+tab_root_flare],
                [-cable_slot_w/2-1,tab_w/2+tab_root_flare]
            ]);
}

module dock() {
    union() {
        dock_body();
        for(i=[0:bays-1]) translate([bay_x(i)+port_x,port_y,0]) cable_tabs();
    }
}

module cable_reference() {
    exit_z = shoulder_z-plug_body_h;
    translate([0,0,cable_z+cable_bend_r])
        cylinder(d=cable_d,h=exit_z-cable_z-cable_bend_r+eps);
    for(angle=[0:10:80]) hull() for(a=[angle,angle+10])
        translate([0,cable_bend_r*(1-cos(a)),cable_z+cable_bend_r*(1-sin(a))])
            sphere(d=cable_d);
    translate([0,cable_bend_r,cable_z]) rotate([-90,0,0])
        cylinder(d=cable_d,h=base_d/2-port_y-cable_bend_r+8);
}

module clamp_cuts() {
    // Opposing front/rear screws keep different plug bodies centered.
    for(side=[-1,1]) translate([port_x,port_y,clamp_z])
        rotate([side == 1 ? -90 : 90,0,0]) {
            cylinder(d=screw_clear,h=block_d);
            translate([0,0,socket_half_d+insert_web])
                cylinder(d=insert_hole_d,h=insert_l+eps);
            translate([0,0,insert_entry]) cylinder(d=access_d,h=block_d);
        }
}

module clamp_reference() {
    for(side=[-1,1]) translate([port_x,port_y,clamp_z])
        rotate([side == 1 ? -90 : 90,0,0]) {
            color([0.73,0.52,0.18]) translate([0,0,socket_half_d+insert_web])
                difference() {
                    cylinder(d=insert_hole_d+0.2,h=insert_l);
                    translate([0,0,-eps]) cylinder(d=3.1,h=insert_l+2*eps);
                }
            color("silver") {
                translate([0,0,plug_d/2]) cylinder(d=3,h=screw_l);
                translate([0,0,plug_d/2+screw_l]) difference() {
                    cylinder(d=5.5,h=3);
                    translate([0,0,1]) cylinder(d=2.8,h=2+eps,$fn=6);
                }
            }
        }
}

module usb_reference() {
    color([0.12,0.13,0.14])
        translate([-plug_w/2,-plug_d/2,shoulder_z-plug_body_h]) cube([plug_w,plug_d,plug_body_h]);
    color("silver") translate([0,0,shoulder_z]) difference() {
        base_hull(metal_w,metal_d,metal_d/2-eps,metal_h);
        translate([0,0,0.5]) base_hull(metal_w-0.8,metal_d-0.8,(metal_d-0.8)/2-eps,metal_h);
    }
    color([0.12,0.13,0.14]) translate([-3,-0.3,shoulder_z+1]) cube([6,0.6,metal_h-1.5]);
}

module coupon() {
    difference() {
        translate([-pocket_w/2-wall,-pocket_d/2-wall,0]) cube([pocket_w+2*wall,pocket_d+2*wall,5]);
        translate([0,0,-eps]) base_hull(pocket_w,pocket_d,corner_r+clearance,5+2*eps);
    }
}

if(part == "dock") dock();
if(part == "coupon") coupon();
if(part == "cable-clearance") intersection() {
    dock();
    union() for(i=[0:bays-1])
        translate([bay_x(i)+port_x,port_y,0]) cable_reference();
}
if(part == "assembly") {
    color([0.30,0.34,0.38]) dock();
    for(i=[0:bays-1]) {
        translate([bay_x(i)+port_x,port_y,0]) usb_reference();
        color([0.9,0.42,0.12]) translate([bay_x(i)+port_x,port_y,0]) cable_reference();
        translate([bay_x(i),0,0]) clamp_reference();
        if(show_device) %translate([bay_x(i),0,floor_z])
            base_hull(device_w,device_d,corner_r,device_h);
    }
}
