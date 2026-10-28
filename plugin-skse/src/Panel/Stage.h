// Stage.h
// Lifecast - the character view: a frame in the page, and the camera and body
// moves that put the character in it.
//
// Nothing is drawn here. The page is transparent outside the panel and the
// game keeps drawing the RaceSex Menu's character behind it; the frame is a
// region of the page, and what shows inside it is decided by the camera. So
// this module moves the camera and turns the body, and the page only draws a
// border and the controls around it.
//
// Five moves, all relative to what the menu showed when it opened:
//
//   turn      the body, about its own vertical axis - RaceMenu's own
//             SetPlayerRotation, the call its camera mode makes (delegated)
//   distance  the camera, along the line from the player to it, in the
//             ground plane - RaceMenu's SetRaceSexCameraPos
//   lift      the camera, up and down - RaceMenu's SetRaceSexCameraPos
//   pan       the camera, sideways - perpendicular to the player-camera
//             line, in the ground plane - RaceMenu's SetRaceSexCameraPos.
//             E4, Fase 6.5: the right-button drag moved lift only; the
//             in-game testing measured that as "so mexe na vertical" and this is
//             the axis that was missing.
//   tilt      the camera node's own rotation, about the horizontal axis
//             across the line of sight. RaceMenu has no call for this (it
//             reads the rotation and never writes it), so the node is
//             written directly: its local rotation, then Update.
//
// Every move is computed from the frame kept at the first move after the menu
// opened - never added to the last written value -, so a long drag does not
// drift. The kept frame is dropped when the menu closes.
//
// A move sets a TARGET and the camera slides to it, because applying a move
// whole in the frame it arrives in reads as jumpy: a button click is 15
// degrees at once, and a drag arrives as steps 50 ms apart. So "stage-move"
// moves the target and nothing else; a tick moves the current position a
// fraction of the gap that is left and writes camera, body and tilt; and the
// state sent back to the page is the TARGET, which is what the player asked
// for and what a button sitting at a limit has to reflect. Both are offsets
// from the kept frame - the easing lives between them, and neither is ever
// read back from the engine.
//
// So "reframe" is no longer exact at the instant of the click: it sets the
// target to the kept frame, and the kept frame is written when the current
// position gets there. Arrival is real and not asymptotic - inside the snap
// epsilon the current position is set to the target - which is what lets a
// button at a limit come back and what ends the tick.
//
// Limits are clamped on the TARGET, never on the current position: clamping
// what is about to be written would stop a button for a reason the page has
// no way to show. A target that gets clamped says so in the log.
//
// Limits are part of the design, not a detail: the camera never comes closer
// to the player than kMinDistance in the ground plane, never goes below the
// player's origin plus kFloorMargin, never strays sideways past kPanMax, and
// never tilts past kTiltMax. The one measured way to lose the character is
// the camera ending up almost on top of the player (1 unit away in the plane
// and 124 above the origin). kPanMax is not measured the same way - it has
// no failure mode like that to bound it against, so it is set to the same
// order of magnitude as kLiftUp and is retuned during in-game review
// (E4, Fase 6.5).
//
// Every RaceMenu call and every node write runs on the UI thread, like
// RaceMenu's own calls from its film; the answer to the page is sent from
// the game thread. Moves that arrive while one is queued are folded into it.
//
// Refusal: without the RaceSex Menu's film, without RaceMenu's CharGen object
// or any of the five calls, or with the camera on top of the player, the
// page is told "not ready" with the reason, and the frame shows it. Nothing
// else in the panel depends on this module.
//
// Messages (see Panel/Bridge.h for the envelope):
//
//   page -> dll  "stage-get"    {}
//   page -> dll  "stage-move"   { "turn": deg, "distance": units,
//                                 "lift": units, "pan": units,
//                                 "tilt": deg }  (deltas, any subset)
//   page -> dll  "stage-reset"  {}
//   page -> dll  "stage-mode"   { "free": bool } the camera mode. STANDARD is
//                                 what the menu opens in: RaceMenu's own
//                                 camera, untouched, and only the turn (the
//                                 body) of ours; every other move is
//                                 dropped HERE, so a page that forgot to hide
//                                 a control still cannot move the camera.
//                                 FREE is the five moves of before. The
//                                 comparison turns FREE on when it opens and
//                                 gives the mode back when it closes.
//   page -> dll  "stage-lock"   { "on": bool } the lock on the character (Acabamento 9),
//                                 free mode. ON is what the view always did and is
//                                 the default: the camera keeps the subject framed
//                                 and re-aims as she drifts. OFF, nothing of ours
//                                 aims the camera - no solve, no re-aim - and it stays
//                                 where it is until the player moves it. Turning it
//                                 back on frames once. Back to ON when the menu closes.
//                                 The state carries it as "lock".
//   page -> dll  "stage-zoom"   { "from": text } standard mode only: asks RaceMenu for its own zoom (far <-> near)
//                                 "from" says what asked (key, dll-lalt, wheel,
//                                 button, pad); it is only logged.
//   page -> dll  "stage-frame"  { "on": bool, "left": frac, "right": frac }
//                                 the character view's edges as a fraction of
//                                 the window width, and whether the camera
//                                 should keep the subject in the middle of
//                                 THEM rather than of the window. "on" absent
//                                 means true: the page sends this whenever
//                                 the view is resized, and the common message
//                                 should not have to carry the flag. See
//                                 FrameOne below.
//   dll -> page  "stage"        { "ready": false, "why": text }
//                            or { "ready": true, "tilt": bool,
//                                 "turn": deg, "distance": units,
//                                 "lift": units, "pan": units,
//                                 "tiltDeg": deg,
//                                 "free": bool, "forced": bool,
//                                 "frame": { "on": bool, "left": frac,
//                                            "right": frac },
//                                 "limits": { "minDistance", "maxDistance",
//                                             "liftMin", "liftMax",
//                                             "panMin", "panMax",
//                                             "tiltMax" } }
//   page -> dll  "stage-tune"   { "maxDistance", "liftUp", "panMax",
//                                 "pairApart", "pairSpan", "ease" }
//                                 (any subset; absent means leave alone)
//                                 ONLY in a LIFECAST_TUNING build - a release
//                                 DLL has no handler and answers unknown-type.
//                                 The answer carries "tuning" with all seven, and
//                                 "tuning" is absent from a release build, which
//                                 is what keeps the page's section hidden there.
//                                 The same state carries "tuningDefaults": the seven as
//                                 COMPILED, which the page's reset sends back through
//                                 this same message. maxDistance alone has a floor -
//                                 a value below the nearest the camera can go is
//                                 raised to it, and "tuneSaid" says so.
//                                 The seven do NOT go in "limits": the page
//                                 disables buttons from that key, so it means
//                                 "how far a move may go" - true of maxDistance,
//                                 liftUp and panMax, and false of the other
//                                 three. Reporting them together would disable
//                                 a button from a number that is not its bound.
//   dll -> page  "key-lalt"     {} Prisma only: LAlt went down with the panel focused
//                                 (PrismaBackend.cpp); the page treats it as its own LAlt.
//   dll -> page  "stage-visible" { "on": bool }  hides or shows the frame and
//                                 its controls; the view itself is untouched.
//                                 Not sent by this module - it is there for
//                                 whoever needs the panel without the frame.

