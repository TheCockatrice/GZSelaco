/*
** ddstexture.cpp
** Texture class for DDS images
**
**---------------------------------------------------------------------------
** Copyright 2006-2016 Randy Heit
** Copyright 2006-2019 Christoph Oelckers
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
** DDS is short for "DirectDraw Surface" and is essentially that. It's
** interesting to us because it is a standard file format for DXTC/S3TC
** encoded images. Look up "DDS File Reference" in the DirectX SDK or
** the online MSDN documentation to the specs for this file format. Look up
** "Compressed Texture Resources" for information about DXTC encoding.
**
** Perhaps the most important part of DXTC to realize is that every 4x4
** pixel block can only have four different colors, and only two of those
** are discrete. So depending on the texture, there may be very noticable
** quality degradation, or it may look virtually indistinguishable from
** the uncompressed texture.
**
** Note: Although this class supports reading RGB textures from a DDS,
** DO NOT use DDS images with plain RGB data. PNG does everything useful
** better. Since DDS lets the R, G, B, and A components lie anywhere in
** the pixel data, it is fairly inefficient to process.
*/

#include "files.h"
#include "filesystem.h"
#include "bitmap.h"
#include "imagehelpers.h"
#include "image.h"
#include "m_swap.h"
#include "engineerrors.h"
#include "texturemanager.h"
#include "printf.h"

// Since we want this to compile under Linux too, we need to define this
// stuff ourselves instead of including a DirectX header.

enum
{
	ID_DDS = MAKE_ID('D', 'D', 'S', ' '),
	ID_DXT1 = MAKE_ID('D', 'X', 'T', '1'),
	ID_DXT2 = MAKE_ID('D', 'X', 'T', '2'),
	ID_DXT3 = MAKE_ID('D', 'X', 'T', '3'),
	ID_DXT4 = MAKE_ID('D', 'X', 'T', '4'),
	ID_DXT5 = MAKE_ID('D', 'X', 'T', '5'),
	ID_DX10 = MAKE_ID('D', 'X', '1', '0'),

	// Bits in dwFlags
	DDSD_CAPS = 0x00000001,
	DDSD_HEIGHT = 0x00000002,
	DDSD_WIDTH = 0x00000004,
	DDSD_PITCH = 0x00000008,
	DDSD_PIXELFORMAT = 0x00001000,
	DDSD_MIPMAPCOUNT = 0x00020000,
	DDSD_LINEARSIZE = 0x00080000,
	DDSD_DEPTH = 0x00800000,

	// Bits in ddpfPixelFormat
	DDPF_ALPHAPIXELS = 0x00000001,
	DDPF_FOURCC = 0x00000004,
	DDPF_RGB = 0x00000040,

	// Bits in DDSCAPS2.dwCaps1
	DDSCAPS_COMPLEX = 0x00000008,
	DDSCAPS_TEXTURE = 0x00001000,
	DDSCAPS_MIPMAP = 0x00400000,

	// Bits in DDSCAPS2.dwCaps2
	DDSCAPS2_CUBEMAP = 0x00000200,
	DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400,
	DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800,
	DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000,
	DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000,
	DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000,
	DDSCAPS2_CUBEMAP_NEGATIZEZ = 0x00008000,
	DDSCAPS2_VOLUME = 0x00200000,
};

//==========================================================================
//
//
//
//==========================================================================

enum D3D10_RESOURCE_DIMENSION_E {
	D3D10_RESOURCE_DIMENSION_UNKNOWN = 0,
	D3D10_RESOURCE_DIMENSION_BUFFER = 1,
	D3D10_RESOURCE_DIMENSION_TEXTURE1D = 2,
	D3D10_RESOURCE_DIMENSION_TEXTURE2D = 3,
	D3D10_RESOURCE_DIMENSION_TEXTURE3D = 4
};

