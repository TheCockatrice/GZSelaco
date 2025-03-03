/*
** texture.cpp
** The base texture class
**
**---------------------------------------------------------------------------
** Copyright 2004-2007 Randy Heit
** Copyright 2006-2018 Christoph Oelckers
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
**
*/

#include "bitmap.h"
#include "image.h"
#include "filesystem.h"
#include "files.h"
#include "cmdlib.h"
#include "palettecontainer.h"
#include "printf.h"
#include "files.h"

FMemArena ImageArena(32768);
TArray<FImageSource *>FImageSource::ImageForLump;
int FImageSource::NextID;
static PrecacheInfo precacheInfo;

struct PrecacheDataPaletted
{
	TArray<uint8_t> Pixels;
	int RefCount;
	int ImageID;
};

struct PrecacheDataRgba
{
	FBitmap Pixels;
	int TransInfo;
	int RefCount;
	int ImageID;
};

// TMap doesn't handle this kind of data well.  std::map neither. The linear search is still faster, even for a few 100 entries because it doesn't have to access the heap as often..
TArray<PrecacheDataPaletted> precacheDataPaletted;
TArray<PrecacheDataRgba> precacheDataRgba;

//===========================================================================
// 
// the default just returns an empty texture.
//
//===========================================================================

TArray<uint8_t> FImageSource::CreatePalettedPixels(int conversion)
{
	TArray<uint8_t> Pixels(Width * Height, true);
	memset(Pixels.Data(), 0, Width * Height);
	return Pixels;
}

PalettedPixels FImageSource::GetCachedPalettedPixels(int conversion)
{
	PalettedPixels ret;

	FString name;
	fileSystem.GetFileShortName(name, SourceLump);

	auto imageID = ImageID;

	// Do we have this image in the cache?
	unsigned index = conversion != normal? UINT_MAX : precacheDataPaletted.FindEx([=](PrecacheDataPaletted &entry) { return entry.ImageID == imageID; });
	if (index < precacheDataPaletted.Size())
	{
		auto cache = &precacheDataPaletted[index];

		if (cache->RefCount > 1)
		{
			ret.Pixels.Set(cache->Pixels.Data(), cache->Pixels.Size());
			cache->RefCount--;
		}
		else if (cache->Pixels.Size() > 0)
		{
			ret.PixelStore = std::move(cache->Pixels);
			ret.Pixels.Set(ret.PixelStore.Data(), ret.PixelStore.Size());
			precacheDataPaletted.Delete(index);
		}
		else
		{
			//Printf("something bad happened for %s, refcount = %d\n", name.GetChars(), cache->RefCount);
		}
	}
	else
	{
		// The image wasn't cached. Now there's two possibilities: 
		auto info = precacheInfo.CheckKey(ImageID);
		if (!info || info->second <= 1 || conversion != normal)
		{
			// This is either the only copy needed or some access outside the caching block. In these cases create a new one and directly return it.
			ret.PixelStore = CreatePalettedPixels(conversion);
			ret.Pixels.Set(ret.PixelStore.Data(), ret.PixelStore.Size());
		}
		else
		{
			// This is the first time it gets accessed and needs to be placed in the cache.
			PrecacheDataPaletted *pdp = &precacheDataPaletted[precacheDataPaletted.Reserve(1)];

			pdp->ImageID = imageID;
			pdp->RefCount = info->second - 1;
			info->second = 0;
			pdp->Pixels = CreatePalettedPixels(normal);
			ret.Pixels.Set(pdp->Pixels.Data(), pdp->Pixels.Size());
		}
	}
	return ret;
}

TArray<uint8_t> FImageSource::GetPalettedPixels(int conversion)
{
	auto pix = GetCachedPalettedPixels(conversion);
	if (pix.ownsPixels())
	{
		// return the pixel store of the returned data directly if this was the last reference.
		auto array = std::move(pix.PixelStore);
		return array;
	}
	else
	{
		// If there are pending references, make a copy.
		TArray<uint8_t> array(pix.Pixels.Size(), true);
		memcpy(array.Data(), pix.Pixels.Data(), array.Size());
		return array;
	}
}



//===========================================================================
//
// FImageSource::CopyPixels
//
// this is the generic case that can handle
// any properly implemented texture for software rendering.
// Its drawback is that it is limited to the base palette which is
// why all classes that handle different palettes should subclass this
// method
//
//===========================================================================

