# Native PVR points/default-lines validation

The test holds three 640x480 pages for eight seconds each. Page 1 covers
one-pixel point coverage and clipping, page 2 covers SDL's default POINTS line
method and endpoint ownership, and page 3 covers viewport-relative clipping and
ordered clears.

The behavioral reference is SDL's software renderer. In particular, an
independent zero-length line draws one pixel; an interior repeated segment is
skipped; a final repeated point may own and draw the endpoint once; and an
empty zero-length closing rectangle is not enlarged by the backend.

The console PASS result covers API/runtime completion only. All three pages
have also passed direct inspection in Flycast and on physical Dreamcast
hardware at 640x480 VGA. The hardware run returned zero; its photographed
pages match the Flycast reference for point coverage, line continuity,
endpoint ownership, viewport/clip/order behavior, and right/bottom guards.
Broader video modes and TA/OPB stress remain Phase 7 hardening.