enum DXGI_FORMAT_E {
	DXGI_FORMAT_UNKNOWN = 0,
	DXGI_FORMAT_R32G32B32A32_TYPELESS = 1,
	DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
	DXGI_FORMAT_R32G32B32A32_UINT = 3,
	DXGI_FORMAT_R32G32B32A32_SINT = 4,
	DXGI_FORMAT_R32G32B32_TYPELESS = 5,
	DXGI_FORMAT_R32G32B32_FLOAT = 6,
	DXGI_FORMAT_R32G32B32_UINT = 7,
	DXGI_FORMAT_R32G32B32_SINT = 8,
	DXGI_FORMAT_R16G16B16A16_TYPELESS = 9,
	DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
	DXGI_FORMAT_R16G16B16A16_UNORM = 11,
	DXGI_FORMAT_R16G16B16A16_UINT = 12,
	DXGI_FORMAT_R16G16B16A16_SNORM = 13,
	DXGI_FORMAT_R16G16B16A16_SINT = 14,
	DXGI_FORMAT_R32G32_TYPELESS = 15,
	DXGI_FORMAT_R32G32_FLOAT = 16,
	DXGI_FORMAT_R32G32_UINT = 17,
	DXGI_FORMAT_R32G32_SINT = 18,
	DXGI_FORMAT_R32G8X24_TYPELESS = 19,
	DXGI_FORMAT_D32_FLOAT_S8X24_UINT = 20,
	DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS = 21,
	DXGI_FORMAT_X32_TYPELESS_G8X24_UINT = 22,
	DXGI_FORMAT_R10G10B10A2_TYPELESS = 23,
	DXGI_FORMAT_R10G10B10A2_UNORM = 24,
	DXGI_FORMAT_R10G10B10A2_UINT = 25,
	DXGI_FORMAT_R11G11B10_FLOAT = 26,
	DXGI_FORMAT_R8G8B8A8_TYPELESS = 27,
	DXGI_FORMAT_R8G8B8A8_UNORM = 28,
	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB = 29,
	DXGI_FORMAT_R8G8B8A8_UINT = 30,
	DXGI_FORMAT_R8G8B8A8_SNORM = 31,
	DXGI_FORMAT_R8G8B8A8_SINT = 32,
	DXGI_FORMAT_R16G16_TYPELESS = 33,
	DXGI_FORMAT_R16G16_FLOAT = 34,
	DXGI_FORMAT_R16G16_UNORM = 35,
	DXGI_FORMAT_R16G16_UINT = 36,
	DXGI_FORMAT_R16G16_SNORM = 37,
	DXGI_FORMAT_R16G16_SINT = 38,
	DXGI_FORMAT_R32_TYPELESS = 39,
	DXGI_FORMAT_D32_FLOAT = 40,
	DXGI_FORMAT_R32_FLOAT = 41,
	DXGI_FORMAT_R32_UINT = 42,
	DXGI_FORMAT_R32_SINT = 43,
	DXGI_FORMAT_R24G8_TYPELESS = 44,
	DXGI_FORMAT_D24_UNORM_S8_UINT = 45,
	DXGI_FORMAT_R24_UNORM_X8_TYPELESS = 46,
	DXGI_FORMAT_X24_TYPELESS_G8_UINT = 47,
	DXGI_FORMAT_R8G8_TYPELESS = 48,
	DXGI_FORMAT_R8G8_UNORM = 49,
	DXGI_FORMAT_R8G8_UINT = 50,
	DXGI_FORMAT_R8G8_SNORM = 51,
	DXGI_FORMAT_R8G8_SINT = 52,
	DXGI_FORMAT_R16_TYPELESS = 53,
	DXGI_FORMAT_R16_FLOAT = 54,
	DXGI_FORMAT_D16_UNORM = 55,
	DXGI_FORMAT_R16_UNORM = 56,
	DXGI_FORMAT_R16_UINT = 57,
	DXGI_FORMAT_R16_SNORM = 58,
	DXGI_FORMAT_R16_SINT = 59,
	DXGI_FORMAT_R8_TYPELESS = 60,
	DXGI_FORMAT_R8_UNORM = 61,
	DXGI_FORMAT_R8_UINT = 62,
	DXGI_FORMAT_R8_SNORM = 63,
	DXGI_FORMAT_R8_SINT = 64,
	DXGI_FORMAT_A8_UNORM = 65,
	DXGI_FORMAT_R1_UNORM = 66,
	DXGI_FORMAT_R9G9B9E5_SHAREDEXP = 67,
	DXGI_FORMAT_R8G8_B8G8_UNORM = 68,
	DXGI_FORMAT_G8R8_G8B8_UNORM = 69,
	DXGI_FORMAT_BC1_TYPELESS = 70,
	DXGI_FORMAT_BC1_UNORM = 71,
	DXGI_FORMAT_BC1_UNORM_SRGB = 72,
	DXGI_FORMAT_BC2_TYPELESS = 73,
	DXGI_FORMAT_BC2_UNORM = 74,
	DXGI_FORMAT_BC2_UNORM_SRGB = 75,
	DXGI_FORMAT_BC3_TYPELESS = 76,
	DXGI_FORMAT_BC3_UNORM = 77,
	DXGI_FORMAT_BC3_UNORM_SRGB = 78,
	DXGI_FORMAT_BC4_TYPELESS = 79,
	DXGI_FORMAT_BC4_UNORM = 80,
	DXGI_FORMAT_BC4_SNORM = 81,
	DXGI_FORMAT_BC5_TYPELESS = 82,
	DXGI_FORMAT_BC5_UNORM = 83,
	DXGI_FORMAT_BC5_SNORM = 84,
	DXGI_FORMAT_B5G6R5_UNORM = 85,
	DXGI_FORMAT_B5G5R5A1_UNORM = 86,
	DXGI_FORMAT_B8G8R8A8_UNORM = 87,
	DXGI_FORMAT_B8G8R8X8_UNORM = 88,
	DXGI_FORMAT_R10G10B10_XR_BIAS_A2_UNORM = 89,
	DXGI_FORMAT_B8G8R8A8_TYPELESS = 90,
	DXGI_FORMAT_B8G8R8A8_UNORM_SRGB = 91,
	DXGI_FORMAT_B8G8R8X8_TYPELESS = 92,
	DXGI_FORMAT_B8G8R8X8_UNORM_SRGB = 93,
	DXGI_FORMAT_BC6H_TYPELESS = 94,
	DXGI_FORMAT_BC6H_UF16 = 95,
	DXGI_FORMAT_BC6H_SF16 = 96,
	DXGI_FORMAT_BC7_TYPELESS = 97,
	DXGI_FORMAT_BC7_UNORM = 98,
	DXGI_FORMAT_BC7_UNORM_SRGB = 99,
	DXGI_FORMAT_AYUV = 100,
	DXGI_FORMAT_Y410 = 101,
	DXGI_FORMAT_Y416 = 102,
	DXGI_FORMAT_NV12 = 103,
	DXGI_FORMAT_P010 = 104,
	DXGI_FORMAT_P016 = 105,
	DXGI_FORMAT_420_OPAQUE = 106,
	DXGI_FORMAT_YUY2 = 107,
	DXGI_FORMAT_Y210 = 108,
	DXGI_FORMAT_Y216 = 109,
	DXGI_FORMAT_NV11 = 110,
	DXGI_FORMAT_AI44 = 111,
	DXGI_FORMAT_IA44 = 112,
	DXGI_FORMAT_P8 = 113,
	DXGI_FORMAT_A8P8 = 114,
	DXGI_FORMAT_B4G4R4A4_UNORM = 115,
	DXGI_FORMAT_P208 = 130,
	DXGI_FORMAT_V208 = 131,
	DXGI_FORMAT_V408 = 132,
	DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE,
	DXGI_FORMAT_SAMPLER_FEEDBACK_MIP_REGION_USED_OPAQUE,
	DXGI_FORMAT_FORCE_UINT = 0xffffffff
};