int FImageSource::CopyPixels(FBitmap *bmp, int conversion)
{
	if (conversion == luminance) conversion = normal;	// luminance images have no use as an RGB source.
	PalEntry *palette = GPalette.BaseColors;
	auto ppix = CreatePalettedPixels(conversion);
	bmp->CopyPixelData(0, 0, ppix.Data(), Width, Height, Height, 1, 0, palette, nullptr);
	return 0;
}

int FImageSource::CopyTranslatedPixels(FBitmap *bmp, const PalEntry *remap)
{
	auto ppix = CreatePalettedPixels(false);
	bmp->CopyPixelData(0, 0, ppix.Data(), Width, Height, Height, 1, 0, remap, nullptr);
	return 0;
}

// @Cockatrice: Thread safe(ish) version of CopyPixels. 
// Default does not do much
int FImageSource::ReadPixels(FImageLoadParams *params, FBitmap *bmp)
{
	/*if (conversion == luminance) conversion = normal;	// luminance images have no use as an RGB source.
	PalEntry *palette = GPalette.BaseColors;
	auto ppix = CreatePalettedPixels(conversion);
	bmp->CopyPixelData(0, 0, ppix.Data(), Width, Height, Height, 1, 0, palette, nullptr);*/

	return 0;
}

int FImageSource::ReadPixels(FileReader *reader, FBitmap *bmp, int conversion) {
	return 0;
}

// @Cockatrice: Thread safe(ish) version of CopyTranslatedPixels
int FImageSource::ReadTranslatedPixels(FileReader *reader, FBitmap *bmp, const PalEntry *remap, int conversion)
{
	// TODO: It
	return 0;
}

// @Cockatrice: This should only ever be used for textures that are in a GPU readable compressed format
int FImageSource::ReadCompressedPixels(FileReader* reader, unsigned char** data, size_t& size, size_t& unitSize, int& mipLevels) {
	FString lumpname = fileSystem.GetFileFullName(SourceLump);
	I_FatalError("FImageSource::ReadCompressedPixels() was called on an image source that does not support it! (%s)", lumpname);
	return 0;
}

// Call this on the main thread to prepare params for a background thread load
// convoluted I know, but some formats require more information than others
// Default version should work for some formats like PNG
FImageLoadParams *FImageSource::NewLoaderParams(int conversion, int translation, FRemapTable *remap) {
	FResourceLump *rLump = SourceLump >= 0 ? fileSystem.GetFileAt(SourceLump) : nullptr;
	FileReader *reader = rLump ? rLump->Owner->GetReader() : nullptr;

	if (!rLump) { return nullptr; }

	FImageLoadParams *il = new FImageLoadParams();
	il->reader = reader ? reader->CopyNew() : rLump->NewReader().CopyNew();
	il->conversion = conversion;
	il->translation = translation;
	il->remap = remap;

	if (il->reader) {
		il->reader->Seek(rLump->GetFileOffset(), FileReader::SeekSet);
	}

	return il;
}


bool FImageSource::SerializeForTextureDef(FILE* fp, FString& name, int useType, FGameTexture* gameTex) {
	const char* fullName = fileSystem.GetFileFullName(SourceLump, false);
	fprintf(fp, "%d:%s:%s:%d:%dx%d:%dx%d\n", 999, name.GetChars(), fullName != NULL ? fullName : "-", useType, Width, Height, LeftOffset, TopOffset);
	return true;
}

int FImageSource::DeSerializeFromTextureDef(FileReader &fr) {
	int fileType = 0, useType = 0;
	char id[9], path[1024];
	char str[1800];

	if (fr.Gets(str, 1800)) {
		int count = sscanf(str,
			"%d:%8[^:]:%1023[^:]:%d:%dx%d:%dx%d",
			&fileType, id, path, &useType, &Width, &Height, &LeftOffset, &TopOffset
		);

		return (int)(count == 8);
	}

	return 0;
}


//==========================================================================
//
//
//
//==========================================================================

