#include "i_interface.h"
#include "st_start.h"
#include "gamestate.h"
#include "startupinfo.h"
#include "c_cvars.h"
#include "gstrings.h"
#include "version.h"

static_assert(sizeof(void*) == 8,
	"Only LP64/LLP64 builds are officially supported. "
	"Please do not attempt to build for other platforms; "
	"even if the program succeeds in a MAP01 smoke test, "
	"there are e.g. known visual artifacts "
	"<https://forum.zdoom.org/viewtopic.php?f=7&t=75673> "
	"that lead to a bad user experience.");

// Some global engine variables taken out of the backend code.
FStartupScreen* StartWindow;
SystemCallbacks sysCallbacks;
FString endoomName;
bool batchrun;
float menuBlurAmount;

bool AppActive = true;
int chatmodeon;
gamestate_t 	gamestate = GS_STARTUP;
bool ToggleFullscreen;
int 			paused;
bool			pauseext;

FStartupInfo GameStartupInfo;

// @Cockatrice - Introduced for updating to Chapter 2, we need to disable mods because none of them will work
// This will have to be updated again for 1.0 (Chapter 3) to a different variable name so we can disable mods again
// Default to FALSE so the user has to confirm turning mods back on after launching the update
// Will disable ALL pwads at startup except specifically sanctioned ones
// Note: This will not disable wad loading from the autoload in the config file.  If a user was knowledgable enough to modify the config
// we will assume they can remove a few incompatible wads
CVAR(Bool, enablemods, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);


// @Cockatrice - Basic set-once cvar to determine if any mod files are specified in MODS subdirectory, command line
// or in workshop.txt
// Informational so we don't prompt users about mods if they don't actually have any installed
CVAR(Bool, modsexist, false, CVAR_NOSAVE | CVAR_NOSET | CVAR_CONFIG_ONLY);


CVAR(Bool, queryiwad, QUERYIWADDEFAULT, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(String, defaultiwad, "", CVAR_ARCHIVE | CVAR_GLOBALCONFIG);
CVAR(Bool, vid_fps, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

EXTERN_CVAR(Bool, ui_generic)

CUSTOM_CVAR(String, language, "auto", CVAR_ARCHIVE | CVAR_NOINITCALL | CVAR_GLOBALCONFIG)
{
	GStrings.UpdateLanguage(self);
	UpdateGenericUI(ui_generic);
	if (sysCallbacks.LanguageChanged) sysCallbacks.LanguageChanged(self);
}

