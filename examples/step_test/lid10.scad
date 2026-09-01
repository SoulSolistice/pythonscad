// Parametric bayonet-lock container / pipe coupler
//
// Generates three interchangeable parts that lock together with a quarter-turn
// bayonet:
//
//   base       - cup with a male lip and bayonet lugs on top
//   lid        - cup with a female socket and bayonet channels in its rim
//   separator  - male lip + lugs at one end, female socket + channels at the
//                other, so any number of them can be stacked between base and
//                lid.  Printed socket-up, then *inverted* for use: the socket
//                drops onto the base lip and its own lip points up at the lid.
//
// Any end can be left open (_openBase / _openLid / _openSeparator).  With the
// base and lid both open the pair becomes a two-piece pipe joined by the
// bayonet; add open separators to extend it.
//
// _hoseThread puts a tapered socket on the plain end of the base or lid, so a
// standard 150 mm AC hose screws in and tightens as it goes.
//
// Parts are dimensioned from the *interior*; the exterior size is derived.
//
// The shipped defaults are a 150 mm AC-hose coupler sized for a sturdy print:
// 3 mm walls, a 10 mm lip, four bayonet lugs and a cammed joint.  Render the
// base and the lid and you have a hose-to-hose coupling that releases with a
// quarter turn.  Turn _hoseThread off on one half for a container with a hose
// port instead.  See hardware/README.md for the reasoning and print notes.

use <utils/build_plate.scad>

/* [Part] */

// type to produce
_part = "base"; // [base,lid,separator]

// show the build volume in preview (never rendered or exported)
_showBuildPlate = true;

/* [Size] */

// minimum interior diameter of container
_insideDiameter = 150;

// height of the container's interior space (per part)
_interiorHeight = 95;

/* [Open ends] */

// omit the floor of the base, turning it into a pipe section with a male end
_openBase = false;

// omit the top of the lid, turning it into a pipe section with a female end
_openLid = false;

// omit the floor of the separator, turning it into a pipe extension
_openSeparator = false;

/* [Style] */

// exterior style of the part
_style = "round thin"; // [round, round thin, tapered, polygon, crown, flipped crown]

// for the polygon and crown styles (no effect on other styles)
_numberOfSides = 9;

// size of the 45 degree chamfer on the bottom edge, and (for polygon/crown) the
// radius of the vertical corners.  The bottom is chamfered rather than filleted
// so the part has a flat first layer to stick to
_edgeRounding = 2.0;

/* [Thickness] */

// the thinnest walls of the container will use this value
_minimumWallThickness = 3.0;

// horizontal thickness used for the top of the lid or bottom of the base
_topBottomThickness = 3.0;

/* [Bayonet] */

// height of the lip between lid and base
_lipHeight = 10.0;

// how much the locking bayonets protrude (larger values are needed for larger diameters)
_bayonetDepth = 1.5;

// number of lugs around the lip
_bayonetStarts = 4;

// angular size of a single lug
_bayonetAngle = 30;

// how far the lid turns to lock; _bayonetAngle + _twistAngle must stay below 360/_bayonetStarts
_twistAngle = 45;

// RADIAL clearance between the bayonet's mating faces.  Radial accuracy is the
// worst axis on an FDM printer at this diameter, so this is the loose one
_partGap = 0.25;

// AXIAL clearance.  Z is the accurate axis on an FDM printer - layer height
// controls it directly - so this can be far tighter than the radial fit, and it
// is what sets how much the locked joint rattles (play = 2x this)
_bayonetAxialGap = 0.10;

// Height of the crush bead on the lid/separator rim.  The bayonet cannot clamp
// flat-on-flat without straining the whole part, but it can flatten a small
// ridge: closing crushes this bead and that is what seals the joint.  0 = none,
// which also disables the cam and gives the plain clearance fit.
_sealBead = 0.8;

// How far the cam crushes the bead over the twist.  About a third of the bead
// height is the usual crush-rib figure - enough to seal, not so much that it
// takes real force or cold-flows away
_sealCrush = 0.3;

// ANGULAR clearance each side of a lug, in degrees.  Nothing depends on this
// being tight - the lug only has to drop into the slot - and deriving it from
// _partGap gave a 0.2 degree window that is very fussy to find by hand
_bayonetAngularGap = 1.0;

/* [Hose thread] */

// put a tapered hose socket on the plain end of the base or lid.  The hose
// screws in and tightens as it goes.  Forces that end open.
_hoseThread = true;

// nominal (inner) diameter of the hose, e.g. 150 for standard AC ducting
_hoseDiameter = 150;

// radial height of the hose's helical rib - MEASURE THIS, it varies by brand
_hoseRibDepth = 4.0;

// distance between two ribs - MEASURE over five ribs and divide by five
_hoseThreadPitch = 25.0;

// socket depth, in pitches
_hoseThreadTurns = 3.0;

// width of the ridge at its base, as a fraction of the pitch.  The rest of the
// pitch is the space the hose's own rib sits in, so this cannot be greedy.
_hoseThreadWidth = 0.45;

// diameter given up per turn, so the hose grips harder the further it screws in
_hoseThreadTaper = 1.0;

// angle of the thread flanks from horizontal.  The lower flank is an overhang
// AND the face that resists the hose pulling out, and a helical groove cannot be
// supported, so it has to carry its own weight: keep this above 45
_hoseThreadFlank = 48;

// length of the parallel (untapered) section at the far end of the socket.  The
// hose stops tightening here, so it bottoms out on the seat at a repeatable
// depth instead of jamming on the taper wherever its tolerance happens to put it
_hoseSeatLength = 10.0;

// radial width of the flat seat the hose end butts against.  This is the stop -
// without it the bore runs straight on into the bayonet end of the part.  Keep
// it close to the hose's end-face width (the echo reports it) and the seat sits
// flush with the hose bore, leaving nothing protruding into the airway
_hoseSeatWidth = 3.0;