#pragma once

// Seven numbers of the view are retuned during in-game review (six
// are declared below, kEase lives in Stage.cpp), and trying one used to cost
// a build and a boot. LIFECAST_TUNABLE is how they are turned instead: in a
// release build it is plain constexpr and nothing can write them, and in a
// build configured with LIFECAST_TUNING the panel can set them
// at run time ("stage-tune"). The VALUE is written once, here, for both.
//
// It is a declarator macro rather than two #ifdef branches per number because
// two branches means writing each value TWICE, and the release branch is the
// one nobody exercises - seven numbers would be seven chances for the two copies
// to drift apart in silence. The macro has one job and no logic, and no use
// site changes spelling.
//
// Which numbers are tunable is NOT a judgement about which matter: it is the
// list chosen by design. kMinDistance and kFloorMargin stay constant
// because they bound the one measured way to lose the character (see the note
// below). kTiltMax joined the list later, because the tilt limit is one more
// number the view's reach depends on and costs one declaration to expose.
#ifdef LIFECAST_TUNING
	#define LIFECAST_TUNABLE inline double
#else
	#define LIFECAST_TUNABLE inline constexpr double
#endif

namespace Lifecast::Panel::Stage
{
	inline constexpr double kMinDistance = 33.56;   // camera to player, ground plane
	LIFECAST_TUNABLE        kMaxDistance = 320.0;
	LIFECAST_TUNABLE        kLiftUp = 80.0;         // above the opening height
	inline constexpr double kFloorMargin = 12.0;    // above the player's origin
	LIFECAST_TUNABLE        kPanMax = 80.0;         // sideways, either way - see note above
	LIFECAST_TUNABLE        kTiltMax = 35.0;        // degrees, either way