FBitmap FImageSource::GetCachedBitmap(const PalEntry *remap, int conversion, int *ptrans)
{
	FBitmap ret;

	//FString name;
	int trans = -1;
	//fileSystem.GetFileShortName(name, SourceLump);

	auto imageID = ImageID;

	if (remap != nullptr)
	{
		// Remapped images are never run through the cache because they would complicate matters too much for very little gain.
		// Translated images are normally sprites which normally just consist of a single image and use no composition.
		// Additionally, since translation requires the base palette, the really time consuming stuff will never be subjected to it.
		ret.Create(Width, Height);
		trans = CopyTranslatedPixels(&ret, remap);
	}
	else
	{
		if (conversion == luminance) conversion = normal;	// luminance has no meaning for true color.
		// Do we have this image in the cache?
		unsigned index = conversion != normal? UINT_MAX : precacheDataRgba.FindEx([=](PrecacheDataRgba &entry) { return entry.ImageID == imageID; });
		if (index < precacheDataRgba.Size())
		{
			auto cache = &precacheDataRgba[index];

			trans = cache->TransInfo;
			if (cache->RefCount > 1)
			{
				//Printf("returning reference to %s, refcount = %d\n", name.GetChars(), cache->RefCount);
				ret.Copy(cache->Pixels, false);
				cache->RefCount--;
			}
			else if (cache->Pixels.GetPixels())
			{
				//Printf("returning contents of %s, refcount = %d\n", name.GetChars(), cache->RefCount);
				ret = std::move(cache->Pixels);
				precacheDataRgba.Delete(index);
			}
			else
			{
				// This should never happen if the function is implemented correctly
				//Printf("something bad happened for %s, refcount = %d\n", name.GetChars(), cache->RefCount);
				ret.Create(Width, Height);
				trans = CopyPixels(&ret, normal);
			}
		}
		else
		{
			// The image wasn't cached. Now there's two possibilities:
			auto info = precacheInfo.CheckKey(ImageID);
			if (!info || info->first <= 1 || conversion != normal)
			{
				// This is either the only copy needed or some access outside the caching block. In these cases create a new one and directly return it.
				//Printf("returning fresh copy of %s\n", name.GetChars());
				ret.Create(Width, Height);
				trans = CopyPixels(&ret, conversion);
			}
			else
			{
				//Printf("creating cached entry for %s, refcount = %d\n", name.GetChars(), info->first);
				// This is the first time it gets accessed and needs to be placed in the cache.
				PrecacheDataRgba *pdr = &precacheDataRgba[precacheDataRgba.Reserve(1)];

				pdr->ImageID = imageID;
				pdr->RefCount = info->first - 1;
				info->first = 0;
				pdr->Pixels.Create(Width, Height);
				trans = pdr->TransInfo = CopyPixels(&pdr->Pixels, normal);
				ret.Copy(pdr->Pixels, false);
			}
		}
	}
	if (ptrans) *ptrans = trans;
	return ret;
}

//==========================================================================
//
//
//
//==========================================================================

void FImageSource::CollectForPrecache(PrecacheInfo &info, bool requiretruecolor)
{
	auto val = info.CheckKey(ImageID);
	bool tc = requiretruecolor;
	if (val)
	{
		val->first += tc;
		val->second += !tc;
	}
	else
	{
		auto pair = std::make_pair(tc, !tc);
		info.Insert(ImageID, pair);
	}
}

void FImageSource::BeginPrecaching()
{
	precacheInfo.Clear();
}

void FImageSource::EndPrecaching()
{
	precacheDataPaletted.Clear();
	precacheDataRgba.Clear();
}

void FImageSource::RegisterForPrecache(FImageSource *img, bool requiretruecolor)
{
	img->CollectForPrecache(precacheInfo, requiretruecolor);
}

//==========================================================================
//
//
//
//==========================================================================

typedef FImageSource* (*CreateFunc)(FileReader & file, int lumpnum);
typedef FImageSource* (*MakeFunc)(FileReader& fr, int lumpnum, bool* hasExtraInfo);

struct TexCreateInfo
{
	CreateFunc TryCreate;
	bool checkflat;
};

FImageSource *IMGZImage_TryCreate(FileReader &, int lumpnum);
FImageSource *PNGImage_TryCreate(FileReader &, int lumpnum);
FImageSource *JPEGImage_TryCreate(FileReader &, int lumpnum);
FImageSource *DDSImage_TryCreate(FileReader &, int lumpnum);
FImageSource *PCXImage_TryCreate(FileReader &, int lumpnum);
FImageSource *TGAImage_TryCreate(FileReader &, int lumpnum);
FImageSource *StbImage_TryCreate(FileReader &, int lumpnum);
FImageSource *AnmImage_TryCreate(FileReader &, int lumpnum);
FImageSource *RawPageImage_TryCreate(FileReader &, int lumpnum);
FImageSource *FlatImage_TryCreate(FileReader &, int lumpnum);
FImageSource *PatchImage_TryCreate(FileReader &, int lumpnum);
FImageSource *EmptyImage_TryCreate(FileReader &, int lumpnum);
FImageSource *AutomapImage_TryCreate(FileReader &, int lumpnum);
FImageSource *StartupPageImage_TryCreate(FileReader &, int lumpnum);