// clearance between the socket and the hose.  Separate from _partGap because a
// hose is far less precise than a printed bayonet and wants a looser fit
_hoseGap = 0.4;

// lead-in chamfer at the socket mouth.  This used to be sized from the wall
// thickness, which at 3 mm cut away the whole of the part's first layer - the
// mouth is the face the part is printed on, so it has to keep a flat land
_hoseMouthChamfer = 1.2;

// O-ring cord or foam thickness for the face seal cut into the seat; 0 = no
// groove, just a plain stop.  It has to fit under the hose's end face, which is
// only about (_hoseRibDepth + _hoseGap - total taper) mm wide - check the echo
_hoseGasketCord = 1.5;

// how far the gasket is compressed when seated: about 0.25 for a rubber O-ring,
// 0.45 for soft foam cord
_gasketSqueeze = 0.45;

/* [Rendering] */

// use lower resolutions to adjust values, set to high for final rendering
_resolution = 60; // [30:Low, 60:Medium, 120:High, 240:Very high, 400:Extreme]

/* [Hidden] */

// build_plate() mode: 0 = Replicator 2, 1 = Replicator, 2 = Thing-o-Matic, 3 = manual
_buildPlateMode = 0;

////////////////////////////////////////////////////////////////////////

if (_showBuildPlate) build_plate(_buildPlateMode);
make();


module make($fn = _resolution) {
	wall  = _minimumWallThickness;
	deck  = _topBottomThickness;
	lip   = _lipHeight;
	depth = _bayonetDepth;
	gap   = _partGap;

	// Exterior size is derived from the requested interior size.  To work from
	// exterior dimensions instead, replace this line with a literal diameter -
	// everything downstream is derived from `radius`.
	outsideDiameter = _insideDiameter + 2*(wall*2 + gap + depth);
	radius = outsideDiameter/2;

	// female bore of every socket, and the radius the angular clearance is
	// measured at
	socketRadius = radius - wall - depth;

	// Angular clearance is a usability figure, not a fit figure: the lug only has
	// to find the slot.  Deriving it from _partGap gave +-0.18 degrees, which is
	// 0.5 mm of arc at this diameter and very fussy to engage by feel.
	channelAngle = _bayonetAngle + 2*_bayonetAngularGap;
	axialGap = _bayonetAxialGap;

	// The hose socket sits on the plain end - the end away from the bayonet -
	// so it only applies to the base and the lid, and that end must be open.
	hose = _hoseThread && (_part == "base" || _part == "lid");

	// The socket is sized from the hose's *outside*, over the ribs.  The ridge
	// crest then lands on the hose's rib valley, one rib depth further in.
	hoseOuter    = _hoseDiameter + 2*_hoseRibDepth;
	socketDepth  = _hoseThreadPitch * _hoseThreadTurns;
	socketRadius0 = hoseOuter/2 + _hoseGap;
	// radius given up per mm of depth; negative, so the socket closes inwards
	socketSlope  = -(_hoseThreadTaper/2)/_hoseThreadPitch;
	collarRadius = socketRadius0 + wall;

	// Only the first part of the socket tapers.  The last _hoseSeatLength runs
	// parallel, so the hose stops gaining interference and can bottom out on the
	// seat at a repeatable depth - a taper alone stops the hose wherever its
	// tolerance decides, which is no use if you want a face seal.
	taperLength   = socketDepth - _hoseSeatLength;
	socketRadius1 = socketRadius0 + socketSlope*taperLength;

	// The seat is the flat annulus the hose end butts against.  It is defined
	// outright rather than inherited from the part's bore - relying on the bore
	// gives a stop only when the container happens to be narrower than the hose.
	seatInner = socketRadius1 - _hoseSeatWidth;

	// Centre the gasket groove under the hose's end face, which runs from its
	// own inner radius out to wherever the socket has squeezed it.
	gasketRadius = (max(seatInner, _hoseDiameter/2) + socketRadius1)/2;
	gasketDepth  = _hoseGasketCord * (1 - _gasketSqueeze);

	// width of hose end face available to squeeze the gasket, and the land left
	// on each side of the groove once it is cut
	hoseFace  = socketRadius1 - _hoseDiameter/2;
	gasketLand = (hoseFace - 1.2*_hoseGasketCord)/2;

	// ease the seat's inner edge so it does not present a square step to the
	// flow, but never at the cost of the land that retains the gasket
	seatChamfer = _hoseGasketCord > 0
	              ? max(0, min(0.6, (gasketRadius - 0.6*_hoseGasketCord) - seatInner - 0.6))
	              : min(0.6, _hoseSeatWidth/4);

	// The channel is opened up by the bead height so the lug is still free when
	// the lid is resting on the uncrushed bead, and the cam then closes through
	// that slack plus the crush.  Without the extra height the bead would be
	// flattened the moment the parts met, and the twist would fight it the whole
	// way round instead of only at the end.
	seal    = _sealBead;
	camRise = seal > 0 ? 2*axialGap + _sealCrush : 0;

	// axial slack the lug has in its channel once locked
	bayonetPlay = seal > 0 ? 0 : 2*axialGap;

	// The ridge has to fit in the hose's rib valley, so its width comes first and
	// the depth is clamped to whatever the flank angle then allows.  The flank
	// runs over the ridge depth PLUS the backing that fuses it to the wall -
	// leaving the backing out of that sum is what put the first version's flanks
	// at 38 degrees when they were meant to be 45, unsupported over the whole
	// thread.
	ridgeBack  = 0.3;
	ridgeRoot  = _hoseThreadWidth * _hoseThreadPitch;
	ridgeDepth = min(_hoseRibDepth,
	                 (ridgeRoot - 0.08*_hoseThreadPitch)/(2*tan(_hoseThreadFlank))
	                 - ridgeBack);
	ridgeCrest = ridgeRoot - 2*(ridgeDepth + ridgeBack)*tan(_hoseThreadFlank);