typedef struct {
	DXGI_FORMAT_E				dxgiFormat;
	D3D10_RESOURCE_DIMENSION_E	resourceDimension;
	uint32_t                 miscFlag;
	uint32_t                 arraySize;
	uint32_t                 miscFlags2;
} DDHEADERDX10;


struct DDPIXELFORMAT
{
	uint32_t			Size;		// Must be 32
	uint32_t			Flags;
	uint32_t			FourCC;
	uint32_t			RGBBitCount;
	uint32_t			RBitMask, GBitMask, BBitMask;
	uint32_t			RGBAlphaBitMask;
};

struct DDCAPS2
{
	uint32_t			Caps1, Caps2;
	uint32_t			Reserved[2];
};

struct DDSURFACEDESC2
{
	uint32_t			Size;		// Must be 124. DevIL claims some writers set it to 'DDS ' instead.
	uint32_t			Flags;
	uint32_t			Height;
	uint32_t			Width;
	union
	{
		int32_t		Pitch;
		uint32_t	LinearSize;
	};
	uint32_t			Depth;
	uint32_t			MipMapCount;
	union
	{
		int32_t				Offsets[11];
		uint32_t			Reserved1[11];
	};
	DDPIXELFORMAT		PixelFormat;
	DDCAPS2				Caps;
	uint32_t			Reserved2;
};

struct DDSFileHeader
{
	uint32_t			Magic;
	DDSURFACEDESC2		Desc;
};


//==========================================================================
//
// A DDS image
// @Cockatrice - repurposed now specifically for BCx Compressed GPU data
// This really served no purpose before since it wasn't keeping the texs
// compressed in VRAM
//
//==========================================================================

class FDDSTexture : public FImageSource
{
	enum
	{
		PIX_Palette = 0,
		PIX_Alphatex = 1,
		PIX_ARGB = 2
	};
public:
	FDDSTexture (FileReader &lump, int lumpnum, void *surfdesc, void *dx10header);
	FDDSTexture (int lumpnum);

	PalettedPixels CreatePalettedPixels(int conversion, int frame = 0) override;
	int ReadCompressedPixels(FileReader* reader, unsigned char** data, size_t& size, size_t& unitSize, int& mipLevels) override;

	bool IsGPUOnly() override { return true; }
	int getGLFormat() const override { return glFormat; }
	int getVKFormat() const override { return vkFormat; }


	bool SerializeForTextureDef(FILE* fp, FString& name, int useType, FGameTexture* gameTex)  override {
		const char* fullName = fileSystem.GetFileFullName(SourceLump);
		fprintf(fp, "%d:%s:%s:%d:%dx%d:%dx%d:%d:%d:%d:%d:", 2, name.GetChars(), fullName != NULL ? fullName : "-", useType, Width, Height, LeftOffset, TopOffset, LinearSize, storedMips,  (int)bMasked, (int)bTranslucent);

		// Signal that the next line is not SPI
		fprintf(fp, "0\n");

		return true;
	}


	int DeSerializeFromTextureDef(FileReader& fr) override {
		int fileType = 0, useType = 0;
		int masked = 0, translucent = 0, numMips = 0;
		int numSPI = 0;

		char id[9], path[1024];
		id[0] = '\0';
		path[0] = '\0';

		char str[1800];

		if (fr.Gets(str, 1800)) {

			int count = sscanf(str,
				"%d:%8[^:]:%1023[^:]:%d:%dx%d:%dx%d:%d:%d:%d:%d",
				&fileType, id, path, &useType, &Width, &Height, &LeftOffset, &TopOffset, &LinearSize, &numMips, &masked, &translucent
			);

			bMasked = masked;
			bTranslucent = translucent;
			storedMips = (uint8_t)numMips;

			if (count != 12) {
				Printf("Failed to parse DDS Texture: %s\n", id);
				return 0;
			}

			return 2;
		}

		return 0;
	}


	bool DeSerializeExtraDataFromTextureDef(FileReader& fr, FGameTexture* gameTex) override {
		// Assign SPI if possible
		if (gameTex != nullptr) {
			SpritePositioningInfo spi[2];
			gameTex->GenerateEmptySpriteData(spi, Width, Height);
			SpritePositioningInfo* spir = gameTex->HasSpritePositioning() ? (SpritePositioningInfo*)&gameTex->GetSpritePositioning(0) : (SpritePositioningInfo*)ImageArena.Alloc(2 * sizeof(SpritePositioningInfo));

			// Copy spi into correct location
			memcpy(spir, spi, sizeof(SpritePositioningInfo) * 2);
			gameTex->SetSpriteRect(spir, true);
		}

		return true;
	}

protected:
	uint32_t Format;

	uint32_t RMask, GMask, BMask, AMask;
	uint8_t RShiftL, GShiftL, BShiftL, AShiftL;
	uint8_t RShiftR, GShiftR, BShiftR, AShiftR;

	int32_t BlockSize = 16;
	int32_t Pitch;
	uint32_t LinearSize;
	uint8_t storedMips;
	bool hasDX10Header = false;

	int glFormat = 0x8E8C;	// GL_COMPRESSED_RGBA_BPTC_UNORM
	int vkFormat = 145;		// VK_FORMAT_BC7_UNORM_BLOCK

