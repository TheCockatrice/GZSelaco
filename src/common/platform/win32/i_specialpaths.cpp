/*
** i_specialpaths.cpp
** Gets special system folders where data should be stored. (Windows version)
**
**---------------------------------------------------------------------------
** Copyright 2013-2016 Randy Heit
** Copyright 2016 Christoph Oelckers
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
*/

#include <windows.h>
#include <lmcons.h>
#include <shlobj.h>
#include <Shlwapi.h>
#include <VersionHelpers.h>

#include "i_specialpaths.h"
#include "printf.h"
#include "cmdlib.h"
#include "findfile.h"
#include "version.h"	// for GAMENAME
#include "gstrings.h"
#include "i_mainwindow.h"
#include "engineerrors.h"


bool UseKnownFolders()
{
	// Cache this value so the semantics don't change during a single run
	// of the program. (e.g. Somebody could add write access while the
	// program is running.)
	static int iswritable = -1;
	HANDLE file;

	if (iswritable >= 0)
	{
		return !iswritable;
	}
	// Consider 'Program Files' read only without actually checking.
	bool found = false;
	for (auto p : { L"ProgramFiles", L"ProgramFiles(x86)" })
	{
		wchar_t buffer1[256];
		if (GetEnvironmentVariable(p, buffer1, 256))
		{
			FString envpath(buffer1);
			FixPathSeperator(envpath);
			if (progdir.MakeLower().IndexOf(envpath.MakeLower()) == 0)
			{
				found = true;
				break;
			}
		}
	}

	if (!found)
	{
		std::wstring testpath = progdir.WideString() + L"writest";
		file = CreateFile(testpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			CloseHandle(file);
			if (!batchrun) Printf("Using program directory for storage\n");
			iswritable = true;
			return false;
		}
	}
	if (!batchrun) Printf("Using known folders for storage\n");
	iswritable = false;
	return true;
}