	// the part's own bore, which the socket has to blend back into above the
	// collar run-out
	partBore = _part == "base" ? radius - wall*2 - depth - gap
	                           : circumscribe(radius - wall - depth);

	// keep the thinning relief clear of the socket, or it would undercut the
	// ridge and leave it floating
	reliefFloor = hose ? socketDepth + 2*wall : 0;

	// a closed end costs one deck thickness of height, an open one does not
	baseDeck = (_openBase || (hose && _part == "base")) ? 0 : deck;
	lidDeck  = (_openLid  || (hose && _part == "lid"))  ? 0 : deck;
	sepDeck  = _openSeparator ? 0 : deck;

	baseHeight      = _interiorHeight + baseDeck - lip;
	separatorHeight = _interiorHeight + sepDeck;
	lidHeight       = _interiorHeight + lidDeck + lip;

	// ---- sanity checks (silently broken geometry is worse than an error) ----
	assert(_insideDiameter > 0, "_insideDiameter must be positive");
	assert(_interiorHeight > 0, "_interiorHeight must be positive");
	assert(wall > 0, "_minimumWallThickness must be positive");
	assert(deck > 0 || (_openBase && _openLid && _openSeparator),
	       "_topBottomThickness must be positive unless every end is open");
	assert(depth > 0, "_bayonetDepth must be positive");
	assert(gap >= 0, "_partGap cannot be negative");
	assert(_numberOfSides >= 3, "_numberOfSides must be at least 3");
	assert(_bayonetStarts >= 1, "_bayonetStarts must be at least 1");
	assert(_bayonetAngle > 0, "_bayonetAngle must be positive");
	assert(_twistAngle > 0, "_twistAngle must be positive");
	assert(_resolution >= 12, "_resolution must be at least 12");
	assert(isStyle(_style), str("unknown _style: ", _style));
	assert(_part == "base" || _part == "lid" || _part == "separator",
	       str("unknown _part: ", _part));
	assert(lip > depth,
	       "_lipHeight must be larger than _bayonetDepth or the lugs collapse");
	assert(radius - wall*2 - depth - gap > 0,
	       "walls, bayonet and gap consume the whole radius");
	assert(_interiorHeight >= 2*lip,
	       "_interiorHeight must be at least 2*_lipHeight");
	assert(channelAngle + _twistAngle <= 180,
	       "_bayonetAngle + _twistAngle must not exceed 180 (smallArc limit)");
	assert(channelAngle + _twistAngle < 360/_bayonetStarts,
	       "bayonet features overlap: reduce _bayonetAngle/_twistAngle or _bayonetStarts");
	assert(!hose || _hoseDiameter > 0, "_hoseDiameter must be positive");
	assert(!hose || _hoseRibDepth > 0, "_hoseRibDepth must be positive");
	assert(!hose || _hoseThreadPitch > 0, "_hoseThreadPitch must be positive");
	assert(!hose || _hoseThreadTurns >= 1, "_hoseThreadTurns must be at least 1");
	assert(!hose || _hoseThreadTaper >= 0, "_hoseThreadTaper cannot be negative");
	assert(!hose || (_hoseThreadWidth > 0.15 && _hoseThreadWidth <= 0.6),
	       "_hoseThreadWidth must be between 0.15 and 0.6 of the pitch, or the hose rib has nowhere to sit");
	assert(!hose || ridgeDepth > 0.5,
	       "_hoseThreadWidth is too small to carry a usable ridge at this pitch");
	assert(!hose || (_hoseThreadFlank >= 45 && _hoseThreadFlank < 80),
	       "_hoseThreadFlank must be between 45 and 80 degrees or the thread will not print");
	assert(_bayonetAxialGap > 0, "_bayonetAxialGap must be positive");
	assert(_bayonetAngularGap > 0, "_bayonetAngularGap must be positive");
	assert(_sealBead >= 0, "_sealBead cannot be negative");
	assert(_sealBead == 0 || _sealCrush < _sealBead,
	       "_sealCrush must be less than _sealBead or the bead is flattened completely");
	assert(_sealBead == 0 ||
	       (circumscribe(radius - wall) + _sealBead + 0.4 < radius - _sealBead - 0.4),
	       "_sealBead does not fit between the bayonet channels and the outside wall");
	assert(_sealBead == 0 || camRise < lugHeightOf(lip, depth)/2,
	       "_sealBead + _sealCrush are too large for _lipHeight: the cam would close the channel");
	assert(!hose || _hoseSeatLength > 0, "_hoseSeatLength must be positive");
	assert(!hose || taperLength >= _hoseThreadPitch,
	       "_hoseSeatLength leaves less than one pitch of taper: raise _hoseThreadTurns");
	assert(!hose || _hoseThreadTaper*taperLength/_hoseThreadPitch < 2*ridgeDepth,
	       "total taper exceeds the rib depth: the hose would jam before it seats");
	assert(!hose || _hoseSeatWidth > 0, "_hoseSeatWidth must be positive");
	assert(!hose || seatInner > _hoseDiameter/2 - _hoseRibDepth,
	       "_hoseSeatWidth reaches more than a rib depth inside the hose bore, choking the airway");
	assert(!hose || _hoseGasketCord >= 0, "_hoseGasketCord cannot be negative");
	assert(_gasketSqueeze > 0 && _gasketSqueeze < 0.8,
	       "_gasketSqueeze must be between 0 and 0.8");
	assert(!hose || _hoseGap >= 0, "_hoseGap cannot be negative");
	assert(!hose || _hoseGasketCord == 0 || gasketLand >= 0.5,
	       "gasket cord is too thick for the hose's end face: thin _hoseGasketCord, or cut _hoseThreadTaper so the hose is squeezed less");
	assert(!hose || _hoseGasketCord == 0 ||
	       (gasketRadius - 0.6*_hoseGasketCord > seatInner &&
	        gasketRadius + 0.6*_hoseGasketCord < socketRadius1),
	       "gasket groove does not fit on the seat: widen _hoseSeatWidth or thin _hoseGasketCord");
	assert(!hose || socketDepth + 2*wall <=
	                (_part == "base" ? baseHeight : lidHeight - lip),
	       "hose socket is deeper than the part: reduce _hoseThreadTurns or raise _interiorHeight");