	static void CalcBitShift (uint32_t mask, uint8_t *lshift, uint8_t *rshift);

	void ReadRGB (FileReader &lump, uint8_t *buffer, int pixelmode);
	void DecompressDXT1 (FileReader &lump, uint8_t *buffer, int pixelmode);
	void DecompressDXT3 (FileReader &lump, bool premultiplied, uint8_t *buffer, int pixelmode);
	void DecompressDXT5 (FileReader &lump, bool premultiplied, uint8_t *buffer, int pixelmode);

	int CopyPixels(FBitmap *bmp, int conversion, int frame = 0) override;

	friend class FTexture;
};



FImageSource* DDSImage_TryMake(FileReader& fr, int lumpnum, bool* hasExtraInfo = nullptr) {
	auto img = new FDDSTexture(lumpnum);
	int res = img->DeSerializeFromTextureDef(fr);
	if (res == 0) {
		delete img;
		return nullptr;
	}
	if (res > 1 && hasExtraInfo != nullptr) *hasExtraInfo = true;
	return img;
}


//==========================================================================
//
//
//
//==========================================================================

static bool CheckDDS (FileReader &file)
{
	DDSFileHeader Header;

	file.Seek(0, FileReader::SeekSet);
	if (file.Read (&Header, sizeof(Header)) != sizeof(Header))
	{
		return false;
	}
	return Header.Magic == ID_DDS &&
		(LittleLong(Header.Desc.Size) == sizeof(DDSURFACEDESC2) || Header.Desc.Size == ID_DDS) &&
		LittleLong(Header.Desc.PixelFormat.Size) == sizeof(DDPIXELFORMAT) &&
		(LittleLong(Header.Desc.Flags) & (DDSD_CAPS | DDSD_PIXELFORMAT | DDSD_WIDTH | DDSD_HEIGHT | 0x4)) == (DDSD_CAPS | DDSD_PIXELFORMAT | DDSD_WIDTH | DDSD_HEIGHT | 0x4) &&
		Header.Desc.Width != 0 &&
		Header.Desc.Height != 0;
}

//==========================================================================
//
//
//
//==========================================================================

FImageSource *DDSImage_TryCreate (FileReader &data, int lumpnum)
{
	union
	{
		DDSURFACEDESC2	surfdesc;
		uint32_t		byteswapping[sizeof(DDSURFACEDESC2) / 4];
	};

	union {
		DDHEADERDX10 dx10header;
		uint32_t	 dx10Byteswapping[sizeof(DDHEADERDX10) / 4];
	};

	memset(&dx10header, 0, sizeof(dx10header));

	if (!CheckDDS(data)) return NULL;

	data.Seek(4, FileReader::SeekSet);
	data.Read (&surfdesc, sizeof(surfdesc));

#ifdef __BIG_ENDIAN__
	// Every single element of the header is a uint32_t
	for (unsigned int i = 0; i < sizeof(DDSURFACEDESC2) / 4; ++i)
	{
		byteswapping[i] = LittleLong(byteswapping[i]);
	}
	// Undo the byte swap for the pixel format
	surfdesc.PixelFormat.FourCC = LittleLong(surfdesc.PixelFormat.FourCC);
#endif

	bool hasDX10 = surfdesc.PixelFormat.Flags & 0x4 && surfdesc.PixelFormat.FourCC == ID_DX10;

	if (!hasDX10) {
		// Verify type
		if (surfdesc.PixelFormat.FourCC != ID_DXT1 && surfdesc.PixelFormat.FourCC != ID_DXT3) {
			FString lumpname = fileSystem.GetFileFullName(lumpnum);
			I_FatalError("DDS File Error (%s) Invalid Format: No DX10 header specified!", lumpname.GetChars());
		}
		else {
			if (surfdesc.PixelFormat.FourCC == ID_DXT1) {
				dx10header.dxgiFormat = DXGI_FORMAT_BC1_UNORM;	// Assume RGB for now, SRGB can come later
			}
			else if (surfdesc.PixelFormat.FourCC == ID_DXT3) {
				dx10header.dxgiFormat = DXGI_FORMAT_BC3_UNORM;	// Assume RGB for now, SRGB can come later
	}
			dx10header.resourceDimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
}
	}
	else {
		data.Read(&dx10header, sizeof(dx10header));
	}


#ifdef __BIG_ENDIAN__
	// Every single element of the dx10header is also a uint32_t
	for (unsigned int i = 0; i < sizeof(DDHEADERDX10) / 4; ++i)
	{
		dx10Byteswapping[i] = LittleLong(byteswapping[i]);
	}
#endif

	if (surfdesc.LinearSize > 20971520) {
		FString lumpname = fileSystem.GetFileFullName(lumpnum);
		I_FatalError("DDS File Error (%s) Invalid Format: File is too large! This error should probably go away.", lumpname.GetChars());
	} else if(dx10header.arraySize > 1) {
		FString lumpname = fileSystem.GetFileFullName(lumpnum);
		I_FatalError("DDS File Error (%s) Invalid Format: Multiple layers or images is currently unsupported!", lumpname.GetChars());
	} else if (dx10header.dxgiFormat != DXGI_FORMAT_BC7_UNORM && dx10header.dxgiFormat != DXGI_FORMAT_BC1_UNORM && dx10header.dxgiFormat != DXGI_FORMAT_BC3_UNORM) {
		FString lumpname = fileSystem.GetFileFullName(lumpnum);
		I_FatalError("DDS File Error (%s) Invalid Format: Currently only DXGI_FORMAT_BC7_UNORM, DXGI_FORMAT_BC1_UNORM  and DXGI_FORMAT_BC3_UNORM formats are supported! More Formats should eventually be included when I'm not lazy.", lumpname.GetChars());
	} else if (dx10header.resourceDimension != D3D10_RESOURCE_DIMENSION_TEXTURE2D) {
		FString lumpname = fileSystem.GetFileFullName(lumpnum);
		I_FatalError("DDS File Error (%s) Invalid Format: Only 2D textures are supported!", lumpname.GetChars());
	}

	return new FDDSTexture (data, lumpnum, &surfdesc, &dx10header);
}

