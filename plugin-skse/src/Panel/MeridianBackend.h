// MeridianBackend.h
// Lifecast
//
// The panel on Meridian UI: a platform browser (IUIPlatformAPI::AddOrGetBrowser),
// not the Meridian.View extension, because the platform browser is what
// carries the two-key focus chord (IBrowser::ToggleBrowserFocusByKeys), which
// Meridian evaluates before a focused browser can swallow a key - the one way
// back to the game that does not depend on the page itself working.
//
// The page is mod://Lifecast/index.html, served from Data/MeridianUI/Lifecast/.
// The handshake is Core/MeridianLink.h; this file only uses its platform
// pointer.

#pragma once

#include "Panel/Backend.h"

namespace Lifecast::Panel
{
	Backend& MeridianBackend();
}