	if (_hoseThread && _part == "separator") {
		echo("note: no hose thread on a separator - it has bayonets at both ends, so there is no plain end to put one on");
	}

	echo(str("outside diameter = ", outsideDiameter, " mm"));
	echo(str("bayonet fit: ", gap, " mm radial clearance, ",
	         seal > 0
	         ? str(seal, " mm seal bead crushed ", _sealCrush, " mm (",
	               round(100*_sealCrush/seal), "%) by a ", camRise, " mm cam")
	         : str(bayonetPlay, " mm axial play, no bead")));
	echo(str("part height = ",
	         _part == "base" ? baseHeight + lip
	         : _part == "separator" ? separatorHeight + lip
	         : lidHeight, " mm"));

	if (hose) {
		echo(str("hose ridge: ", ridgeDepth, " mm deep, ", ridgeRoot,
		         " mm at the root, ", ridgeCrest, " mm at the crest, leaving ",
		         _hoseThreadPitch - ridgeRoot, " mm of pitch for the hose rib",
		         ridgeDepth < _hoseRibDepth
		         ? str(" (depth clamped from ", _hoseRibDepth,
		               " mm to hold the flanks at ", _hoseThreadFlank, " degrees)")
		         : ""));
		echo(str("effective thread: ",
		         max(0, (socketDepth - ridgeRoot)/_hoseThreadPitch - 0.40),
		         " turns at full depth"));
		echo(str("hose socket: ", 2*socketRadius0, " mm at the mouth, ",
		         2*socketRadius1, " mm through the parallel section, ",
		         socketDepth, " mm deep"));
		echo(str("hose seat: ", _hoseSeatWidth, " mm wide stop at ", socketDepth,
		         " mm deep, after ", _hoseSeatLength,
		         " mm of parallel socket; narrowest airway ", 2*seatInner, " mm",
		         _hoseGasketCord > 0
		         ? str("; gasket groove for ", _hoseGasketCord, " mm cord at ",
		               2*gasketRadius, " mm diameter, ", gasketDepth, " mm deep, ",
		               gasketLand, " mm land each side")
		         : " (no gasket groove)"));
		echo(str("airway: hose bore ", _hoseDiameter, " mm -> narrowest ",
		         2*seatInner, " mm -> part bore ", 2*partBore, " mm; seat stands ",
		         max(0, _hoseDiameter/2 - seatInner), " mm into the flow",
		         seatChamfer > 0 ? str(", eased by a ", seatChamfer, " mm chamfer") : ""));
		echo(str("hose collar outside diameter = ", 2*collarRadius, " mm",
		         collarRadius > radius
		         ? str(" (stands ", 2*(collarRadius - radius), " mm proud of the body)")
		         : ""));
	}

	if (_part == "base") {
		withHoseThread(hose, socketRadius0, socketSlope, taperLength, socketDepth,
		               _hoseThreadPitch, ridgeDepth, ridgeCrest, ridgeRoot, ridgeBack, wall,
		               partBore, seatInner, _hoseGasketCord, gasketRadius,
		               gasketDepth, seatChamfer, _hoseMouthChamfer)
		makeBase(style = _style, radius = radius, bodyHeight = baseHeight,
		         wall = wall, deck = baseDeck, lip = lip, depth = depth,
		         lugAngle = _bayonetAngle, gap = gap,
		         sides = _numberOfSides, starts = _bayonetStarts,
		         rounding = _edgeRounding, open = _openBase || hose,
		         reliefFloor = reliefFloor);

	} else if (_part == "separator") {
		makeSeparator(style = _style, radius = radius, bodyHeight = separatorHeight,
		              wall = wall, deck = sepDeck, lip = lip, depth = depth,
		              lugAngle = _bayonetAngle, channelAngle = channelAngle,
		              twist = _twistAngle, gap = gap, axialGap = axialGap,
		              seal = seal, cam = camRise,
		              sides = _numberOfSides, starts = _bayonetStarts,
		              rounding = _edgeRounding, open = _openSeparator);

	} else if (_part == "lid") {
		withHoseThread(hose, socketRadius0, socketSlope, taperLength, socketDepth,
		               _hoseThreadPitch, ridgeDepth, ridgeCrest, ridgeRoot, ridgeBack, wall,
		               partBore, seatInner, _hoseGasketCord, gasketRadius,
		               gasketDepth, seatChamfer, _hoseMouthChamfer)
		makeLid(style = _style, radius = radius, bodyHeight = lidHeight,
		        wall = wall, deck = lidDeck, lip = lip, depth = depth,
		        channelAngle = channelAngle, twist = _twistAngle, gap = gap,
		        axialGap = axialGap, seal = seal, cam = camRise,
		        sides = _numberOfSides, starts = _bayonetStarts,
		        rounding = _edgeRounding, open = _openLid || hose,
		        reliefFloor = reliefFloor);
	}
}