//==========================================================================
//
//
//
//==========================================================================

FDDSTexture::FDDSTexture (FileReader &lump, int lumpnum, void *vsurfdesc, void* dx10header)
: FImageSource(lumpnum)
{
	DDSURFACEDESC2 *surf = (DDSURFACEDESC2 *)vsurfdesc;
	DDHEADERDX10* dx10 = (DDHEADERDX10*)dx10header;

	bMasked = false;
	Width = uint16_t(surf->Width);
	Height = uint16_t(surf->Height);
	hasDX10Header = surf->PixelFormat.Flags & 0x4 && surf->PixelFormat.FourCC == ID_DX10;

	// @Cockatrice - Only supporting BC1 and BC7 RGB/ARGB at the moment
	// There is no use case for the other formats since BC1 with no alpha is the smallest size
	// And BC7 with alpha is the most robust format for sprites
	// Also throwing in BC3 with alpha support as a fallback for GPUs that don't support BC7
	switch (dx10->dxgiFormat) {
		case DXGI_FORMAT_BC1_UNORM:
			vkFormat = 131;		// VK_FORMAT_BC1_RGB_UNORM_BLOCK
			glFormat = 0x83F0;	// GL_COMPRESSED_RGB_S3TC_DXT1_EXT
			BlockSize = 8;
			break;
		case DXGI_FORMAT_BC3_UNORM:
			vkFormat = 137;		// VK_FORMAT_BC3_UNORM_BLOCK
			glFormat = 0x83F2;  // GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
			BlockSize = 16;
			break;
		case DXGI_FORMAT_BC7_UNORM:
		default:
			// By default assume BC7, since it's the most useful for sprites
			vkFormat = 145;		// VK_FORMAT_BC7_UNORM_BLOCK
			glFormat = 0x8E8C;	// GL_COMPRESSED_RGBA_BPTC_UNORM
			BlockSize = 16;
			break;
	}

	LinearSize = surf->LinearSize;
	Format = surf->PixelFormat.FourCC;
	Pitch = 0;

	// This is a bit backwards, but this value should be set by Offsetter. 
	// We want translucency by default, and most exporters will set this value to zero
	// So 0 = translucent  1 = not translucent
	bTranslucent = dx10->dxgiFormat == DXGI_FORMAT_BC1_UNORM ? false : surf->Offsets[2] == 0;
	storedMips = surf->MipMapCount;
	SetOffsets(surf->Offsets[0], surf->Offsets[1]);
}


FDDSTexture::FDDSTexture(int lumpnum) : FImageSource(lumpnum)
{
	
	bMasked = false;
	Width = uint16_t(0);
	Height = uint16_t(0);

	Format = ID_DX10;
	Pitch = 0;

	// This is a bit backwards, but this value should be set by Offsetter. 
	// We want translucency by default, and most exporters will set this value to zero
	// So 0 = translucent  1 = not translucent
	bTranslucent = 0;
	storedMips = 0;
	SetOffsets(0,0);
}


// Data must be interpreted, this may include mipmap data which may be used or discarded at will
int FDDSTexture::ReadCompressedPixels(FileReader* reader, unsigned char** data, size_t& size, size_t& unitSize, int& mipLevels) {
	const size_t headerSize = sizeof(DDSURFACEDESC2) + (hasDX10Header ? sizeof(DDHEADERDX10) : 0) + 4;
	
	// TODO: Read remapped/translated version here when necessary!
	int lumpSize = fileSystem.FileLength(SourceLump);
	if (lumpSize <= 0) {
		return 0;
	}

	size_t pixelDataSize = lumpSize - headerSize;

	unsigned char* cacheData = new unsigned char[lumpSize];
	reader->Read(cacheData, lumpSize);
	
	*data = (unsigned char *)malloc(pixelDataSize);
	//unitSize = LinearSize;
	//linearSize is wrong most of the time with non DX10 images
	//calculate manually
	unitSize = std::max(1, ((Width + 3) / 4)) * std::max(1, ((Height + 3) / 4)) * BlockSize;
	size = pixelDataSize;
	mipLevels = storedMips;

	if (pixelDataSize >= unitSize) {
		// Copy data from file
		memcpy(*data, cacheData + headerSize, pixelDataSize);
		delete[]cacheData;
	}
	else {
		delete[]cacheData;
		//memset(*data, 9, pixelDataSize);
		free(*data);
		*data = nullptr;
		size = 0;

		return 0;
	}

	return (int)bTranslucent;
}

//==========================================================================
//
// Returns the number of bits the color must be shifted to produce
// an 8-bit value, as in:
//
// c   = (color & mask) << lshift;
// c  |= c >> rshift;
// c >>= 24;
//
// For any color of at least 4 bits, this ensures that the result
// of the calculation for c will be fully saturated, given a maximum
// value for the input bit mask.
//
//==========================================================================

void FDDSTexture::CalcBitShift (uint32_t mask, uint8_t *lshiftp, uint8_t *rshiftp)
{
	uint8_t shift;

	if (mask == 0)
	{
		*lshiftp = *rshiftp = 0;
		return;
	}

	shift = 0;
	while ((mask & 0x80000000) == 0)
	{
		mask <<= 1;
		shift++;
	}
	*lshiftp = shift;

	shift = 0;
	while (mask & 0x80000000)
	{
		mask <<= 1;
		shift++;
	}
	*rshiftp = shift;
}

