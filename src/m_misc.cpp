//-----------------------------------------------------------------------------
//
// Copyright 1993-1996 id Software
// Copyright 1999-2016 Randy Heit
// Copyright 2002-2016 Christoph Oelckers
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//		Default Config File.
//		Screenshots.
//
//-----------------------------------------------------------------------------


#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#include "r_defs.h"

#include "version.h"

#if defined(_WIN32)
#include <io.h>
#else
#endif


#include "m_swap.h"
#include "m_argv.h"

#include "filesystem.h"

#include "c_cvars.h"
#include "c_dispatch.h"
#include "c_bind.h"

#include "i_video.h"
#include "v_video.h"
#include "i_system.h"

// Data.
#include "m_misc.h"
#include "m_png.h"

#include "cmdlib.h"

#include "g_game.h"
#include "gi.h"

#include "gameconfigfile.h"
#include "gstrings.h"
#include "vm.h"

#include <fstream>
#include <filesystem>

FGameConfigFile *GameConfig;

CVAR(Bool, screenshot_quiet, false, CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
CVAR(String, screenshot_type, "png", CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
CVAR(String, screenshot_dir, "", CVAR_ARCHIVE|CVAR_GLOBALCONFIG);
EXTERN_CVAR(Bool, longsavemessages);
EXTERN_CVAR(Int, developer)

static size_t ParseCommandLine (const char *args, int *argc, char **argv);

TMap<FString, FString> globalStorage;
void M_LoadGlobalVars(const char* filename);
bool M_SaveGlobalVars(const char* filename);
void M_ReadGlobalVars(FileReader& fr, TMap<FString, FString>& map);


// Quick funcs for global vars
DEFINE_ACTION_FUNCTION(_Globals, Get)
{
	PARAM_PROLOGUE;
	PARAM_STRING(key);

	FString *val = globalStorage.CheckKey(key);

	if (numret > 0) ret[0].SetString(val != NULL ? *val : FString(""));
	if (numret > 1) ret[1].SetInt(val != NULL);

	return numret;
}


DEFINE_ACTION_FUNCTION(_Globals, GetInt)
{
	PARAM_PROLOGUE;
	PARAM_STRING(key);

	FString* val = globalStorage.CheckKey(key);

	if (numret > 0) ret[0].SetInt(val != NULL ? (int)val->ToLong() : 0);
	if (numret > 1) ret[1].SetInt(val != NULL);

	return numret;
}


DEFINE_ACTION_FUNCTION(_Globals, GetKeys)
{
	PARAM_PROLOGUE;
	PARAM_POINTER(out, TArray<FString>);

	// Copy keys to out
	TMapIterator<FString, FString> it(globalStorage);
	TMap<FString, FString>::Pair* pair;
	int outSize = out->Size();

	while (it.NextPair(pair)) {
		out->Push(pair->Key);
	}

	ACTION_RETURN_INT(out->Size() - outSize);
}


DEFINE_ACTION_FUNCTION(_Globals, Set)
{
	PARAM_PROLOGUE;
	PARAM_STRING(key);
	PARAM_STRING(value);

	if (key.Len() > 0) {
		if (value.Len() == 0) {
			globalStorage.Remove(key);
		}
		else {
			globalStorage[key] = value;
		}
	}

	return 0;
}



DEFINE_ACTION_FUNCTION(_Globals, SetInt)
{
	PARAM_PROLOGUE;
	PARAM_STRING(key);
	PARAM_INT(value);

	if (key.Len() > 0) {
		FString v;
		v.Format("%d", value);
		globalStorage[key] = v;
	}

	return 0;
}


DEFINE_ACTION_FUNCTION(_Globals, Save)
{
	// Save global vars from the same path as GameConfig
	if (GameConfig != nullptr && GameConfig->GetPathName() != nullptr) {
		FString path = M_GetSavegamesPath();
		path += "selaco.globals";
		M_SaveGlobalVars(path.GetChars());
	}
	else {
		Printf(TEXTCOLOR_RED"Failed to write globals!");
	}

	return 0;
}


UNSAFE_CCMD(writeglobals)
{
	FString path = M_GetSavegamesPath();
	path += "selaco.globals";
	if (M_SaveGlobalVars(path.GetChars())) {
		Printf(TEXTCOLOR_BLUE"Wrote globals to: %s", path.GetChars());
	}
	else {
		Printf(TEXTCOLOR_RED"Failed to write globals!");
	}
}



//---------------------------------------------------------------------------
//
// PROC M_FindResponseFile
//
//---------------------------------------------------------------------------

void M_FindResponseFile (void)
{
	const int limit = 100;	// avoid infinite recursion
	int added_stuff = 0;
	int i = 1;

	while (i < Args->NumArgs())
	{
		if (Args->GetArg(i)[0] != '@')
		{
			i++;
		}
		else
		{
			char	**argv;
			FileSys::FileData file;
			int		argc = 0;
			size_t	argsize = 0;
			int 	index;

			// Any more response files after the limit will be removed from the
			// command line.
			if (added_stuff < limit)
			{
				// READ THE RESPONSE FILE INTO MEMORY
				FileReader fr;
				if (!fr.OpenFile(Args->GetArg(i) + 1))
				{ // [RH] Make this a warning, not an error.
					Printf ("No such response file (%s)!\n", Args->GetArg(i) + 1);
				}
				else
				{
					Printf ("Found response file %s!\n", Args->GetArg(i) + 1);
					file = fr.ReadPadded(1);
					argsize = ParseCommandLine (file.string(), &argc, nullptr);
				}
			}
			else
			{
				Printf ("Ignored response file %s.\n", Args->GetArg(i) + 1);
			}

			if (argc != 0)
			{
				argv = (char **)M_Malloc (argc*sizeof(char *) + argsize);
				argv[0] = (char *)argv + argc*sizeof(char *);
				ParseCommandLine (file.string(), nullptr, argv);

				// Create a new argument vector
				FArgs *newargs = new FArgs;

				// Copy parameters before response file.
				for (index = 0; index < i; ++index)
					newargs->AppendArg(Args->GetArg(index));

				// Copy parameters from response file.
				for (index = 0; index < argc; ++index)
					newargs->AppendArg(argv[index]);

				// Copy parameters after response file.
				for (index = i + 1; index < Args->NumArgs(); ++index)
					newargs->AppendArg(Args->GetArg(index));

				// Use the new argument vector as the global Args object.
				delete Args;
				Args = newargs;
				if (++added_stuff == limit)
				{
					Printf("Response file limit of %d hit.\n", limit);
				}
			}
			else
			{
				// Remove the response file from the Args object
				Args->RemoveArg(i);
			}
		}
	}
	if (added_stuff > 0)
	{
		// DISPLAY ARGS
		Printf ("Added %d response file%s, now have %d command-line args:\n",
			added_stuff, added_stuff > 1 ? "s" : "", Args->NumArgs ());
		for (int k = 1; k < Args->NumArgs (); k++)
			Printf ("%s\n", Args->GetArg (k));
	}
}

// ParseCommandLine
//
// This is just like the version in c_dispatch.cpp, except it does not
// do cvar expansion.

static size_t ParseCommandLine (const char *args, int *argc, char **argv)
{
	int count;
	char* buffstart;
	char *buffplace;

	count = 0;
	buffstart = NULL;
	if (argv != NULL)
	{
		buffstart = argv[0];
	}
	buffplace = buffstart;

	for (;;)
	{
		while (*args <= ' ' && *args)
		{ // skip white space
			args++;
		}
		if (*args == 0)
		{
			break;
		}
		else if (*args == '\"')
		{ // read quoted string
			char stuff;
			if (argv != NULL)
			{
				argv[count] = buffplace;
			}
			count++;
			args++;
			do
			{
				stuff = *args++;
				if (stuff == '\\' && *args == '\"')
				{
					stuff = '\"', args++;
				}
				else if (stuff == '\"')
				{
					stuff = 0;
				}
				else if (stuff == 0)
				{
					args--;
				}
				if (argv != NULL)
				{
					*buffplace = stuff;
				}
				buffplace++;
			} while (stuff);
		}
		else
		{ // read unquoted string
			const char *start = args++, *end;

			while (*args && *args > ' ' && *args != '\"')
				args++;
			end = args;
			if (argv != NULL)
			{
				argv[count] = buffplace;
				while (start < end)
					*buffplace++ = *start++;
				*buffplace++ = 0;
			}
			else
			{
				buffplace += end - start + 1;
			}
			count++;
		}
	}
	if (argc != NULL)
	{
		*argc = count;
	}
	return (buffplace - buffstart);
}


//
// M_SaveDefaults
//

bool M_SaveDefaults (const char *filename)
{
	FString oldpath;
	bool success;

	if (GameConfig == nullptr) return true;
	if (filename != nullptr)
	{
		oldpath = GameConfig->GetPathName();
		GameConfig->ChangePathName (filename);
	}
	GameConfig->ArchiveGlobalData ();
	if (gameinfo.ConfigName.IsNotEmpty())
	{
		GameConfig->ArchiveGameData (gameinfo.ConfigName.GetChars());
	}
	success = GameConfig->WriteConfigFile ();
	if (filename != nullptr)
	{
		GameConfig->ChangePathName (filename);
	}

	// Save global vars from the same path as GameConfig
	if (success) {
		FString path = M_GetSavegamesPath();
		path += "selaco.globals";
		M_SaveGlobalVars(path.GetChars());
	}

	return success;
}

void M_SaveDefaultsFinal ()
{
	if (GameConfig == nullptr) return;
	while (!M_SaveDefaults (nullptr) && I_WriteIniFailed (GameConfig->GetPathName()))
	{
		/* Loop until the config saves or I_WriteIniFailed() returns false */
	}
	delete GameConfig;
	GameConfig = nullptr;
}

UNSAFE_CCMD (writeini)
{
	const char *filename = (argv.argc() == 1) ? NULL : argv[1];
	if (!M_SaveDefaults (filename))
	{
		Printf ("Writing config failed: %s\n", strerror(errno));
	}
	else
	{
		Printf ("Config saved.\n");
	}
}

// @Cockatrice - I lazily implemented the first version of the globals file
// that just copied your .ini path. This doesn't sync between Steam installs
// so we are going to migrate to something in the Save folder.
// This function will read the globals file, merge the important fields
// and then delete the file.
void M_MigrateGlobalVars(const char* filename, const char *newFilename) {
	TMap<FString, FString> map;

	FileReader fr;
	fr.OpenFile(filename);
	M_ReadGlobalVars(fr, globalStorage);
	fr.Close();

	// Merge data by adopting the largest number for each value
	// Legacy data was all INTs so we can just convert everything to INT
	TMapIterator<FString, FString> it(map);
	TMap<FString, FString>::Pair* pair;

	while (it.NextPair(pair))
	{
		auto data = pair->Value.ToLong();

		if (globalStorage.CheckKey(pair->Key)) {
			FString sData;
			sData.Format("%d", max(data, globalStorage[pair->Key].ToLong()));
			globalStorage[pair->Key] = sData;
		}
		else {
			globalStorage[pair->Key] = pair->Value;
		}
	}

	if (FileExists(filename)) {
		// Don't delete this file unless we can save the new one
		if (M_SaveGlobalVars(newFilename)) {
			std::filesystem::remove(filename);
			Printf("Migrated %s\n", filename);
		}
	}
}


CCMD(openconfig)
{
	M_SaveDefaults(nullptr);
	I_OpenShellFolder(ExtractFilePath(GameConfig->GetPathName()).GetChars());
}

// M_LoadGlobalVars
// @Cockatrice - Simple as fu, load global vars in a silly binary format that is somewhat hard to edit
// These variables are only used in ZScript for storing game-wide information outside of a save file and outside of CVars
void M_LoadGlobalVars(const char* filename) {
	globalStorage.Clear();

	Printf("Loading Globals...\n");
	FileReader fr;
	fr.OpenFile(filename);
	M_ReadGlobalVars(fr, globalStorage);
	fr.Close();
}


void M_ReadGlobalVars(FileReader& fr, TMap<FString, FString>& map) {
	const auto fileSize = fr.isOpen() ? fr.GetLength() : 0;

	if (!fr.isOpen() || fileSize < sizeof(uint32_t))
		return;

	const uint32_t numEntries = fr.ReadUInt32();
	const char hash = (char)(numEntries % 256);
	if (numEntries == 0) {
		fr.Close();
		return;
	}

	for (uint32_t x = 0; x < numEntries && fr.isOpen() && fr.Tell() < fileSize; x++) {
		char buf[256];
		char bufLen = 0;
		FString key, value;

		// Length of key
		const unsigned char keyLen = fr.ReadUInt8();

		// Key
		for (int k = 0; k < keyLen && k < 255 && fr.Tell() < fileSize; k++) {
			char tpos = (char)(fr.Tell() % 256);
			char c = fr.ReadInt8() - hash - tpos;
			buf[bufLen++] = c;
		}
		buf[bufLen] = '\0';
		key = buf;

		// Length of value
		const unsigned char valLen = fr.ReadUInt8();

		// Value
		bufLen = 0;
		for (int k = 0; k < valLen && k < 255 && fr.Tell() < fileSize; k++) {
			char tpos = (char)(fr.Tell() % 256);
			char c = fr.ReadInt8() - hash - tpos;
			buf[bufLen++] = c;
		}
		buf[bufLen] = '\0';
		value = buf;

		if (!key.Len() == 0 && !value.Len() == 0) {
			map[key] = value;
			if(developer) Printf("Globals Read: %s = %s\n", key.GetChars(), value.GetChars());
		}
	}
}


bool M_SaveGlobalVars(const char* filename) {
	std::ofstream fw(filename, std::ios_base::binary | std::ios_base::out);
	
	if (!fw.is_open()) return false;

	const uint32_t numEntries = globalStorage.CountUsed();
	const char hash = (char)(numEntries % 256);
	uint32_t ne = LittleLong(numEntries);
	fw.write(reinterpret_cast<char*>(&ne), sizeof(ne));
	
	if (numEntries == 0) {
		fw.close();
		return true;
	}

	TMapIterator<FString, FString> it(globalStorage);
	TMap<FString, FString>::Pair* pair;

	while (it.NextPair(pair))
	{
		unsigned char keyLen = (unsigned char)clamp(pair->Key.Len(), (size_t)0, (size_t)255);
		unsigned char valLen = (unsigned char)clamp(pair->Value.Len(), (size_t)0, (size_t)255);

		fw.write(reinterpret_cast<char*>(&keyLen), sizeof(keyLen));
		for (char x = 0; x < keyLen; x++) {
			char tpos = (char)(fw.tellp() % 256);
			char c = pair->Key[x] + hash + tpos;
			fw.write(&c, sizeof(c));
		}

		fw.write(reinterpret_cast<char*>(&valLen), sizeof(valLen));
		for (char x = 0; x < valLen; x++) {
			char tpos = (char)(fw.tellp() % 256);
			char c = pair->Value[x] + hash + tpos;
			fw.write(&c, sizeof(c));
		}
	}

	fw.close();

	return true;
}


//
// M_LoadDefaults
//

void M_LoadDefaults ()
{
	GameConfig = new FGameConfigFile;
	GameConfig->DoGlobalSetup ();

	FString path = M_GetSavegamesPath();
	path += "selaco.globals";
	M_LoadGlobalVars(path.GetChars());

	// Migrate old global vars from the same path as GameConfig
	if (GameConfig->GetPathName() != nullptr) {
		FString iniName = GameConfig->GetPathName();
		FString iniGlobals = iniName + ".globals";

		M_MigrateGlobalVars(iniGlobals.GetChars(), path.GetChars());

		// Attempt to migrate any .globals files in the same folder, and in the program folder
		std::filesystem::path iniFilePath(iniName.GetChars());
		std::filesystem::path iniPath = iniFilePath.parent_path();

		if (!iniPath.empty() && std::filesystem::exists(iniPath)) {
			for (auto const& entry : std::filesystem::directory_iterator{ iniPath }) {
				if (!entry.is_directory() && entry.path().extension() == ".globals") {
					M_MigrateGlobalVars(entry.path().string().c_str(), path.GetChars());
				}
			}
		}

		// Attempt to migrate .globals file in the program directory
		std::filesystem::path localPath(".");

		if (!localPath.empty() && std::filesystem::exists(localPath)) {
			for (auto const& entry : std::filesystem::directory_iterator{ localPath }) {
				if (!entry.is_directory() && entry.path().extension() == ".globals") {
					M_MigrateGlobalVars(entry.path().string().c_str(), path.GetChars());
				}
			}
		}
	}
}


//
// SCREEN SHOTS
//


struct pcx_t
{
	int8_t				manufacturer;
	int8_t				version;
	int8_t				encoding;
	int8_t				bits_per_pixel;

	uint16_t			xmin;
	uint16_t			ymin;
	uint16_t			xmax;
	uint16_t			ymax;
	
	uint16_t			hdpi;
	uint16_t			vdpi;

	uint8_t				palette[48];
	
	int8_t				reserved;
	int8_t				color_planes;
	uint16_t			bytes_per_line;
	uint16_t			palette_type;
	
	int8_t				filler[58];
};


inline void putc(unsigned char chr, FileWriter *file)
{
	file->Write(&chr, 1);
}

//
// WritePCXfile
//
void WritePCXfile (FileWriter *file, const uint8_t *buffer, const PalEntry *palette,
				   ESSType color_type, int width, int height, int pitch)
{
	TArray<uint8_t> temprow_storage(width * 3, true);
	uint8_t *temprow = &temprow_storage[0];
	const uint8_t *data;
	int x, y;
	int runlen;
	int bytes_per_row_minus_one;
	uint8_t color;
	pcx_t pcx;

	pcx.manufacturer = 10;				// PCX id
	pcx.version = 5;					// 256 (or more) colors
	pcx.encoding = 1;
	pcx.bits_per_pixel = 8;				// 256 (or more) colors
	pcx.xmin = 0;
	pcx.ymin = 0;
	pcx.xmax = LittleShort((unsigned short)(width-1));
	pcx.ymax = LittleShort((unsigned short)(height-1));
	pcx.hdpi = LittleShort((unsigned short)75);
	pcx.vdpi = LittleShort((unsigned short)75);
	memset (pcx.palette, 0, sizeof(pcx.palette));
	pcx.reserved = 0;
	pcx.color_planes = (color_type == SS_PAL) ? 1 : 3;	// chunky image
	pcx.bytes_per_line = width + (width & 1);
	pcx.palette_type = 1;				// not a grey scale
	memset (pcx.filler, 0, sizeof(pcx.filler));

	file->Write(&pcx, 128);

	bytes_per_row_minus_one = ((color_type == SS_PAL) ? width : width * 3) - 1;

	// pack the image
	for (y = height; y > 0; y--)
	{
		switch (color_type)
		{
		case SS_PAL:
			data = buffer;
			break;

		case SS_RGB:
			// Unpack RGB into separate planes.
			for (int i = 0; i < width; ++i)
			{
				temprow[i            ] = buffer[i*3];
				temprow[i + width    ] = buffer[i*3 + 1];
				temprow[i + width * 2] = buffer[i*3 + 2];
			}
			data = temprow;
			break;

		case SS_BGRA:
			// Unpack RGB into separate planes, discarding A.
			for (int i = 0; i < width; ++i)
			{
				temprow[i            ] = buffer[i*4 + 2];
				temprow[i + width    ] = buffer[i*4 + 1];
				temprow[i + width * 2] = buffer[i*4];
			}
			data = temprow;
			break;

		default:
			// Should never happen.
			return;
		}
		buffer += pitch;

		color = *data++;
		runlen = 1;

		for (x = bytes_per_row_minus_one; x > 0; x--)
		{
			if (*data == color)
			{
				runlen++;
			}
			else
			{
				if (runlen > 1 || color >= 0xc0)
				{
					while (runlen > 63)
					{
						putc (0xff, file);
						putc (color, file);
						runlen -= 63;
					}
					if (runlen > 0)
					{
						putc (0xc0 + runlen, file);
					}
				}
				if (runlen > 0)
				{
					putc (color, file);
				}
				runlen = 1;
				color = *data;
			}
			data++;
		}

		if (runlen > 1 || color >= 0xc0)
		{
			while (runlen > 63)
			{
				putc (0xff, file);
				putc (color, file);
				runlen -= 63;
			}
			if (runlen > 0)
			{
				putc (0xc0 + runlen, file);
			}
		}
		if (runlen > 0)
		{
			putc (color, file);
		}

		if (width & 1)
			putc (0, file);
	}

	// write the palette
	if (color_type == SS_PAL)
	{
		putc (12, file);		// palette ID byte
		for (x = 0; x < 256; x++, palette++)
		{
			putc (palette->r, file);
			putc (palette->g, file);
			putc (palette->b, file);
		}
	}
}

//
// WritePNGfile
//
void WritePNGfile (FileWriter *file, const uint8_t *buffer, const PalEntry *palette,
				   ESSType color_type, int width, int height, int pitch, float gamma)
{
	char software[100];
	mysnprintf(software, countof(software), GAMENAME " %s", GetVersionString());
	if (!M_CreatePNG (file, buffer, palette, color_type, width, height, pitch, gamma) ||
		!M_AppendPNGText (file, "Software", software) ||
		!M_FinishPNG (file))
	{
		Printf ("%s\n", GStrings.GetString("TXT_SCREENSHOTERR"));
	}
}


//
// M_ScreenShot
//
static bool FindFreeName (FString &fullname, const char *extension)
{
	FString lbmname;
	int i;

	for (i = 0; i <= 9999; i++)
	{
		const char *gamename = gameinfo.ConfigName.GetChars();

		time_t now;
		tm *tm;

		time(&now);
		tm = localtime(&now);

		if (tm == NULL)
		{
			lbmname.Format ("%sScreenshot_%s_%04d.%s", fullname.GetChars(), gamename, i, extension);
		}
		else if (i == 0)
		{
			lbmname.Format ("%sScreenshot_%s_%04d%02d%02d_%02d%02d%02d.%s", fullname.GetChars(), gamename,
				tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec,
				extension);
		}
		else
		{
			lbmname.Format ("%sScreenshot_%s_%04d%02d%02d_%02d%02d%02d_%02d.%s", fullname.GetChars(), gamename,
				tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
				tm->tm_hour, tm->tm_min, tm->tm_sec,
				i, extension);
		}

		if (!FileExists (lbmname.GetChars()))
		{
			fullname = lbmname;
			return true;		// file doesn't exist
		}
	}
	return false;
}

void M_ScreenShot (const char *filename)
{
	FileWriter *file;
	FString autoname;
	bool writepcx = (stricmp (screenshot_type, "pcx") == 0);	// PNG is the default

	// find a file name to save it to
	if (filename == NULL || filename[0] == '\0')
	{
		size_t dirlen;
		autoname = Args->CheckValue("-shotdir");
		if (autoname.IsEmpty())
		{
			autoname = screenshot_dir;
		}
		dirlen = autoname.Len();
		if (dirlen == 0)
		{
			autoname = M_GetScreenshotsPath();
			dirlen = autoname.Len();
		}
		if (dirlen > 0)
		{
			if (autoname[dirlen-1] != '/' && autoname[dirlen-1] != '\\')
			{
				autoname += '/';
			}
		}
		autoname = NicePath(autoname.GetChars());
		CreatePath(autoname.GetChars());
		if (!FindFreeName (autoname, writepcx ? "pcx" : "png"))
		{
			Printf ("M_ScreenShot: Delete some screenshots\n");
			return;
		}
	}
	else
	{
		autoname = filename;
		DefaultExtension (autoname, writepcx ? ".pcx" : ".png");
	}

	// save the screenshot
	int pitch;
	ESSType color_type;
	float gamma;

	auto buffer = screen->GetScreenshotBuffer(pitch, color_type, gamma);
	if (buffer.Size() > 0)
	{
		file = FileWriter::Open(autoname.GetChars());
		if (file == NULL)
		{
			Printf ("Could not open %s\n", autoname.GetChars());
			return;
		}
		if (writepcx)
		{
			WritePCXfile(file, buffer.Data(), nullptr, color_type,
				screen->GetWidth(), screen->GetHeight(), pitch);
		}
		else
		{
			WritePNGfile(file, buffer.Data(), nullptr, color_type,
				screen->GetWidth(), screen->GetHeight(), pitch, gamma);
		}
		delete file;

		if (!screenshot_quiet)
		{
			ptrdiff_t slash = -1;
			if (!longsavemessages) slash = autoname.LastIndexOfAny(":/\\");
			Printf ("Captured %s\n", autoname.GetChars()+slash+1);
		}
	}
	else
	{
		if (!screenshot_quiet)
		{
			Printf ("Could not create screenshot.\n");
		}
	}
}

UNSAFE_CCMD (screenshot)
{
	if (argv.argc() == 1)
		G_ScreenShot (NULL);
	else
		G_ScreenShot (argv[1]);
}

CCMD(openscreenshots)
{
	size_t dirlen;
	FString autoname;
	autoname = Args->CheckValue("-shotdir");
	if (autoname.IsEmpty())
	{
		autoname = screenshot_dir;
	}
	dirlen = autoname.Len();
	if (dirlen == 0)
	{
		autoname = M_GetScreenshotsPath();
		dirlen = autoname.Len();
	}
	if (dirlen > 0)
	{
		if (autoname[dirlen-1] != '/' && autoname[dirlen-1] != '\\')
		{
			autoname += '/';
		}
	}
	autoname = NicePath(autoname.GetChars());

	CreatePath(autoname.GetChars());

	I_OpenShellFolder(autoname.GetChars());
}