// Cup with a male lip and outward-facing lugs at the top.
module makeBase(style, radius, bodyHeight, wall, deck, lip, depth, lugAngle,
                gap, sides, starts, rounding, open, reliefFloor = 0) {
	bore       = radius - wall*2 - depth - gap;   // interior
	lipRadius  = bore + wall;                     // outside of the male lip
	fullHeight = bodyHeight + lip;
	chamfer    = depth;
	lugHeight  = (lip - chamfer)/2;
	round      = safeRounding(rounding, radius, bodyHeight);
	inset      = max(deck, wall);
	eps        = 0.1;

	difference() {
		union() {
			// body - lipRadius is the taper target so base, separator and lid
			// all share one cone angle
			makeBody(style, radius, bodyHeight, round, sides, lipRadius);

			// lip
			translate([0, 0, round + eps]) {
				cylinder(r = lipRadius, h = fullHeight - round - eps);
			}

			// lugs, flush with the top of the lip, ramped underneath
			bayonetLugs(rInner = lipRadius - eps, rOuter = lipRadius + depth,
			            angle = lugAngle, zBottom = fullHeight - lugHeight,
			            height = lugHeight, rotation = 0, starts = starts,
			            ramp = -chamfer, rampInner = bore, rampOuter = lipRadius);
		}

		// interior
		translate([0, 0, open ? -eps : deck]) {
			cylinder(r = bore, h = fullHeight + 2*eps);
		}

		// thin the side walls back to _minimumWallThickness
		if (style == "round thin") {
			thinRelief(rOuter = radius - wall, rInner = bore,
			           zBottom = max(inset, reliefFloor), zTop = bodyHeight - inset,
			           taperBottom = open || reliefFloor > 0, taperTop = true);
		}
	}
}


// Cup with a female socket and bayonet channels in its rim.
module makeLid(style, radius, bodyHeight, wall, deck, lip, depth, channelAngle,
               twist, gap, axialGap, sides, starts, rounding, open,
               seal = 0, cam = 0, reliefFloor = 0) {
	// holes are circumscribed so faceting never eats into the fit
	socket        = circumscribe(radius - wall - depth);
	channelOuter  = circumscribe(radius - wall);
	lipRadius     = radius - wall - depth - gap;  // taper target, matches the base
	chamfer       = depth;
	lugHeight     = (lip - chamfer)/2;
	round         = safeRounding(rounding, radius, bodyHeight);
	inset         = max(deck, wall);
	channelBottom = bodyHeight - lip - axialGap;
	eps           = 0.1;

	difference() {
		union() {
			makeBody(style, radius, bodyHeight, round, sides, lipRadius);
			if (seal > 0) {
				sealBead((channelOuter + radius)/2, seal, bodyHeight);
			}
		}

		// interior
		translate([0, 0, open ? -eps : deck]) {
			cylinder(r = socket, h = bodyHeight + 2*eps);
		}

		// thin the side walls, stopping short of the socket
		if (style == "round thin") {
			thinRelief(rOuter = radius - wall, rInner = socket,
			           zBottom = max(inset, reliefFloor), zTop = bodyHeight - lip - inset,
			           taperBottom = open || reliefFloor > 0, taperTop = true);
		}

		// entry slots, open at the rim
		bayonetLugs(rInner = socket - eps, rOuter = channelOuter,
		            angle = channelAngle, zBottom = channelBottom,
		            height = lip + axialGap + eps, rotation = twist + channelAngle,
		            starts = starts);

		// the channel the lugs twist along, its roof climbing as they go
		bayonetChannel(rInner = socket - eps, rOuter = channelOuter,
		               lugAngle = channelAngle, zBottom = channelBottom,
		               height = lugHeight + 2*axialGap + seal,
		               entryRotation = twist + channelAngle, twist = twist,
		               cam = cam, chamfer = chamfer, starts = starts);
	}
}


// Male lip + lugs at the bottom, female socket + channels at the top.
// Printed as generated; inverted in use.
module makeSeparator(style, radius, bodyHeight, wall, deck, lip, depth, lugAngle,
                     channelAngle, twist, gap, axialGap, sides, starts, rounding,
                     open, seal = 0, cam = 0) {
	bore          = radius - wall*2 - depth - gap;
	lipRadius     = bore + wall;
	socket        = circumscribe(radius - wall - depth);
	channelOuter  = circumscribe(radius - wall);
	fullHeight    = bodyHeight + lip;
	chamfer       = depth;
	lugHeight     = (lip - chamfer)/2;
	inset         = max(deck, wall);
	channelBottom = fullHeight - lip - axialGap;
	round         = safeRounding(rounding, radius, bodyHeight);
	eps           = 0.1;

	difference() {
		union() {
			// lip
			cylinder(r = lipRadius, h = fullHeight);

			// body - no bottom fillet, it has to sit flat on the part below
			translate([0, 0, lip]) {
				makeBody(style, radius, bodyHeight, round, sides, lipRadius,
				         bottomFillet = false);
			}

			// crush bead on the top rim, the face that meets the shoulder above
			if (seal > 0) {
				sealBead((channelOuter + radius)/2, seal, fullHeight);
			}

			// lugs, flush with the bottom of the lip, ramped on top - the
			// mirror of the base, because this part is used upside down
			bayonetLugs(rInner = lipRadius - eps, rOuter = lipRadius + depth,
			            angle = lugAngle, zBottom = 0, height = lugHeight,
			            rotation = 0, starts = starts,
			            ramp = chamfer, rampInner = bore, rampOuter = lipRadius);
		}

		// interior through the lip
		translate([0, 0, open ? -eps : deck]) {
			cylinder(r = bore, h = fullHeight + 2*eps);
		}

		// interior above the lip.  This is bored to `socket`, so it doubles as
		// the female socket at the top - no separate socket cut is needed.
		translate([0, 0, lip + inset]) {
			cylinder(r = socket, h = fullHeight);
		}

		if (style == "round thin") {
			thinRelief(rOuter = radius - wall, rInner = socket,
			           zBottom = lip + inset, zTop = fullHeight - lip - inset,
			           taperBottom = true, taperTop = true);
		}

		// entry slots, open at the top
		bayonetLugs(rInner = socket - eps, rOuter = channelOuter,
		            angle = channelAngle, zBottom = channelBottom,
		            height = lip + axialGap + eps, rotation = twist + channelAngle,
		            starts = starts);

		// the channel the lugs twist along, its roof climbing as they go
		bayonetChannel(rInner = socket - eps, rOuter = channelOuter,
		               lugAngle = channelAngle, zBottom = channelBottom,
		               height = lugHeight + 2*axialGap + seal,
		               entryRotation = twist + channelAngle, twist = twist,
		               cam = cam, chamfer = chamfer, starts = starts);
	}
}