//==========================================================================
//
//
//
//==========================================================================

PalettedPixels FDDSTexture::CreatePalettedPixels(int conversion, int frame)
{
	auto lump = fileSystem.OpenFileReader (SourceLump);

	PalettedPixels Pixels(Width*Height);

	lump.Seek (sizeof(DDSURFACEDESC2) + 4, FileReader::SeekSet);

	int pmode = conversion == luminance ? PIX_Alphatex : PIX_Palette;
	if (Format >= 1 && Format <= 4)		// RGB: Format is # of bytes per pixel
	{
		ReadRGB (lump, Pixels.Data(), pmode);
	}
	else if (Format == ID_DXT1)
	{
		DecompressDXT1 (lump, Pixels.Data(), pmode);
	}
	else if (Format == ID_DXT3 || Format == ID_DXT2)
	{
		DecompressDXT3 (lump, Format == ID_DXT2, Pixels.Data(), pmode);
	}
	else if (Format == ID_DXT5 || Format == ID_DXT4)
	{
		DecompressDXT5 (lump, Format == ID_DXT4, Pixels.Data(), pmode);
	}
	return Pixels;
}

//==========================================================================
//
// Note that pixel size == 8 is column-major, but 32 is row-major!
//
//==========================================================================

void FDDSTexture::ReadRGB (FileReader &lump, uint8_t *buffer, int pixelmode)
{
	uint32_t x, y;
	uint32_t amask = AMask == 0 ? 0 : 0x80000000 >> AShiftL;
	uint8_t *linebuff = new uint8_t[Pitch];

	for (y = Height; y > 0; --y)
	{
		uint8_t *buffp = linebuff;
		uint8_t *pixelp = pixelmode == PIX_ARGB ? buffer + 4 * (y - 1)*Width : buffer + y - 1;
		lump.Read (linebuff, Pitch);
		for (x = Width; x > 0; --x)
		{
			uint32_t c;
			if (Format == 4)
			{
				c = LittleLong(*(uint32_t *)buffp); buffp += 4;
			}
			else if (Format == 2)
			{
				c = LittleShort(*(uint16_t *)buffp); buffp += 2;
			}
			else if (Format == 3)
			{
				c = buffp[0] | (buffp[1] << 8) | (buffp[2] << 16); buffp += 3;
			}
			else //  Format == 1
			{
				c = *buffp++;
			}
			if (pixelmode != PIX_ARGB)
			{
				if (amask == 0 || (c & amask))
				{
					uint32_t r = (c & RMask) << RShiftL; r |= r >> RShiftR;
					uint32_t g = (c & GMask) << GShiftL; g |= g >> GShiftR;
					uint32_t b = (c & BMask) << BShiftL; b |= b >> BShiftR;
					uint32_t a = (c & AMask) << AShiftL; a |= a >> AShiftR;
					*pixelp = ImageHelpers::RGBToPalette(pixelmode == PIX_Alphatex, r >> 24, g >> 24, b >> 24, a >> 24);
				}
				else
				{
					*pixelp = 0;
					bMasked = true;
				}
				pixelp += Height;
			}
			else
			{
				uint32_t r = (c & RMask) << RShiftL; r |= r >> RShiftR;
				uint32_t g = (c & GMask) << GShiftL; g |= g >> GShiftR;
				uint32_t b = (c & BMask) << BShiftL; b |= b >> BShiftR;
				uint32_t a = (c & AMask) << AShiftL; a |= a >> AShiftR;
				pixelp[0] = (uint8_t)(b>>24);
				pixelp[1] = (uint8_t)(g>>24);
				pixelp[2] = (uint8_t)(r>>24);
				pixelp[3] = (uint8_t)(a>>24);
				pixelp+=4;
			}
		}
	}
	delete[] linebuff;
}

//==========================================================================
//
//
//
//==========================================================================