	// How far to the side the second subject stands, in the ground plane and
	// across the line of sight (Edit/Compare). It is a WORLD distance and not
	// an angle on purpose: two figures overlap on screen when the world gap
	// between them is smaller than they are wide, and that comparison does
	// not change with how far the camera is - moving back shrinks the gap and
	// the figures by the same factor. So separation is settled here, once,
	// and the camera only has to decide whether the pair FITS.
	//
	// 60 is a working value, from the boot 87 sweep: at 18 units the twin
	// came out on top of the player, at 28 she was still partly behind her
	// shoulder and hair. Both of those were measured with the camera where
	// the menu left it, which is why neither of them is a rule.
	LIFECAST_TUNABLE kPairApart = 60.0;

	// How much of the character view the pair should span, edge to edge of
	// the two heads. The rest is margin: a figure is wider than the point
	// that gets framed, and a pair that exactly fills the frame is a pair cut
	// at both borders - which is what the boot 87 capture shows at the right
	// edge.
	//
	// WORKING VALUE, and this one is retuned during in-game review:
	// it is the one number here that is pure taste. Nothing measures it.
	LIFECAST_TUNABLE kPairSpan = 0.5;

	// The ruler the single-figure framing rule measures the camera with: a
	// world distance across the line of sight, projected either side of the
	// subject and never occupied by anything. It buys one number - how many
	// fractions of the window one world unit is worth at the camera the menu
	// left - and nothing stands there.
	//
	// It is SMALL, and that is the whole point. The pair rule probes at
	// kPairApart, and the archived logs say what that costs: at the distance
	// the RaceSex camera actually opens at, 60 units to the side projects to
	// x 2.27 with the depth almost halved against the subject's (boots 93 to
	// 134, "[stage] pair on"). That point is nowhere near the subject, so the
	// straight-line model fitted through it is not a model of the subject's
	// neighbourhood. Five units either side is, and the probe is measured
	// SYMMETRICALLY about the aim so the first-order error cancels instead of
	// accumulating on one side.
	//
	// Not tunable, and deliberately not kPairApart: this is an instrument,
	// not a taste knob. kPairApart is retuned during in-game review,
	// and a ruler that changes length when a taste knob is turned would make
	// this solve depend on a number with nothing to do with it.
	inline constexpr double kProbeStep = 10.0;

	// Registers the bridge handlers and the session callbacks. Call once, at
	// kDataLoaded, after Session::Install. Safe to call twice.
	void Install();

	// The same requests as "stage-move" and "stage-reset", for code that is
	// not the page. Game thread; queued like the page's, answered to the page
	// like the page's.
	void Move(double a_turn, double a_distance, double a_lift, double a_pan, double a_tilt);
	void Reset();