// Exterior shell.  `taperRadius` is the bottom radius used by the tapered
// style; every part passes the same value so the profiles line up.
// `bottomFillet` rounds the bottom edge - off for the separator, whose body has
// to sit flat on the part below it.  The vertical corner radius of the polygon
// and crown styles is unaffected, so every part keeps the same silhouette.
module makeBody(style, radius, height, rounding, sides, taperRadius, bottomFillet = true) {
	if (style == "tapered") {
		taperedCylinder(radius, taperRadius, height, rounding, bottomFillet);
	} else if (style == "crown") {
		crownCylinder(sides, radius, height, rounding, bottomFillet);
	} else if (style == "flipped crown") {
		translate([0, 0, height])
		mirror([0, 0, 1]) {
			crownCylinder(sides, radius, height, rounding, bottomFillet);
		}
	} else if (style == "polygon") {
		polyCylinder(sides, radius, height, rounding, bottomFillet);
	} else {
		// "round" and "round thin" share the same exterior
		roundCylinder(radius, height, rounding, bottomFillet);
	}
}


////////////// Hose thread ///////////////////

// Wraps a part in its hose socket: adds the collar, bores the tapered socket
// out of it, then lays the helical ridge onto the socket wall.  Passing
// enabled = false hands the part straight through, so the rest of the model
// does not have to know about any of this.
module withHoseThread(enabled, radius0, slope, taperLength, socketDepth, pitch,
                      ridgeDepth, crestWidth, rootWidth, back, wall, boreRadius,
                      seatInner, gasketCord, gasketRadius, gasketDepth,
                      seatChamfer, mouthChamfer) {
	if (!enabled) {
		children();
	} else {
		union() {
			difference() {
				union() {
					children();
					hoseCollar(radius0 + wall, socketDepth, wall);
				}
				hoseSocket(radius0, slope, taperLength, socketDepth, wall,
				           boreRadius, seatInner, gasketCord, gasketRadius,
				           gasketDepth, seatChamfer, mouthChamfer);
			}
			hoseRidge(radius0, slope, taperLength, socketDepth, pitch, ridgeDepth,
			          crestWidth, rootWidth, back);
		}
	}
}

// Boss the socket is bored out of, with a 45 degree run-out into the body so
// there is never a horizontal ledge to print over.
module hoseCollar(radius, height, wall) {
	// chamfer the bottom edge: this is the face the part prints on
	hull() {
		translate([0, 0, 0.6]) cylinder(r = radius, h = height - 0.6);
		cylinder(r = radius - 0.6, h = 0.01);
	}
	translate([0, 0, height]) {
		cylinder(r1 = radius, r2 = max(0.01, radius - 2*wall), h = 2*wall);
	}
}

// The tapered bore itself, plus a chamfer at the mouth to help the hose start.
// It is cut past z = 0 so the mouth is a clean opening.
//
// The last cut is what stops the collar's run-out from sealing the part shut:
// the collar is solid all the way to the axis, so the bore has to be carried on
// through the run-out and blended into the part's own bore.  Where the socket
// is wider than that bore - the usual case, since the hose is wider than the
// container - the blend doubles as the shoulder the hose seats against.
module hoseSocket(radius0, slope, taperLength, socketDepth, wall, boreRadius,
                  seatInner, gasketCord, gasketRadius, gasketDepth,
                  seatChamfer, mouthChamfer) {
	eps = 0.1;
	runOut = 2*wall;
	radiusEnd = radius0 + slope*taperLength;

	// chamfer at the mouth, so the hose finds its way in
	translate([0, 0, -eps]) {
		cylinder(r1 = radius0 + mouthChamfer + eps, r2 = radius0, h = mouthChamfer + eps);
	}

	// the tapered section, which does the tightening
	translate([0, 0, -eps]) {
		cylinder(r1 = radius0 - slope*eps, r2 = radiusEnd, h = taperLength + eps);
	}

	// the parallel section: interference stops growing here, so the hose runs on
	// to the seat instead of wedging at whatever depth its tolerance dictates
	translate([0, 0, taperLength]) {
		cylinder(r = radiusEnd, h = socketDepth - taperLength);
	}

	// past the seat, open back out to the part's own bore.  Starting this at
	// seatInner rather than radiusEnd is what leaves the seat standing.
	translate([0, 0, socketDepth]) {
		cylinder(r1 = seatInner, r2 = max(boreRadius, seatInner), h = runOut + eps);
	}

	// ease the seat's inner edge, so the airway sees a chamfer and not a square
	// step where the seat stands proud of the hose bore
	if (seatChamfer > 0) {
		translate([0, 0, socketDepth - seatChamfer]) {
			cylinder(r1 = seatInner, r2 = seatInner + seatChamfer, h = seatChamfer + eps);
		}
	}

	// face-seal groove in the seat
	if (gasketCord > 0) {
		translate([0, 0, socketDepth - eps])
		difference() {
			cylinder(r = gasketRadius + 0.6*gasketCord, h = gasketDepth + eps);
			translate([0, 0, -eps]) {
				cylinder(r = gasketRadius - 0.6*gasketCord, h = gasketDepth + 3*eps);
			}
		}
	}
}

