/*
 * ws-worker variant — browser board with Wippersnapper protocol wrappers.
 *
 * Inherits everything from browser (display, terminal, hardware modules).
 * Adds dual-registration of C modules under _prefixed names so frozen
 * Python wrappers can shadow them with protocol observability.
 *
 * import digitalio → loads frozen digitalio.py (protocol wrapper)
 *   → internally does: from _digitalio import * (C module)
 *   → adds WS protocol broadcasting on state changes
 *
 * The C common-hal code is identical to browser variant.
 */

// Start from browser variant (display, terminal, buses, hardware)
#include "../browser/mpconfigvariant.h"

// Enable module aliasing — module_aliases.c registers C modules
// under _prefixed names (_digitalio, _analogio, etc.)
#define CIRCUITPY_WS_PROTOCOL       (1)