	// THE FRAMING RULE: puts the player and a second subject standing
	// kPairApart to the side both inside the character view, and takes it
	// back when the comparison is turned off.
	//
	// It is not two numbers. For any opening it answers where the camera has
	// to be, and it answers it from a measurement taken at that opening
	// rather than from constants that happened to work in one room:
	//
	//   1  the engine's own camera is asked where two known world points
	//      land on screen - the player's aim point, and the same point one
	//      probe step to the side. That gives how many fractions of the
	//      window one world unit across the line of sight is worth at the
	//      distance the camera is at now, and where the middle of the view
	//      actually sits. Neither is assumed, and neither is a constant of
	//      this file: the RaceSex camera does not look straight at the
	//      player - she opens left of centre - and field of view, resolution
	//      and the player's own height all move those two numbers.
	//   2  with that, distance and pan are solved: distance so the pair
	//      spans kPairSpan of the character view, pan so the pair's middle
	//      lands on the view's middle. Zoom alone cannot do it, because the
	//      view is centred on the player and backing away from a centre that
	//      IS the player keeps her in the middle (D105).
	//   3  the solution is clamped by the same limits every other move is.
	//      A clamp is where the room wins: in a space too tight to back away
	//      in, distance stops at kMaxDistance and the pair comes out bigger
	//      than kPairSpan asked for. The log says what was wanted and what
	//      was given, and that difference is the measurement.
	//   4  once the slide has arrived, the two points are projected AGAIN and
	//      what was achieved is logged against what was asked. That is the
	//      only honest check: everything above is a model of a camera, and
	//      the model is not the camera.
	//
	// a_frameLeft and a_frameRight are the character view's own edges as a
	// fraction of the window width. The panel covers the left of the screen,
	// so the middle of the window is not the middle of what is visible.
	//
	// Game thread, queued to the UI thread like every other move here. Off
	// restores the view the menu opened with, which is what Reset does.
	void FramePair(bool a_on, double a_frameLeft, double a_frameRight);

	// THE FRAMING RULE FOR ONE FIGURE: puts the subject in the middle of the
	// CHARACTER VIEW - the window minus the panel - instead of the middle of
	// the window, and puts her there again whenever that view changes size.
	//
	// a_frameLeft and a_frameRight are the character view's own edges as a
	// fraction of the window width, measured by the page from the stage
	// element's own rect. The DLL is told rather than computing it, and that
	// is the decision: the view's width moves with the panel opening and
	// closing, with the panel scale the player sets, with the belt's narrow
	// form and with the resolution. Any constant here would be a number that
	// is right on one screen - and the one measurement that exists is on the
	// test screen (2560 CSS px), which is the single worst population to
	// generalise a layout constant from.
	//
	// It moves PAN and nothing else, and that is also a decision rather than
	// a simplification:
	//
	//   * V099 measured that backing away does not centre - the subject slides
	//     to the border instead - and D105 says the same for the pair: the
	//     view is centred on the player, so retreating from a centre that IS
	//     the player keeps her where she was. Distance is not a centring tool.
	//     It stays where the player's wheel left it.
	//   * Pan is a pure sideways translation of the camera, which is the one
	//     motion whose effect on screen x the projection model predicts
	//     exactly. The pair rule moves distance too, and its own audit shows
	//     what that costs: still 27% out after its one correction (boot 93 on).
	//   * The camera distance CANCELS out of the answer. With
	//       x = x0 + k * lateral / depth  and  k = sep * depth / step,
	//     the solved pan is  pan + (aim - centre) * step / sep  - no depth in
	//     it. So the one number V099 says the first reading of an opening gets
	//     wrong cannot bend what this computes. (It can still be WRITTEN; that
	//     is what the second reading of the frame is for - see Stage.cpp.)
	//
	// WHAT IT AIMS AT is the face, and it is read from a node rather than
	// computed: "NPCEyeBone", which the skeleton hangs under the head and
	// places on the head's midline, 8.1 units forward of the head bone and 3.7
	// up. That matters because what a capture measures is the face's own
	// bilateral-symmetry axis, and E4d measured the old aim -
	// GetLookingAtLocation() - to be the HEAD BONE itself, +24 to +76 px to the
	// side of that axis, the gap growing with the head's yaw. Falls back to the
	// head node and then to GetLookingAtLocation(), and says which answered.
	//
	// AND IT RE-AIMS, every kTrackMs, while it is on. This is not polish: with
	// the camera parked to the third decimal the head bone sweeps 190 to 230 px
	// of 2560 in 13 s, and the sweep is slow and not stationary - half of a
	// 13 s record has a mean up to 6.1% of the stage away from the other half,
	// and even a 10 s rolling mean moves 2.0 to 2.5% between neighbouring
	// windows, which is the entire tolerance the rule is judged by (Fase 7,
	// E4d, boot 137). So there is no short window to average over and no static
	// aim that can promise where the subject will be: a rule that aims once
	// centres the instant it happened to look at. Re-aiming moves the TARGET
	// only, so the easing is what travels, and the camera glides at the speed
	// the subject drifts - about 17 px a second on screen. Inside a dead band
	// of half the rule's own tolerance it does nothing at all.
	//
	// AND THE SOLVE READS THE CURVE instead of extrapolating the slope. Moving
	// the camera +d sideways and moving the aim -d sideways are the same
	// projection exactly, because pan does not rotate anything, so the whole of
	// x(pan) can be sampled by projecting points - no camera moved, no slide
	// waited for. E4c and E4d measured why the straight line is not enough:
	// perp is not perpendicular to the line of sight (the RaceSex camera does
	// not look at the player), so panning changes the aim's DEPTH and the scale
	// the rule reads falls by half between the measurement and the arrival. The
	// slope is right where it is measured; extrapolating it 40 units was wrong.
	//
	// Off restores the view the menu opened with, which is what Reset does,
	// and is also how the measurement's negative control is taken: the same
	// DLL, the same boot, the same room, one message apart.
	//
	// Stands down while the PAIR rule is framing - two rules aiming the same
	// camera is worse than either - and says so in the log.
	//
	// Game thread, queued to the UI thread like every other move here.
	void FrameOne(bool a_on, double a_frameLeft, double a_frameRight);