// The helical ridge, swept as a single polyhedron rather than stacked slices so
// the pitch is exact and CGAL only sees one solid.
//
// The profile is given as (dr, dz) offsets from the socket wall: positive dr
// reaches back into the collar so the ridge is always fused to it, negative dr
// is the part that protrudes into the hose.  Only the protruding part is scaled
// by the run-out factor, so both ends of the thread fade into the wall instead
// of stopping dead - that is what lets the hose catch the start of the thread.
//
// The sweep is inset by half a root width at each end so the ridge stays inside
// the socket while keeping the pitch exactly as measured.  It follows the socket
// through both the tapered and the parallel section, so the hose stays engaged
// right up to the seat.
module hoseRidge(radius0, slope, taperLength, socketDepth, pitch, ridgeDepth,
                 crestWidth, rootWidth, back) {
	zStart  = rootWidth/2;
	span    = socketDepth - rootWidth;
	turns   = span/pitch;
	steps   = max(24, round($fn*turns));
	// run-outs expressed in turns, then converted to a fraction of the sweep
	leadIn  = min(0.35, 0.25/turns);
	leadOut = min(0.25, 0.15/turns);
	np      = 4;

	assert(span > 0,
	       "hose socket is shallower than one thread root: raise _hoseThreadTurns");

	// Built one row per station rather than as one flat list, so the sweep can
	// hand the exporter the order it generated in. That ordering is the one
	// thing a mesh loses and no measurement recovers, and it is what lets the
	// thread go out as a surface instead of as a thousand facets. `points` is
	// the same list it always was, flattened back out of the rows.
	rows = [
		for (i = [0 : steps])
		let (t = i/steps,
		     a = 360*turns*t,
		     z = zStart + span*t,
		     r = radius0 + slope*min(z, taperLength),
		     f = max(0, min(1, t/leadIn, (1 - t)/leadOut)))
		[ for (p = [[back, -rootWidth/2], [-ridgeDepth*f, -crestWidth/2],
		            [-ridgeDepth*f, crestWidth/2], [back, rootWidth/2]])
			[(r + p[0])*cos(a), (r + p[0])*sin(a), z + p[1]] ]
	];
	points = [ for (row = rows) for (p = row) p ];

	faces = concat(
		[ for (i = [0 : steps - 1]) for (j = [0 : np - 1]) for (k = [0, 1])
			let (a0 = i*np + j,
			     b0 = i*np + (j + 1)%np,
			     c0 = (i + 1)*np + (j + 1)%np,
			     d0 = (i + 1)*np + j)
			k == 0 ? [a0, c0, b0] : [a0, d0, c0] ],
		[ [0, 1, 2, 3] ],
		[ [steps*np + 3, steps*np + 2, steps*np + 1, steps*np] ]
	);

	// The profile closes on itself - four points round the ridge - so `closed`.
	declare_grid(points = rows, closed = true)
	polyhedron(points = points, faces = faces, convexity = 8);
}


// Crush bead on the rim, as a triangular ring with 45 degree flanks so it prints
// without support.  It sits outboard of the bayonet channels, where the mating
// rim is continuous - a bead crossing the entry slots would seal against nothing
// for a quarter of its length.
module sealBead(beadRadius, height, z) {
	translate([0, 0, z])
	rotate_extrude(convexity = 4)
	polygon([[beadRadius - height, 0], [beadRadius + height, 0], [beadRadius, height]]);
}


// The channel a lug twists along, swept as a single polyhedron.
//
// The parts are used with the lid inverted, so the channel's TOP in print
// orientation is the floor of the assembled joint - the surface the lug bears
// down on, and the only one that can pull the lid onto the base.  Lowering that
// top along the travel is what closes the joint.  `cam` is how much it closes.
//
// Two things this has to get right, both learned the hard way:
//
//  - The closure holds FLAT over the last `lugAngle` of travel.  A lug has
//    angular width, so at the locked position its leading edge sits over channel
//    carved earlier in the sweep; ramp all the way to the end and only the
//    trailing edge ever reaches full closure.
//  - It is one swept solid, not a stack of rotated arcs.  The stacked-arc version
//    measured correct in isolation and then failed to cut anything at all once
//    called from makeLid, which is not a failure mode worth risking twice.
//
// The profile is a quadrilateral in (r,z): flat bottom, and a top that steps down
// by `chamfer` from the bore outwards so the ceiling prints without support.
module bayonetChannel(rInner, rOuter, lugAngle, zBottom, height, entryRotation,
                      twist, cam, chamfer, starts) {
	span   = lugAngle + twist;
	locked = 90 + entryRotation - span;
	steps  = max(24, ceil(span/1.5));
	np     = 4;

	points = [
		for (i = [0 : steps])
		let (a  = span*i/steps,
		     th = locked + a,
		     h  = a <= lugAngle ? height - cam
		                        : height - cam*(1 - (a - lugAngle)/twist))
		for (q = [[rOuter, 0], [rInner, 0], [rInner, h + chamfer], [rOuter, h]])
			[q[0]*cos(th), q[0]*sin(th), zBottom + q[1]]
	];

	faces = concat(
		[ for (i = [0 : steps - 1]) for (j = [0 : np - 1]) for (k = [0, 1])
			let (a0 = i*np + j,
			     b0 = i*np + (j + 1)%np,
			     c0 = (i + 1)*np + (j + 1)%np,
			     d0 = (i + 1)*np + j)
			k == 0 ? [a0, c0, b0] : [a0, d0, c0] ],
		[ [0, 1, 2, 3] ],
		[ [steps*np + 3, steps*np + 2, steps*np + 1, steps*np] ]
	);

	for (i = [0 : starts - 1])
	rotate([0, 0, i*360/starts])
	polyhedron(points = points, faces = faces, convexity = 8);
}


// `starts` copies of one bayonet feature, evenly spaced.  A non-zero `ramp`
// hulls the arc with a second arc offset that far in z, forming the lead-in
// ramp that wedges the parts together.
module bayonetLugs(rInner, rOuter, angle, zBottom, height, rotation, starts,
                   ramp = 0, rampInner = 0, rampOuter = 0) {
	for (i = [0 : starts - 1])
	rotate([0, 0, i*360/starts + rotation])
	translate([0, 0, zBottom + height/2]) {
		hull() {
			smallArc(rInner, rOuter, angle, height);
			if (ramp != 0) {
				translate([0, 0, ramp]) {
					smallArc(rampInner, rampOuter, angle, height);
				}
			}
		}
	}
}