void FDDSTexture::DecompressDXT1 (FileReader &lump, uint8_t *buffer, int pixelmode)
{
	const long blocklinelen = ((Width + 3) >> 2) << 3;
	uint8_t *blockbuff = new uint8_t[blocklinelen];
	uint8_t *block;
	PalEntry color[4];
	uint8_t palcol[4] = { 0,0,0,0 };	// shut up compiler warnings.
	int ox, oy, x, y, i;

	color[0].a = 255;
	color[1].a = 255;
	color[2].a = 255;

	for (oy = 0; oy < Height; oy += 4)
	{
		lump.Read (blockbuff, blocklinelen);
		block = blockbuff;
		for (ox = 0; ox < Width; ox += 4)
		{
			uint16_t color16[2] = { LittleShort(((uint16_t *)block)[0]), LittleShort(((uint16_t *)block)[1]) };

			// Convert color from R5G6B5 to R8G8B8.
			for (i = 1; i >= 0; --i)
			{
				color[i].r = ((color16[i] & 0xF800) >> 8) | (color16[i] >> 13);
				color[i].g = ((color16[i] & 0x07E0) >> 3) | ((color16[i] & 0x0600) >> 9);
				color[i].b = ((color16[i] & 0x001F) << 3) | ((color16[i] & 0x001C) >> 2);
			}
			if (color16[0] > color16[1])
			{ // Four-color block: derive the other two colors.
				color[2].r = (color[0].r + color[0].r + color[1].r + 1) / 3;
				color[2].g = (color[0].g + color[0].g + color[1].g + 1) / 3;
				color[2].b = (color[0].b + color[0].b + color[1].b + 1) / 3;

				color[3].r = (color[0].r + color[1].r + color[1].r + 1) / 3;
				color[3].g = (color[0].g + color[1].g + color[1].g + 1) / 3;
				color[3].b = (color[0].b + color[1].b + color[1].b + 1) / 3;
				color[3].a = 255;
			}
			else
			{ // Three-color block: derive the other color.
				color[2].r = (color[0].r + color[1].r) / 2;
				color[2].g = (color[0].g + color[1].g) / 2;
				color[2].b = (color[0].b + color[1].b) / 2;

				color[3].a = color[3].b = color[3].g = color[3].r = 0;

				// If you have a three-color block, presumably that transparent
				// color is going to be used.
				bMasked = true;
			}
			// Pick colors from the palette for each of the four colors.
			if (pixelmode != PIX_ARGB) for (i = 3; i >= 0; --i)
			{
				palcol[i] = ImageHelpers::RGBToPalette(pixelmode == PIX_Alphatex, color[i]);
			}
			// Now decode this 4x4 block to the pixel buffer.
			for (y = 0; y < 4; ++y)
			{
				if (oy + y >= Height)
				{
					break;
				}
				uint8_t yslice = block[4 + y];
				for (x = 0; x < 4; ++x)
				{
					if (ox + x >= Width)
					{
						break;
					}
					int ci = (yslice >> (x + x)) & 3;
					if (pixelmode != PIX_ARGB) 
					{
						buffer[oy + y + (ox + x) * Height] = palcol[ci];
					}
					else
					{
						uint8_t * tcp = &buffer[(ox + x)*4 + (oy + y) * Width*4];
						tcp[0] = color[ci].b;
						tcp[1] = color[ci].g;
						tcp[2] = color[ci].r;
						tcp[3] = color[ci].a;
					}
				}
			}
			block += 8;
		}
	}
	delete[] blockbuff;
}

//==========================================================================
//
// DXT3: Decompression is identical to DXT1, except every 64-bit block is
// preceded by another 64-bit block with explicit alpha values.
//
//==========================================================================

void FDDSTexture::DecompressDXT3 (FileReader &lump, bool premultiplied, uint8_t *buffer, int pixelmode)
{
	const long blocklinelen = ((Width + 3) >> 2) << 4;
	uint8_t *blockbuff = new uint8_t[blocklinelen];
	uint8_t *block;
	PalEntry color[4];
	uint8_t palcol[4] = { 0,0,0,0 };
	int ox, oy, x, y, i;

	for (oy = 0; oy < Height; oy += 4)
	{
		lump.Read (blockbuff, blocklinelen);
		block = blockbuff;
		for (ox = 0; ox < Width; ox += 4)
		{
			uint16_t color16[2] = { LittleShort(((uint16_t *)block)[4]), LittleShort(((uint16_t *)block)[5]) };

			// Convert color from R5G6B5 to R8G8B8.
			for (i = 1; i >= 0; --i)
			{
				color[i].r = ((color16[i] & 0xF800) >> 8) | (color16[i] >> 13);
				color[i].g = ((color16[i] & 0x07E0) >> 3) | ((color16[i] & 0x0600) >> 9);
				color[i].b = ((color16[i] & 0x001F) << 3) | ((color16[i] & 0x001C) >> 2);
			}
			// Derive the other two colors.
			color[2].r = (color[0].r + color[0].r + color[1].r + 1) / 3;
			color[2].g = (color[0].g + color[0].g + color[1].g + 1) / 3;
			color[2].b = (color[0].b + color[0].b + color[1].b + 1) / 3;

			color[3].r = (color[0].r + color[1].r + color[1].r + 1) / 3;
			color[3].g = (color[0].g + color[1].g + color[1].g + 1) / 3;
			color[3].b = (color[0].b + color[1].b + color[1].b + 1) / 3;

			// Pick colors from the palette for each of the four colors.
			if (pixelmode != PIX_ARGB) for (i = 3; i >= 0; --i)
			{
				palcol[i] = ImageHelpers::RGBToPalette(pixelmode == PIX_Alphatex, color[i], false);
			}

			// Now decode this 4x4 block to the pixel buffer.
			for (y = 0; y < 4; ++y)
			{
				if (oy + y >= Height)
				{
					break;
				}
				uint8_t yslice = block[12 + y];
				uint16_t yalphaslice = LittleShort(((uint16_t *)block)[y]);
				for (x = 0; x < 4; ++x)
				{
					if (ox + x >= Width)
					{
						break;
					}
					if (pixelmode == PIX_Palette)
					{
						buffer[oy + y + (ox + x) * Height] = ((yalphaslice >> (x*4)) & 15) < 8 ?
							(bMasked = true, 0) : palcol[(yslice >> (x + x)) & 3];
					}
					else if (pixelmode == PIX_Alphatex)
					{
						int alphaval = ((yalphaslice >> (x * 4)) & 15);
						int palval = palcol[(yslice >> (x + x)) & 3];
						buffer[oy + y + (ox + x) * Height] = palval * alphaval / 15;
					}
					else
					{
						uint8_t * tcp = &buffer[(ox + x)*4 + (oy + y) * Width*4];
						int c = (yslice >> (x + x)) & 3;
						tcp[0] = color[c].b;
						tcp[1] = color[c].g;
						tcp[2] = color[c].r;
						tcp[3] = ((yalphaslice >> (x * 4)) & 15) * 0x11;
					}
				}
			}
			block += 16;
		}
	}
	delete[] blockbuff;
}

//==========================================================================
//
// DXT5: Decompression is identical to DXT3, except every 64-bit alpha block
// contains interpolated alpha values, similar to the 64-bit color block.
//
//==========================================================================