	// Where the second subject has to STAND for the rule above to be framing
	// her and not an empty patch of floor: the player's own position, moved
	// kPairApart across the line of sight in the ground plane, plus the shift
	// the player gave her (PairShift, below).
	//
	// It is published rather than duplicated on purpose. Edit/Compare places
	// her and this module frames her, and if each worked out the offset for
	// itself the two would agree until one of them was edited. One definition,
	// two callers.
	//
	// False when there is no kept frame yet - the offset is across the line of
	// sight, so without a camera direction there is no "to the side". In
	// practice the frame is captured the moment the panel comes on screen,
	// well before any button can ask for this; it is written once there and
	// cleared when the menu closes, which is why reading it from the game
	// thread is a read of two numbers that do not change during an opening.
	bool PairSpot(RE::NiPoint3& a_out);

	// Where the player has MOVED the second subject from that spot (E14), in
	// world units: `across` along the same screen right PairSpot uses, `depth`
	// along the line of sight, positive away from the camera. PairSpot and the
	// framing rule's aim both add it, so the two still agree - the reason the
	// spot is published at all. Edit/Compare writes it, from the game thread;
	// the rule reads it on the UI thread, so it is kept in two atomics.
	struct PairShift
	{
		double across = 0.0;
		double depth = 0.0;
	};

	// Adds a delta and cuts the total to its limits: `across` so that she
	// stands between half and three times kPairApart to the player's right
	// (never on top of her), `depth` between half kPairApart towards the
	// camera and twice it away (V107: pushed towards the camera, past a point
	// she disappears). Returns what is kept; a_cut says whether the cut took
	// anything off.
	PairShift ShiftPair(double a_across, double a_depth, bool& a_cut);
	PairShift PairShiftNow();
	void      ClearPairShift();

	// Where a world point stands from the player in the same two axes, read
	// from the camera that is drawing now: `across` to screen right, `depth`
	// away from the camera. What PairSpot put there reads kPairApart plus the
	// shift across, and the shift in depth. False without a kept frame.
	bool PairReads(const RE::NiPoint3& a_at, double& a_across, double& a_depth);

	// One step of the slide, when there is one under way. Game thread, and a
	// single atomic read when there is not - it is called from the input tick
	// the Switcher already runs, measured between 56.7 and 64.6 a second
	// whether the panel holds focus or not, rather than from a second tick
	// of its own. The step itself is queued to the UI thread like every
	// other move.
	void Tick();
}