// Cut that thins a side wall between two heights, blended into the bore with a
// cone at whichever ends are asked for.  Silently does nothing when the part is
// too short for it, rather than emitting a negative-height cylinder.
module thinRelief(rOuter, rInner, zBottom, zTop, taperBottom, taperTop) {
	taper  = max(0, rOuter - rInner);
	bottom = taperBottom ? taper : 0;
	top    = taperTop ? taper : 0;
	height = zTop - zBottom;

	if (rOuter > rInner && height > bottom + top + 0.01) {
		hull() {
			translate([0, 0, zBottom]) {
				cylinder(r = rInner, h = height);
			}
			translate([0, 0, zBottom + bottom]) {
				cylinder(r = rOuter, h = height - bottom - top);
			}
		}
	}
}


////////////// Utility Functions ///////////////////

function lugHeightOf(lip, depth) = (lip - depth)/2;

function isStyle(s) = s == "round" || s == "round thin" || s == "tapered"
                   || s == "polygon" || s == "crown" || s == "flipped crown";

// angle subtended by an arc of length `len` at radius `r`
function arcAngle(len, r) = (r <= 0) ? 0 : len*180/(PI*r);

// radius at which an $fn-sided polygon's flats sit on `r`; use for holes so
// faceting never removes engagement from a fit
function circumscribe(r) = r / cos(180/$fn);

// keep the chamfer inside what the radius and the part height allow
function safeRounding(rounding, radius, height) =
	max(0, min(rounding, radius/2 - 0.01, height/2));

// corner resolution for the polygon/crown lobes, kept modest so a high $fn
// does not turn a 20-lobe hull into tens of thousands of facets
function cornerFn() = max(12, min(60, floor($fn/2)));

// only works from (0 to 180] degrees, runs clockwise from 12 o'clock
module smallArc(radius0, radius1, angle, depth) {
	assert(angle > 0 && angle <= 180, str("smallArc(): angle out of range: ", angle));
	eps = 0.01;

	difference() {
		// full ring
		cylinder(r = radius1, h = depth, center = true);
		cylinder(r = radius0, h = depth + 2*eps, center = true);

		for (z = [0, 180 - angle]) {
			rotate([0, 0, z])
			translate([-radius1, 0, 0]) {
				cube(size = [radius1*2, radius1*2, depth + eps], center = true);
			}
		}
	}
}


module roundCylinder(radius, height, rounding, bottomFillet = true) {
	if (rounding <= 0 || !bottomFillet) {
		cylinder(r = radius, h = height);
	} else {
		// A 45 degree chamfer, not a fillet.  A fillet meets the bed tangentially,
		// so the first layer is a line with no area at all - measured as 0 mm2 on
		// a 165 mm part, which will not stay stuck.  A chamfer leaves a flat face
		// and still knocks the sharp edge off.
		hull() {
			translate([0, 0, rounding]) {
				cylinder(r = radius, h = height - rounding);
			}
			cylinder(r = max(0.01, radius - rounding), h = 0.01);
		}
	}
}

module taperedCylinder(radius1, radius2, height, rounding, bottomFillet = true) {
	eps = 0.1;
	hull() {
		translate([0, 0, height - eps]) {
			cylinder(r = radius1, h = eps);
		}

		if (rounding <= 0 || !bottomFillet) {
			cylinder(r = radius2, h = eps);
		} else {
			hull() {
				translate([0, 0, rounding]) cylinder(r = radius2, h = eps);
				cylinder(r = max(0.01, radius2 - rounding), h = 0.01);
			}
		}
	}
}

// Vertices sit at (radius - rounding)/cos(angle/2) so that the corner radius
// puts the *flats* exactly on `radius`.  With rounding == 0 the corner radius
// is zero too, which means a plain prism circumscribing `radius` - not a hull
// of unit-radius cylinders.
module crownCylinder(sides, radius, height, rounding, bottomFillet = true) {
	eps = 0.1;
	angle = 360/sides;
	vertexRadius = (radius - rounding)/cos(angle/2);

	hull() {
		translate([0, 0, height - eps]) {
			cylinder(r = radius, h = eps);
		}

		if (rounding <= 0) {
			// rotated so a vertex points at +y, matching the lobed variant
			rotate([0, 0, 90]) {
				cylinder(r = vertexRadius, h = eps, $fn = sides);
			}
		} else {
			cornerLobes(sides, vertexRadius, rounding, bottomFillet);
		}
	}
}

module polyCylinder(sides, radius, height, rounding, bottomFillet = true) {
	angle = 360/sides;
	vertexRadius = (radius - rounding)/cos(angle/2);

	if (rounding <= 0) {
		rotate([0, 0, 90]) {
			cylinder(r = vertexRadius, h = height, $fn = sides);
		}
	} else {
		hull() {
			translate([0, 0, height - rounding])
			for (i = [0 : sides - 1])
			rotate([0, 0, i*angle])
			translate([0, vertexRadius, 0]) {
				cylinder(r = rounding, h = rounding, $fn = cornerFn());
			}

			cornerLobes(sides, vertexRadius, rounding, bottomFillet);
		}
	}
}

// The corner features at the bottom of a polygon/crown body: spheres when the
// bottom edge should be filleted, plain cylinders when it has to sit flat.
// Either way the vertical corner radius is the same, so a part with a flat
// bottom keeps the same silhouette as one with a filleted bottom.
module cornerLobes(sides, vertexRadius, rounding, bottomFillet) {
	angle = 360/sides;

	for (i = [0 : sides - 1])
	rotate([0, 0, i*angle])
	translate([0, vertexRadius, 0]) {
		if (bottomFillet) {
			// chamfered, not filleted - see roundCylinder
			hull() {
				translate([0, 0, rounding]) cylinder(r = rounding, h = 0.01, $fn = cornerFn());
				cylinder(r = 0.01, h = 0.01, $fn = cornerFn());
			}
		} else {
			cylinder(r = rounding, h = rounding, $fn = cornerFn());
		}
	}
}