bool UseKnownFoldersIncludingProgramFiles()
{
	// Cache this value so the semantics don't change during a single run
	// of the program. (e.g. Somebody could add write access while the
	// program is running.)
	static int iswritable = -1;
	HANDLE file;

	if (iswritable >= 0)
	{
		return !iswritable;
	}

	std::wstring testpath = progdir.WideString() + L"writest";
	file = CreateFile(testpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if (file != INVALID_HANDLE_VALUE)
	{
		CloseHandle(file);
		Printf("Using program directory for savegame storage\n");
		iswritable = true;
		return false;
	}

	Printf("Using known folders for savegame storage\n");
	iswritable = false;
	return true;
}



static int isportable = -1;

//===========================================================================
//
// IsProgramDirectoryWritable
//
// If the program directory is writable, then dump everything in there for
// historical reasons. Otherwise, known folders get used instead.
//
//===========================================================================

bool IsPortable()
{
	// @Cockatrice - Commenting this out for now, since we need to be comaptible with older Selaco storage setups
	/*
	// Cache this value so the semantics don't change during a single run
	// of the program. (e.g. Somebody could add write access while the
	// program is running.)
	HANDLE file;

	if (isportable >= 0)
	{
		return !!isportable;
	}

	// Consider 'Program Files' read only without actually checking.
	bool found = false;
	for (auto p : { L"ProgramFiles", L"ProgramFiles(x86)" })
	{
		wchar_t buffer1[256];
		if (GetEnvironmentVariable(p, buffer1, 256))
		{
			FString envpath(buffer1);
			FixPathSeperator(envpath);
			if (progdir.MakeLower().IndexOf(envpath.MakeLower()) == 0)
			{
				isportable = false;
				return false;
			}
		}
	}

	// A portable INI means that this storage location should also be portable if the file can be written to.
	FStringf path("%s" GAMENAMELOWERCASE "_portable.ini", progdir.GetChars());
	if (FileExists(path))
	{
		file = CreateFile(path.WideString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (file != INVALID_HANDLE_VALUE)
		{
			CloseHandle(file);
			if (!batchrun) Printf("Using portable configuration\n");
			isportable = true;
			return true;
		}
	}

	isportable = false;
	return false;
	*/

	return !UseKnownFoldersIncludingProgramFiles();
}


//===========================================================================
//
// GetKnownFolder
//
// Returns the known_folder from SHGetKnownFolderPath
//
//===========================================================================

FString GetKnownFolder(int shell_folder, REFKNOWNFOLDERID known_folder, bool create, bool isFatal = true)
{
	PWSTR wpath;
	if (FAILED(SHGetKnownFolderPath(known_folder, create ? KF_FLAG_CREATE : 0, NULL, &wpath)))
	{
		// This should never be triggered unless the OS was compromised
		if (isFatal) {
			I_FatalError(TEXTCOLOR_YELLOW "Unable to retrieve or create known folder: {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX} Create: %d",
				known_folder.Data1, known_folder.Data2, known_folder.Data3,
				known_folder.Data4[0], known_folder.Data4[1], known_folder.Data4[2], known_folder.Data4[3],
				known_folder.Data4[4], known_folder.Data4[5], known_folder.Data4[6], known_folder.Data4[7],
				create);
		}
		else {
			Printf(TEXTCOLOR_YELLOW "Warning: Unable to retrieve or create known folder: {%08lX-%04hX-%04hX-%02hhX%02hhX-%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX} Create: %d",
				known_folder.Data1, known_folder.Data2, known_folder.Data3,
				known_folder.Data4[0], known_folder.Data4[1], known_folder.Data4[2], known_folder.Data4[3],
				known_folder.Data4[4], known_folder.Data4[5], known_folder.Data4[6], known_folder.Data4[7],
				create);
		}
		
		return "";
	}

	FString path = FString(wpath);
	FixPathSeperator(path);
	CoTaskMemFree(wpath);
	return path;
}

//===========================================================================
//
// M_GetAppDataPath													Windows
//
// Returns the path for the AppData folder.
//
//===========================================================================

FString M_GetAppDataPath(bool create)
{
	FString path = GetKnownFolder(CSIDL_LOCAL_APPDATA, FOLDERID_LocalAppData, create);

	path += "/" GAMENAMELOWERCASE;
	if (create)
	{
		CreatePath(path.GetChars());
	}
	return path;
}

//===========================================================================
//
// M_GetCachePath													Windows
//
// Returns the path for cache GL nodes.
//
//===========================================================================

FString M_GetCachePath(bool create)
{
	FString path = GetKnownFolder(CSIDL_LOCAL_APPDATA, FOLDERID_LocalAppData, create);

	// Don't use GAME_DIR and such so that ZDoom and its child ports can
	// share the node cache.
	path += "/zdoom/cache";
	if (create)
	{
		CreatePath(path.GetChars());
	}
	return path;
}

//===========================================================================
//
// M_GetAutoexecPath												Windows
//
// Returns the expected location of autoexec.cfg.
//
//===========================================================================

FString M_GetAutoexecPath()
{
	return "$PROGDIR/autoexec.cfg";
}

//===========================================================================
//
// M_GetOldConfigPath
//
// Check if we have a config in a place that's no longer used.
// 
//===========================================================================

FString M_GetOldConfigPath(int& type)
{
	FString path;
	HRESULT hr;

	// construct "$PROGDIR/-$USER.ini"
	WCHAR uname[UNLEN + 1];
	DWORD unamelen = UNLEN;

	path = progdir;
	hr = GetUserNameW(uname, &unamelen);
	if (SUCCEEDED(hr) && uname[0] != 0)
	{
		// Is it valid for a user name to have slashes?
		// Check for them and substitute just in case.
		auto probe = uname;
		while (*probe != 0)
		{
			if (*probe == '\\' || *probe == '/')
				*probe = '_';
			++probe;
		}
		path << GAMENAMELOWERCASE "-" << FString(uname) << ".ini";
		type = 0;
		if (FileExists(path))
			return path;
	}

	// Check in app data where this was previously stored.
	// We actually prefer to store the config in a more visible place so this is no longer used.
	path = GetKnownFolder(CSIDL_APPDATA, FOLDERID_RoamingAppData, true);
	path += "/" GAME_DIR "/" GAMENAMELOWERCASE ".ini";
	type = 1;
	if (FileExists(path))
		return path;

	return "";
}

//===========================================================================
//
// M_MigrateOldConfig
//
// Ask the user what to do with their old config.
// 
//===========================================================================

int M_MigrateOldConfig()
{
	int selection = IDCANCEL;
	auto globalstr = L"Move to Users/ folder";
	auto portablestr = L"Convert to portable installation";
	auto cancelstr = L"Cancel";
	auto titlestr = L"Migrate existing configuration";
	auto infostr = L"" GAMENAME " found a user specific config in the game folder";
	const TASKDIALOG_BUTTON buttons[] = { {IDYES, globalstr}, {IDNO, portablestr}, {IDCANCEL, cancelstr} };
	TASKDIALOGCONFIG taskDialogConfig = {};
	taskDialogConfig.cbSize = sizeof(TASKDIALOGCONFIG);
	taskDialogConfig.pszMainIcon = TD_WARNING_ICON;
	taskDialogConfig.pButtons = buttons;
	taskDialogConfig.cButtons = countof(buttons);
	taskDialogConfig.pszWindowTitle = titlestr;
	taskDialogConfig.pszContent = infostr;
	taskDialogConfig.hwndParent = mainwindow.GetHandle();
	taskDialogConfig.dwFlags = TDF_USE_COMMAND_LINKS;
	TaskDialogIndirect(&taskDialogConfig, &selection, NULL, NULL);
	if (selection == IDYES || selection == IDNO) return selection;
	throw CExitEvent(3);
}

//===========================================================================
//
// M_GetConfigPath													Windows
//
// Returns the path to the config file. On Windows, this can vary for reading
// vs writing. i.e. If the user specific ini does not exist, it will try
// to read from a neutral version, but never write to it.
//
//===========================================================================

FString M_GetConfigPath(bool for_reading)
{
	FString path;
	HRESULT hr;

	path.Format("%s" GAMENAMELOWERCASE "_portable.ini", progdir.GetChars());
	if (FileExists(path))
	{
		return path;
	}
	path = "";

	// Construct a user-specific config name
	if (UseKnownFolders())
	{
		path = GetKnownFolder(CSIDL_APPDATA, FOLDERID_RoamingAppData, true);
		path += "/" GAME_DIR;
		CreatePath(path.GetChars());
		path += "/" GAMENAMELOWERCASE ".ini";
	}
	else
	{ // construct "$PROGDIR/-$USER.ini"
		WCHAR uname[UNLEN + 1];
		DWORD unamelen = UNLEN;

		path = progdir;
		hr = GetUserNameW(uname, &unamelen);
		if (SUCCEEDED(hr) && uname[0] != 0)
		{
			// Is it valid for a user name to have slashes?
			// Check for them and substitute just in case.
			auto probe = uname;
			while (*probe != 0)
			{
				if (*probe == '\\' || *probe == '/')
					*probe = '_';
				++probe;
			}
			path << GAMENAMELOWERCASE "-" << FString(uname) << ".ini";
		}
		else
		{ // Couldn't get user name, so just use base version.
			path += GAMENAMELOWERCASE ".ini";
		}
	}

	// If we are reading the config file, check if it exists. If not, fallback
	// to base version.
	if (for_reading)
	{
		if (!FileExists(path))
		{
			path = progdir;
			path << GAMENAMELOWERCASE ".ini";
		}
	}

	return path;
}

//===========================================================================
//
// M_GetScreenshotsPath												Windows
//
// Returns the path to the default screenshots directory.
//
//===========================================================================

FString M_GetScreenshotsPath()
{
	FString path;

	if (IsPortable())
	{
		path << progdir << "Screenshots/";
	}
	else if (IsWindows8OrGreater())
	{
		path = GetKnownFolder(-1, FOLDERID_Screenshots, true);

		path << "/" GAMENAME "/";
	}
	else 
	{
		path = GetKnownFolder(CSIDL_MYPICTURES, FOLDERID_Pictures, true);
		path << "/Screenshots/" GAMENAME "/";
	}
	CreatePath(path.GetChars());
	return path;
}

//===========================================================================
//
// M_GetSavegamesPath												Windows
//
// Returns the path to the default save games directory.
//
//===========================================================================

FString M_GetSavegamesPath()
{
	FString path;

	if (IsPortable())
	{
		path << progdir << "Save/";
	}
	// Try standard Saved Games folder
	else
	{
		// On some Win11 systems, this call fails so use Documents as a backup
		path = GetKnownFolder(-1, FOLDERID_SavedGames, true, false);

		if (path.IsEmpty()) {
			// Try Documents folder
			path = GetKnownFolder(CSIDL_PERSONAL, FOLDERID_Documents, true, false);
			
			if (!path.IsEmpty()) {
				// Documents folder
				path << "/My Games/" GAMENAME "/";
			}
			else {
				// Last resort, use the AppData folder. This is probably because the user profile wasn't created properly
				// I'm looking at you Windows11
				path = GetKnownFolder(CSIDL_APPDATA, FOLDERID_RoamingAppData, true);
				path += "/" GAME_DIR "/Save/";
				CreatePath(path.GetChars());

				// In Selaco, Steam isn't setup to synchronize cloud saves from this folder yet, so output an error to the console if we pick this
				// Only warn once
				static bool hasWarned = false;
				if (!hasWarned) {
					hasWarned = Printf(TEXTCOLOR_ORANGE"Error: No standard save path could be located.\n" TEXTCOLOR_ORANGE "Your savegames may not be synchronized over Steam Cloud.\n" TEXTCOLOR_ORANGE "Savegames are currently being saved to : \n\t" TEXTCOLOR_BLUE "% s\n", path) > 0;
				}
			}
		}
		else {	
			// Saved Games folder
			path << "/" GAMENAME "/";
		}
	}
	return path;
}


//===========================================================================
//
// M_GetSavegamesPaths												Windows
//
// Returns all paths where savegames might be located
//
//===========================================================================

int M_GetSavegamesPaths(TArray<FString>& outputAr) {
	FString path;
	int cnt = 1;
	
	// Start with progdir
	path << progdir << "Save/";
	outputAr.Push(path);

	path = "";

	// Try Saved Games
	path = GetKnownFolder(-1, FOLDERID_SavedGames, false, false);
	if(!path.IsEmpty()) {
		path << "/" GAMENAME "/";
		outputAr.Push(path);
		cnt++;
	}
	path = "";

	// Try Documents/My Games/ Folder
	path = GetKnownFolder(CSIDL_PERSONAL, FOLDERID_Documents, false, false);
	if(!path.IsEmpty()) {
		path << "/My Games/" GAMENAME "/";
		outputAr.Push(path);
		cnt++;
	}

	// Try appdata/GAMENAMELOWERCASE/Save
	path = GetKnownFolder(CSIDL_APPDATA, FOLDERID_RoamingAppData, true);
	if (!path.IsEmpty()) {
		path += "/" GAME_DIR "/Save/";
		outputAr.Push(path);
		cnt++;
	}

	return cnt;
}

//===========================================================================
//
// M_GetDocumentsPath												Windows
//
// Returns the path to the default documents directory.
//
//===========================================================================

FString M_GetDocumentsPath()
{
	FString path;

	if (IsPortable())
	{
		return progdir;
	}
	// Try defacto My Documents/My Games folder
	else 
	{
		// I assume since this isn't a standard folder, it doesn't have a localized name either.
		path = GetKnownFolder(CSIDL_PERSONAL, FOLDERID_Documents, true);
		path << "/My Games/" GAMENAME "/";
		CreatePath(path.GetChars());
	}
	return path;
}

//===========================================================================
//
// M_GetDemoPath												Windows
//
// Returns the path to the default demp directory.
//
//===========================================================================

FString M_GetDemoPath()
{
	FString path;

	// A portable INI means that this storage location should also be portable.
	if (IsPortable())
	{
		path << progdir << "Demos/";
	}
	else
	// Try defacto My Documents/My Games folder
	{
		// I assume since this isn't a standard folder, it doesn't have a localized name either.
		path = GetKnownFolder(CSIDL_PERSONAL, FOLDERID_Documents, true);
		path << "/My Games/" GAMENAME "/";
	}

	return path;
}

//===========================================================================
//
// M_NormalizedPath
//
// Normalizes the given path and returns the result.
//
//===========================================================================

FString M_GetNormalizedPath(const char* path)
{
	std::wstring wpath = WideString(path);
	wchar_t buffer[MAX_PATH];
	GetFullPathNameW(wpath.c_str(), MAX_PATH, buffer, nullptr);
	FString result(buffer);
	FixPathSeperator(result);
	return result;
}
