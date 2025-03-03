#pragma once

#include "resourcefile.h"

//==========================================================================
//
// Zip Lump
//
//==========================================================================


struct FDirectoryLump : public FResourceLump
{
	FileReader NewReader() override;
	int FillCache() override;
	long ReadData(FileReader &reader, char *buffer) override;

	FString mFullPath;
};


//==========================================================================
//
// Zip file
//
//==========================================================================

class FDirectory : public FResourceFile
{
	TArray<FDirectoryLump> Lumps;
	const bool nosubdir;

	int AddDirectory(const char *dirpath);
	void AddEntry(const char *fullpath, int size);

public:
	FDirectory(const char * dirname, bool nosubdirflag = false);
	bool Open(bool quiet, LumpFilterInfo* filter);
	virtual FResourceLump *GetLump(int no) { return ((unsigned)no < NumLumps) ? &Lumps[no] : NULL; }
};
