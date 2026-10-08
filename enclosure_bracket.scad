// SITE-R 7372 - sensor mounting bracket
// OpenSCAD source for the clip that holds the DHT11 and the LDR inside the
// glass enclosure. Print at 0.2mm layer height, 4 perimeters, 20% infill.
//
// Render:  openscad -o bracket.stl enclosure_bracket.scad
// Edit:    change PLATE_W / DEPTH / slot positions, re-render.

$fn = 48;

// --- overall plate ---
PLATE_W   = 60;   // mm, width of the mounting plate
PLATE_D   = 22;   // mm, depth of the plate
PLATE_T   = 3;    // mm, plate thickness
HOLE_D    = 3.2;  // mm, screw clearance
HOLE_INSET= 5;    // mm, distance from plate edge to screw hole centre

// --- sensor slots ---
SLOT_W    = 10;   // mm, DHT11 body width
SLOT_H    = 16;   // mm, DHT11 body height
SLOT_D    = 4;    // mm, how deep the slot is cut into the plate
DHT_X     = 12;   // mm, slot centre from left edge
DHT_HOLE_D= 2.6;  // mm, mounting hole through the slot

// LDR opening: a round window so light still reaches the photoresistor
LDR_X     = 42;   // mm, window centre from left edge
LDR_D     = 8;    // mm, window diameter
LDR_HOLE_D= 3.2;

module plate() {
    difference() {
        // the plate itself
        translate([0, 0, 0])
            linear_extrude(height = PLATE_T)
                square([PLATE_W, PLATE_D], center = false);

        // two screw holes, one per end
        for (x = [HOLE_INSET, PLATE_W - HOLE_INSET])
            translate([x, PLATE_D / 2, -1])
                cylinder(h = PLATE_T + 2, d = HOLE_D);

        // DHT11 slot (cut from the top edge downwards)
        translate([DHT_X - SLOT_W / 2, PLATE_D - SLOT_D, -1])
            cube([SLOT_W, SLOT_D + 1, PLATE_T + 2]);

        // screw hole down the middle of the DHT11 slot
        translate([DHT_X, PLATE_D - SLOT_D / 2, -1])
            cylinder(h = PLATE_T + 2, d = DHT_HOLE_D);

        // LDR light window
        translate([LDR_X, PLATE_D / 2, -1])
            cylinder(h = PLATE_T + 2, d = LDR_D);

        // screw hole beside the LDR
        translate([LDR_X + LDR_D / 2 + 3.5, PLATE_D / 2, -1])
            cylinder(h = PLATE_T + 2, d = LDR_HOLE_D);
    }
}

// small feet so the bracket stands off the enclosure floor
module feet() {
    for (x = [HOLE_INSET, PLATE_W - HOLE_INSET])
        translate([x, PLATE_D / 2, -4])
            cylinder(h = 4, d = 6);
}

module bracket() {
    difference() {
        union() {
            plate();
            feet();
        }
    }
}

bracket();