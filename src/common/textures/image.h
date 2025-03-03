#pragma once

#include <stdint.h>
#include "tarray.h"
#include "bitmap.h"
#include "memarena.h"
#include "files.h"

class FImageSource;
using PrecacheInfo = TMap<int, std::pair<int, int>>;
extern FMemArena ImageArena;


// For bg loader
// TODO: Move to a different file
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <map>
#include "stats.h"
#include "TSQueue.h"
#include "palettecontainer.h"




// Doom patch format header
struct patch_t
{
	int16_t			width;			// bounding box size 
	int16_t			height;
	int16_t			leftoffset; 	// pixels to the left of origin 
	int16_t			topoffset;		// pixels below the origin 
	uint32_t 		columnofs[1];	// only [width] used
};

struct PalettedPixels
{
	friend class FImageSource;
	TArrayView<uint8_t> Pixels;
private:
	TArray<uint8_t> PixelStore;

	bool ownsPixels() const
	{
		return Pixels.Data() == PixelStore.Data();
	}
};

class ImageLoadThread;
class FGameTexture;

// @Cockatrice - Image sources must prepare the information they will need to load in the background thread
// in the main thread. These params or a subclass will be passed to the loader and then back to the image source
class FImageLoadParams {
public:
	FileReader *reader;
	int translation, conversion;
	FRemapTable *remap;

	virtual ~FImageLoadParams() {
		if (reader) delete reader;
	}
};


// This represents a naked image. It has no high level logic attached to it.
// All it can do is provide raw image data to its users.
class FImageSource
{
	friend class FBrightmapTexture;
	friend class ImageLoadThread;
	friend class VkTexLoadThread;	// TODO: Remove this, lazy

protected:

	static TArray<FImageSource *>ImageForLump;
	static int NextID;

	int SourceLump;
	int Width = 0, Height = 0;
	int LeftOffset = 0, TopOffset = 0;			// Offsets stored in the image.
	bool bUseGamePalette = false;				// true if this is an image without its own color set.
	int ImageID = -1;

	// Internal image creation functions. All external access should go through the cache interface,
	// so that all code can benefit from future improvements to that.

	virtual TArray<uint8_t> CreatePalettedPixels(int conversion);
	virtual int CopyPixels(FBitmap *bmp, int conversion);						// This will always ignore 'luminance'
	int CopyTranslatedPixels(FBitmap *bmp, const PalEntry *remap);


public:
	virtual bool SupportRemap0() { return false; }		// Unfortunate hackery that's needed for Hexen's skies. Only the image can know about the needed parameters
	virtual bool IsRawCompatible() { return true; }		// Same thing for mid texture compatibility handling. Can only be determined by looking at the composition data which is private to the image.
	virtual bool IsGPUOnly() { return false; }			// @Cockatrice - Image can only exist on the GPU, and CPU manipulation of this image will not be possible. Used for DDS Compressed Textures

	void CopySize(FImageSource &other)
	{
		Width = other.Width;
		Height = other.Height;
		LeftOffset = other.LeftOffset;
		TopOffset = other.TopOffset;
		SourceLump = other.SourceLump;
	}

	// Images are statically allocated and freed in bulk. None of the subclasses may hold any destructible data.
	void *operator new(size_t block) { return ImageArena.Alloc(block); }
	void operator delete(void *block) {}

	// @Cockatrice - Create params for a background load op
	virtual FImageLoadParams *NewLoaderParams(int conversion, int translation, FRemapTable *remap);
	virtual int ReadPixels(FImageLoadParams *params, FBitmap *bmp);									// Thread safe(ish) version
	virtual int ReadPixels(FileReader *reader, FBitmap *bmp, int conversion);						// Direct read pixels, must be implemented for things like multipatch to work properly
	virtual int ReadTranslatedPixels(FileReader *reader, FBitmap *bmp, const PalEntry *remap, int conversion);							// Thread safe(ish) version
	virtual int ReadCompressedPixels(FileReader* reader, unsigned char** data, size_t &size, size_t &unitSize, int &mipLevels);			// Thread safe, read data for the GPU and don't interpret it at all

	bool bMasked = true;						// Image (might) have holes (Assume true unless proven otherwise!)
	int8_t bTranslucent = -1;					// Image has pixels with a non-0/1 value. (-1 means the user needs to do a real check)

	int GetId() const { return ImageID; }

	// 'noremap0' will only be looked at by FPatchTexture and forwarded by FMultipatchTexture.

	// Either returns a reference to the cache, or a newly created item. The return of this has to be considered transient. If you need to store the result, use GetPalettedPixels
	PalettedPixels GetCachedPalettedPixels(int conversion);

	// tries to get a buffer from the cache. If not available, create a new one. If further references are pending, create a copy.
	TArray<uint8_t> GetPalettedPixels(int conversion);


	// Unlike for paletted images there is no variant here that returns a persistent bitmap, because all users have to process the returned image into another format.
	FBitmap GetCachedBitmap(const PalEntry *remap, int conversion, int *trans = nullptr);

	static void ClearImages() { ImageArena.FreeAll(); ImageForLump.Clear(); NextID = 0; }
	static FImageSource* GetImage(int lumpnum, bool checkflat);
	static FImageSource* CreateImageFromDef(FileReader& fr, int filetype, int lumpnum, bool* hasExtraInfo = nullptr);


	// Conversion option
	enum EType
	{
		normal = 0,
		luminance = 1,
		noremap0 = 2
	};

	FImageSource(int sourcelump = -1) : SourceLump(sourcelump) { ImageID = ++NextID; }
	virtual ~FImageSource() {}

	virtual bool SerializeForTextureDef(FILE* fp, FString& name, int useType, FGameTexture* gameTex);
	virtual int DeSerializeFromTextureDef(FileReader &fr);
	virtual bool DeSerializeExtraDataFromTextureDef(FileReader& fr, FGameTexture* gameTex) { return true; }

	int GetWidth() const
	{
		return Width;
	}

	int GetHeight() const
	{
		return Height;
	}

	std::pair<int, int> GetSize() const
	{
		return std::make_pair(Width, Height);
	}

	std::pair<int, int> GetOffsets() const
	{
		return std::make_pair(LeftOffset, TopOffset);
	}

	void SetOffsets(int x, int y)
	{
		LeftOffset = x;
		TopOffset = y;
	}

	int LumpNum() const
	{
		return SourceLump;
	}

	bool UseGamePalette() const
	{
		return bUseGamePalette;
	}

	virtual void CollectForPrecache(PrecacheInfo &info, bool requiretruecolor);
	static void BeginPrecaching();
	static void EndPrecaching();
	static void RegisterForPrecache(FImageSource *img, bool requiretruecolor);
};


//==========================================================================
//
// A texture defined in a Build TILESxxx.ART file
//
//==========================================================================
struct FRemapTable;

class FBuildTexture : public FImageSource
{
public:
	FBuildTexture(const FString& pathprefix, int tilenum, const uint8_t* pixels, FRemapTable* translation, int width, int height, int left, int top);
	TArray<uint8_t> CreatePalettedPixels(int conversion) override;
	int CopyPixels(FBitmap* bmp, int conversion) override;

protected:
	const uint8_t* RawPixels;
	FRemapTable* Translation;
};


class FTexture;

FTexture* CreateImageTexture(FImageSource* img) noexcept;