void FDDSTexture::DecompressDXT5 (FileReader &lump, bool premultiplied, uint8_t *buffer, int pixelmode)
{
	const size_t blocklinelen = ((Width + 3) >> 2) << 4;
	uint8_t *blockbuff = new uint8_t[blocklinelen];
	uint8_t *block;
	PalEntry color[4];
	uint8_t palcol[4] = { 0,0,0,0 };
	uint32_t yalphaslice = 0;
	int ox, oy, x, y, i;

	for (oy = 0; oy < Height; oy += 4)
	{
		lump.Read (blockbuff, blocklinelen);
		block = blockbuff;
		for (ox = 0; ox < Width; ox += 4)
		{
			uint16_t color16[2] = { LittleShort(((uint16_t *)block)[4]), LittleShort(((uint16_t *)block)[5]) };
			uint8_t alpha[8];

			// Calculate the eight alpha values.
			alpha[0] = block[0];
			alpha[1] = block[1];

			if (alpha[0] >= alpha[1])
			{ // Eight-alpha block: derive the other six alphas.
				for (i = 0; i < 6; ++i)
				{
					alpha[i + 2] = ((6 - i) * alpha[0] + (i + 1) * alpha[1] + 3) / 7;
				}
			}
			else
			{ // Six-alpha block: derive the other four alphas.
				for (i = 0; i < 4; ++i)
				{
					alpha[i + 2] = ((4 - i) * alpha[0] + (i + 1) * alpha[1] + 2) / 5;
				}
				alpha[6] = 0;
				alpha[7] = 255;
			}

			// Convert color from R5G6B5 to R8G8B8.
			for (i = 1; i >= 0; --i)
			{
				color[i].r = ((color16[i] & 0xF800) >> 8) | (color16[i] >> 13);
				color[i].g = ((color16[i] & 0x07E0) >> 3) | ((color16[i] & 0x0600) >> 9);
				color[i].b = ((color16[i] & 0x001F) << 3) | ((color16[i] & 0x001C) >> 2);
			}
			// Derive the other two colors.
			color[2].r = (color[0].r + color[0].r + color[1].r + 1) / 3;
			color[2].g = (color[0].g + color[0].g + color[1].g + 1) / 3;
			color[2].b = (color[0].b + color[0].b + color[1].b + 1) / 3;

			color[3].r = (color[0].r + color[1].r + color[1].r + 1) / 3;
			color[3].g = (color[0].g + color[1].g + color[1].g + 1) / 3;
			color[3].b = (color[0].b + color[1].b + color[1].b + 1) / 3;

			// Pick colors from the palette for each of the four colors.
			if (pixelmode != PIX_ARGB) for (i = 3; i >= 0; --i)
			{
				palcol[i] = ImageHelpers::RGBToPalette(pixelmode == PIX_Alphatex, color[i], false);
			}
			// Now decode this 4x4 block to the pixel buffer.
			for (y = 0; y < 4; ++y)
			{
				if (oy + y >= Height)
				{
					break;
				}
				// Alpha values are stored in 3 bytes for 2 rows
				if ((y & 1) == 0)
				{
					yalphaslice = block[y*3] | (block[y*3+1] << 8) | (block[y*3+2] << 16);
				}
				else
				{
					yalphaslice >>= 12;
				}
				uint8_t yslice = block[12 + y];
				for (x = 0; x < 4; ++x)
				{
					if (ox + x >= Width)
					{
						break;
					}
					if (pixelmode == PIX_Palette)
					{
						buffer[oy + y + (ox + x) * Height] = alpha[((yalphaslice >> (x*3)) & 7)] < 128 ?
							(bMasked = true, 0) : palcol[(yslice >> (x + x)) & 3];
					}
					else if (pixelmode == PIX_Alphatex)
					{
						int alphaval = alpha[((yalphaslice >> (x * 3)) & 7)];
						int palval = palcol[(yslice >> (x + x)) & 3];
						buffer[oy + y + (ox + x) * Height] = palval * alphaval / 255;
					}
					else
					{
						uint8_t * tcp = &buffer[(ox + x)*4 + (oy + y) * Width*4];
						int c = (yslice >> (x + x)) & 3;
						tcp[0] = color[c].b;
						tcp[1] = color[c].g;
						tcp[2] = color[c].r;
						tcp[3] = alpha[((yalphaslice >> (x*3)) & 7)];
					}
				}
			}
			block += 16;
		}
	}
	delete[] blockbuff;
}

//===========================================================================
//
// FDDSTexture::CopyPixels
//
//===========================================================================

int FDDSTexture::CopyPixels(FBitmap *bmp, int conversion, int frame)
{
	auto lump = fileSystem.OpenFileReader (SourceLump);

	uint8_t *TexBuffer = bmp->GetPixels();

	lump.Seek (sizeof(DDSURFACEDESC2) + 4, FileReader::SeekSet);

	if (Format >= 1 && Format <= 4)		// RGB: Format is # of bytes per pixel
	{
		ReadRGB (lump, TexBuffer, PIX_ARGB);
	}
	else if (Format == ID_DXT1)
	{
		DecompressDXT1 (lump, TexBuffer, PIX_ARGB);
	}
	else if (Format == ID_DXT3 || Format == ID_DXT2)
	{
		DecompressDXT3 (lump, Format == ID_DXT2, TexBuffer, PIX_ARGB);
	}
	else if (Format == ID_DXT5 || Format == ID_DXT4)
	{
		DecompressDXT5 (lump, Format == ID_DXT4, TexBuffer, PIX_ARGB);
	}

	return -1;
}	