FImageSource* PNGImage_TryMake(FileReader& fr, int lumpnum, bool* hasExtraInfo);
FImageSource* JPEGImage_TryMake(FileReader& fr, int lumpnum, bool* hasExtraInfo);
FImageSource* DDSImage_TryMake(FileReader& fr, int lumpnum, bool* hasExtraInfo);
//FImageSource* DDSImage_TryMake(const char* str, int lumpnum);
//FImageSource* PCXImage_TryMake(const char* str, int lumpnum);
//FImageSource* TGAImage_TryMake(const char* str, int lumpnum);
//FImageSource* FlatImage_TryMake(const char* str, int lumpnum);
//FImageSource* PatchImage_TryMake(const char* str, int lumpnum);
//FImageSource* EmptyImage_TryMake(const char* str, int lumpnum);
//FImageSource* StartupPageImage_TryMake(const char* str, int lumpnum);
//FImageSource* AutomapImage_TryMake(const char* str, int lumpnum);


// Examines the lump contents to decide what type of texture to create,
// and creates the texture.
FImageSource * FImageSource::GetImage(int lumpnum, bool isflat)
{
	static TexCreateInfo CreateInfo[] = {
		//{ IMGZImage_TryCreate,			false },
		{ PNGImage_TryCreate,			false },
		{ JPEGImage_TryCreate,			false },
		{ DDSImage_TryCreate,			false },
		//{ PCXImage_TryCreate,			false },
		//{ StbImage_TryCreate,			false },
		{ TGAImage_TryCreate,			false },
		//{ AnmImage_TryCreate,			false },
		{ StartupPageImage_TryCreate,	false },
		//{ RawPageImage_TryCreate,		false },
		{ FlatImage_TryCreate,			true },	// flat detection is not reliable, so only consider this for real flats.
		{ PatchImage_TryCreate,			false },
		{ EmptyImage_TryCreate,			false },
		{ AutomapImage_TryCreate,		false },
	};

	if (lumpnum == -1) return nullptr;

	unsigned size = ImageForLump.Size();
	if (size <= (unsigned)lumpnum)
	{
		// Hires textures can be added dynamically to the end of the lump array, so this must be checked each time.
		ImageForLump.Resize(lumpnum + 1);
		for (; size < ImageForLump.Size(); size++) ImageForLump[size] = nullptr;
	}
	// An image for this lump already exists. We do not need another one.
	if (ImageForLump[lumpnum] != nullptr) return ImageForLump[lumpnum];

	auto data = fileSystem.OpenFileReader(lumpnum);
	if (!data.isOpen()) 
		return nullptr;

	for (size_t i = 0; i < countof(CreateInfo); i++)
	{
		if (!CreateInfo[i].checkflat || isflat)
		{
			auto image = CreateInfo[i].TryCreate(data, lumpnum);
			if (image != nullptr)
			{
				ImageForLump[lumpnum] = image;
				return image;
			}
		}
	}
	return nullptr;
}


FImageSource* FImageSource::CreateImageFromDef(FileReader& fr, int filetype, int lumpnum, bool *hasExtraInfo)
{
	static MakeFunc MakeInfo[] = {
		//IMGZImage_TryCreate
		PNGImage_TryMake,
		JPEGImage_TryMake,
		DDSImage_TryMake,
		//DDSImage_TryMake,
		//PCXImage_TryMake,
		//StbImage_TryMake,
		//TGAImage_TryMake,
		//AnmImage_TryMake,
		//StartupPageImage_TryMake,
		//RawPageImage_TryMake,
		//FlatImage_TryMake,
		//PatchImage_TryMake,
		//EmptyImage_TryMake,
		//AutomapImage_TryMake
	};

	if (lumpnum == -1) 
		return nullptr;

	unsigned size = ImageForLump.Size();
	if (size <= (unsigned)lumpnum)
	{
		// Hires textures can be added dynamically to the end of the lump array, so this must be checked each time.
		ImageForLump.Resize(lumpnum + 1);
		for (; size < ImageForLump.Size(); size++) ImageForLump[size] = nullptr;
	}

	// An image for this lump already exists. We do not need another one.
	if (ImageForLump[lumpnum] != nullptr) {
		// Skip the next line
		char buf[1800];
		fr.Gets(buf, 1800);	// Ignore results
		return ImageForLump[lumpnum];
	}

	// Use the specified type to create an empty image of that type
	auto image = MakeInfo[filetype](fr, lumpnum, hasExtraInfo);

	if (image != nullptr) {
		ImageForLump[lumpnum] = image;
	}

	return image;
}