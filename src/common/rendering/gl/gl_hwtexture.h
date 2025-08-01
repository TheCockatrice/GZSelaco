#pragma once
class FBitmap;
class FTexture;

#include <mutex>
#include <atomic>

#include "tarray.h"
#include "hw_ihwtexture.h"


#ifdef LoadImage
#undef LoadImage
#endif

#define SHADED_TEXTURE -1
#define DIRECT_PALETTE -2

#include "tarray.h"
#include "gl_interface.h"
#include "hw_ihwtexture.h"

class FCanvasTexture;

namespace OpenGLRenderer
{

class FHardwareTexture : public IHardwareTexture
{
public:
	

	static unsigned int lastbound[MAX_TEXTURES];

	static int GetTexDimension(int value)
	{
		if (value > gl.max_texturesize) return gl.max_texturesize;
		return value;
	}

	static void InitGlobalState() { for (int i = 0; i < MAX_TEXTURES; i++) lastbound[i] = 0; }

private:
	struct GLLoadInfo {
		bool forcenofilter = false;
		unsigned int glTexID = 0;
		int glTextureBytes = 0;
		bool mipmapped = false;
	} glInfo; // Used to manage shared information between threads when loading

	bool forcenofilter;

	unsigned int glTexID = 0;
	unsigned int glDepthID = 0;	// only used by camera textures
	unsigned int glBufferID = 0;
	int glTextureBytes;
	bool mipmapped = false;

	HardwareState hwStates[MAX_TEXTURES] = { HardwareState::NONE };


	int GetDepthBuffer(int w, int h);

public:
	FHardwareTexture(int numchannels = 4, bool disablefilter = false)
	{
		forcenofilter = disablefilter;
		glTextureBytes = numchannels;
	}

	~FHardwareTexture();

	static void Unbind(int texunit);
	static void UnbindAll();

	void BindToFrameBuffer(int w, int h);

	unsigned int Bind(int texunit, bool needmipmap);
	bool BindOrCreate(FTexture* tex, int texunit, int clampmode, int translation, int flags);

	void AllocateBuffer(int w, int h, int texelsize);
	uint8_t* MapBuffer();

	unsigned int CreateTexture(unsigned char* buffer, int w, int h, int texunit, bool mipmap, const char* name);
	unsigned int BackgroundCreateTexture(GLsync* fence, unsigned char* buffer, int w, int h, int texunit, bool mipmap, bool indexed, const char* name, bool forceNoMips = false);
	unsigned int BackgroundCreateCompressedTexture(GLsync *fence, unsigned char* buffer, uint32_t dataSize, uint32_t totalSize, int w, int h, int format, int texunit, int numMips, const char* name, bool forceNoMips = false, bool allowQualityReduction = false);
	unsigned int CreateCompressedTexture(unsigned char* buffer, uint32_t dataSize, uint32_t totalSize, int w, int h, int format, int texunit, int numMips, const char* name, bool forceNoMips = false, bool allowQualityReduction = false);
	bool CreateCompressedMipmap(unsigned int glTexID, unsigned char* buffer, int mipLevel, int w, int h, int format, int32_t size, int texunit);
	unsigned int GetTextureHandle()
	{
		return glTexID;
	}

	int numChannels() { return glTextureBytes; }

	bool SwapToLoadedImage();
	void CreateInvalid(int texUnit);
	void DestroyLoadedImage();
	HardwareState GetState(int texUnit) override { return hwStates[texUnit]; }
	void SetHardwareState(HardwareState hws, int texUnit = 0) override { 
		hwStates[texUnit] = hws;
		if (texUnit == 0) hwState = hws;
	}
};

}
