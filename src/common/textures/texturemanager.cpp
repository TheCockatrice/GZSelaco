/*
** texturemanager.cpp
** The texture manager class
**
**---------------------------------------------------------------------------
** Copyright 2004-2008 Randy Heit
** Copyright 2006-2008 Christoph Oelckers
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

#include "printf.h"
#include "c_cvars.h"

#include "gstrings.h"
#include "textures.h"
#include "texturemanager.h"
#include "c_dispatch.h"
#include "sc_man.h"
#include "image.h"
#include "vectors.h"
#include "animtexture.h"
#include "formats/multipatchtexture.h"
#include "basics.h"
#include "cmdlib.h"
#include "m_argv.h"
#include "engineerrors.h"
#include "filesystem.h"
#include <set>

using namespace FileSys;

FTextureManager TexMan;


//==========================================================================
//
// FTextureManager :: FTextureManager
//
//==========================================================================

FTextureManager::FTextureManager ()
{
	memset (HashFirst, -1, sizeof(HashFirst));

	for (int i = 0; i < 2048; ++i)
	{
		sintable[i] = short(sin(i*(pi::pi() / 1024)) * 16384);
	}
}

//==========================================================================
//
// FTextureManager :: ~FTextureManager
//
//==========================================================================

FTextureManager::~FTextureManager ()
{
	DeleteAll();
}

//==========================================================================
//
// FTextureManager :: DeleteAll
//
//==========================================================================

void FTextureManager::DeleteAll()
{
	for (unsigned int i = 0; i < Textures.Size(); ++i)
	{
		delete Textures[i].Texture;
	}
	FImageSource::ClearImages();
	Textures.Clear();
	Translation.Clear();
	FirstTextureForFile.Clear();
	memset (HashFirst, -1, sizeof(HashFirst));
	DefaultTexture.SetInvalid();

	BuildTileData.Clear();
	tmanips.Clear();
}

//==========================================================================
//
// Flushes all hardware dependent data.
// This must not, under any circumstances, delete the wipe textures, because
// all CCMDs triggering a flush can be executed while a wipe is in progress
//
// This now also deletes the software textures because the software
// renderer can also use the texture scalers and that is the
// main reason to call this outside of the destruction code.
//
//==========================================================================

void FTextureManager::FlushAll()
{
	for (int i = TexMan.NumTextures() - 1; i >= 0; i--)
	{
		for (int j = 0; j < 2; j++)
		{
			Textures[i].Texture->CleanHardwareData();
			delete Textures[i].Texture->GetSoftwareTexture();
			calcShouldUpscale(Textures[i].Texture);
			Textures[i].Texture->SetSoftwareTexture(nullptr);
		}
	}
}

//==========================================================================
//
// Examines the lump contents to decide what type of texture to create,
// and creates the texture.
//
//==========================================================================

static FTexture* CreateTextureFromLump(int lumpnum, bool allowflats = false)
{
	if (lumpnum == -1) return nullptr;

	auto image = FImageSource::GetImage(lumpnum, allowflats);
	if (image != nullptr)
	{
		return new FImageTexture(image);
	}
	return nullptr;
}

//==========================================================================
//
// FTextureManager :: CheckForTexture
//
//==========================================================================

FTextureID FTextureManager::CheckForTexture (const char *name, ETextureType usetype, BITFIELD flags)
{
	int i;
	int firstfound = -1;
	auto firsttype = ETextureType::Null;

	if (name == NULL || name[0] == '\0')
	{
		return FTextureID(-1);
	}
	// [RH] Doom counted anything beginning with '-' as "no texture".
	// Hopefully nobody made use of that and had textures like "-EMPTY",
	// because -NOFLAT- is a valid graphic for ZDoom.
	if (name[0] == '-' && name[1] == '\0')
	{
		return FTextureID(0);
	}

	for(i = HashFirst[MakeKey(name) % HASH_SIZE]; i != HASH_END; i = Textures[i].HashNext)
	{
		auto tex = Textures[i].Texture;


		if (tex->GetName().CompareNoCase(name) == 0 )
		{
			// If we look for short names, we must ignore any long name texture.
			if ((flags & TEXMAN_ShortNameOnly) && tex->isFullNameTexture())
			{
				continue;
			}
			auto texUseType = tex->GetUseType();
			// The name matches, so check the texture type
			if (usetype == ETextureType::Any)
			{
				if (flags & TEXMAN_ReturnAll) return FTextureID(i);	// user asked to skip all checks, including null textures.
				// All NULL textures should actually return 0
				if (texUseType == ETextureType::FirstDefined && !(flags & TEXMAN_ReturnFirst)) return 0;
				if (texUseType == ETextureType::SkinGraphic && !(flags & TEXMAN_AllowSkins)) return 0;
				return FTextureID(texUseType==ETextureType::Null ? 0 : i);
			}
			else if ((flags & TEXMAN_Overridable) && texUseType == ETextureType::Override)
			{
				return FTextureID(i);
			}
			else if (texUseType == usetype)
			{
				return FTextureID(i);
			}
			else if (texUseType == ETextureType::FirstDefined && usetype == ETextureType::Wall)
			{
				if (!(flags & TEXMAN_ReturnFirst)) return FTextureID(0);
				else return FTextureID(i);
			}
			else if (texUseType == ETextureType::Null && usetype == ETextureType::Wall)
			{
				// We found a NULL texture on a wall -> return 0
				return FTextureID(0);
			}
			else
			{
				if (firsttype == ETextureType::Null ||
					(firsttype == ETextureType::MiscPatch &&
					 texUseType != firsttype &&
					 texUseType != ETextureType::Null)
				   )
				{
					firstfound = i;
					firsttype = texUseType;
				}
			}
		}
	}

	if ((flags & TEXMAN_TryAny) && usetype != ETextureType::Any)
	{
		// Never return the index of NULL textures.
		if (firstfound != -1)
		{
			if (flags & TEXMAN_ReturnAll) return FTextureID(i);	// user asked to skip all checks, including null textures.
			if (firsttype == ETextureType::Null) return FTextureID(0);
			if (firsttype == ETextureType::FirstDefined && !(flags & TEXMAN_ReturnFirst)) return FTextureID(0);
			return FTextureID(firstfound);
		}
	}


	if (!(flags & TEXMAN_ShortNameOnly))
	{
		// We intentionally only look for textures in subdirectories.
		// Any graphic being placed in the zip's root directory can not be found by this.
		if (strchr(name, '/') || (flags & TEXMAN_ForceLookup))
		{
			FGameTexture *const NO_TEXTURE = (FGameTexture*)-1; // marker for lumps we already checked that do not map to a texture.
			int lump = fileSystem.CheckNumForFullName(name);
			if (lump >= 0)
			{
				FGameTexture *tex = GetLinkedTexture(lump);
				if (tex == NO_TEXTURE) return FTextureID(-1);
				if (tex != NULL) return tex->GetID();
				if (flags & TEXMAN_DontCreate) return FTextureID(-1);	// we only want to check, there's no need to create a texture if we don't have one yet.
				tex = MakeGameTexture(CreateTextureFromLump(lump), nullptr, ETextureType::Override);
				if (tex != NULL)
				{
					tex->AddAutoMaterials();
					SetLinkedTexture(lump, tex);
					return AddGameTexture(tex);
				}
				else
				{
					// mark this lump as having no valid texture so that we don't have to retry creating one later.
					SetLinkedTexture(lump, NO_TEXTURE);
				}
			}
		}
	}
	if (!(flags & TEXMAN_NoAlias))
	{
		int* alias = aliases.CheckKey(name);
		if (alias) return FTextureID(*alias);
	}

	return FTextureID(-1);
}



//==========================================================================
//
// FTextureManager :: FindTextures
// @Cockatrice - Search through textures finding all that contain search
//
//==========================================================================

int FTextureManager::FindTextures(const char* search, TArray<FTextureID> *list, ETextureType usetype, BITFIELD flags)
{
	//int firstfound = -1;
	//auto firsttype = ETextureType::Null;
	int found = 0;
	//size_t stLen = strlen(search);
	
	// Disallow NULL and * (wildcard all) or **, we don't want to dump the entire texture database
	if (search == NULL || search[0] == '\0' || (search[0] == '*' && search[1] == '\0') || (search[0] == '*' && search[1] == '*')) {
		return 0;
	}

	for (auto &tx : Textures)
	{
		auto tex = tx.Texture;

		// If we look for short names, we must ignore any long name texture.
		if ((flags & TEXMAN_ShortNameOnly) && tex->isFullNameTexture())
		{
			continue;
		}
		auto texUseType = tex->GetUseType();
		// The name matches, so check the texture type
		if (usetype == ETextureType::Any)
		{
			// All NULL textures should actually return 0
			if (texUseType == ETextureType::FirstDefined && !(flags & TEXMAN_ReturnFirst)) continue;
			if (texUseType == ETextureType::SkinGraphic && !(flags & TEXMAN_AllowSkins)) continue;
			if (texUseType == ETextureType::Null) continue;
		}
		else if (texUseType != usetype)
		{
			continue;
		}
		
		if (strstr(tex->GetName().GetChars(), search) == nullptr) {
			continue;
		}
			
		list->Push(tex->GetID());
		found++;
	}

	return found;
}


//==========================================================================
//
// FTextureManager :: AnalyzeTextures
//
//==========================================================================

static const char* getUseTypeName(ETextureType  type) {
	switch (type) {
	case ETextureType::Any:
		return "Any";
	case ETextureType::Wall:
		return "Wall";
	case ETextureType::Flat:
		return "Flat";
	case ETextureType::Sprite:
		return "Sprite";
	case ETextureType::WallPatch:
		return "WallPatch";
	case ETextureType::Decal:
		return "Decal";
	default:
		return "Unknown";
	}
}



// @Cockatrice - Go through all textures to find the ones that most need optimization
// Designed to help reduce VRAM usage
void FTextureManager::AnalyzeTextures(int limit, bool recommendOnly) {
	auto cmp = [](TextureDescriptor* a, TextureDescriptor* b) { return (a->Texture->GetTexelWidth() * a->Texture->GetTexelHeight()) > (b->Texture->GetTexelWidth() * b->Texture->GetTexelHeight()); };
	std::set<TextureDescriptor*, decltype(cmp)> list(cmp);

	Printf("\nAnalyzing %d textures...\n\n", Textures.size());

	size_t totalEstimatedTextureSize = 0;
	size_t totalEstimatedTextureSizeWithoutCompression = 0;
	size_t totalWallSize = 0;
	size_t totalFlatSize = 0;
	size_t totalSpriteSize = 0;
	size_t totalMiscSize = 0, totalFontSize = 0;

	int numFiles = fileSystem.GetNumEntries();


	for (auto& tx : Textures)
	{
		auto tex = tx.Texture;

		// If we look for short names, we must ignore any long name texture.
		auto texUseType = tex->GetUseType();
		int fmt = tex->GetTexture()->GetImage() ? tex->GetTexture()->GetImage()->getGLFormat() : 0;

		size_t pixels = tex->GetTexelWidth() * tex->GetTexelHeight();
		size_t size = 0;
		if (fmt == 0x83F0) {
			size = pixels / 2;
		}
		else if (fmt == 0x8E8C) {
			size = pixels;
		}
		else {
			size = pixels * 4;
		}

		totalEstimatedTextureSize += size;
		totalEstimatedTextureSizeWithoutCompression += pixels * 4;

		switch (texUseType) {
			case ETextureType::WallPatch:
			case ETextureType::Wall:
				totalWallSize += size;
				break;
			case ETextureType::Flat:
				totalFlatSize += size;
				break;
			case ETextureType::Decal:
			case ETextureType::Sprite:
			case ETextureType::MiscPatch:
			case ETextureType::SkinSprite:
				totalSpriteSize += size;
				break;
			case ETextureType::Override:
			case ETextureType::Special:
			case ETextureType::Any:
				totalMiscSize += size;
				break;
			case ETextureType::FontChar:
				totalFontSize += size;
				break;
				
			default:
				Printf("  [%d]  ", texUseType);
				break;
		}


		// We can't compress level textures yet so skip these
		if (texUseType == ETextureType::Wall || texUseType == ETextureType::Flat || texUseType == ETextureType::WallPatch) continue;

		list.insert(&tx);
	}

	Printf("Estimated VRAM Total (Minus mips):\n   %.2fMB current VS %.2fMB without any compression\n", (double)totalEstimatedTextureSize / 1024.0 / 1024.0, (double)totalEstimatedTextureSizeWithoutCompression / 1024.0 / 1024.0);

	Printf("      Misc: %.2fMB\n", (double)totalMiscSize / 1024.0 / 1024.0);
	Printf("     Walls: %.2fMB\n", (double)totalWallSize / 1024.0 / 1024.0);
	Printf("     Flats: %.2fMB\n", (double)totalFlatSize / 1024.0 / 1024.0);
	Printf("     Fonts: %.2fMB\n", (double)totalFontSize / 1024.0 / 1024.0);
	Printf("   Sprites: %.2fMB\n\n", (double)totalSpriteSize / 1024.0 / 1024.0);

	// Check the first (limit)
	int cnt = 0;
	if (limit > 0) {
		Printf("Possible Optimizations...\n");
		for (auto tx : list) {
			auto tex = tx->Texture;
			const char* name = tex->GetSourceLump() > 0 ? fileSystem.GetFileFullName(tex->GetSourceLump()) : tex->GetName().GetChars();
			bool shouldBC1 = !tex->GetTranslucency();
			bool shouldBC7 = tex->GetTranslucency();
			bool shouldShrink = (tex->GetTexelWidth() * tex->GetTexelHeight()) >= (1920 * 1080);
			if (name == 0) continue;

			bool isGPU = tex->GetTexture()->GetImage()->IsGPUOnly();
			int format = tex->GetTexture()->GetImage()->getGLFormat();
			bool shouldChangeCompression = (format == 0x83F0 && shouldBC7) || (format == 0x8E8C && shouldBC1);
			bool hasRecommendations = shouldShrink || !isGPU || shouldChangeCompression;

			if (recommendOnly && !hasRecommendations) continue;


			if (hasRecommendations) {
				Printf(TEXTCOLOR_RED "#%d  (%s) [%s]: [%d x %d]\n", cnt, name, getUseTypeName(tex->GetUseType()), tex->GetTexelWidth(), tex->GetTexelHeight());
				Printf("Recommendations: \n");
				if (!isGPU) Printf("   Encode with %s as .DDS file\n", shouldBC1 ? "BC1-RGB" : "BC7-ARGB");
				else if (shouldChangeCompression) Printf("\t\tChange encoding to %s\n", shouldBC1 ? "BC1-RGB or BC1a: Alpha channel not necessary, image is not translucent" : "BC7-ARGB: Image is translucent and requires more precision");
				if (shouldShrink) Printf("   Consider reducing resolution\n");
			}
			else {
				Printf("#%d  (%s): [%d x %d]\n", cnt, name, tex->GetTexelWidth(), tex->GetTexelHeight());
			}

			if (cnt++ >= limit) {
				break;
			}
		}
	}
}

//==========================================================================
//
// FTextureManager :: ListTextures
//
//==========================================================================

int FTextureManager::ListTextures (const char *name, TArray<FTextureID> &list, bool listall)
{
	int i;

	if (name == NULL || name[0] == '\0')
	{
		return 0;
	}
	// [RH] Doom counted anything beginning with '-' as "no texture".
	// Hopefully nobody made use of that and had textures like "-EMPTY",
	// because -NOFLAT- is a valid graphic for ZDoom.
	if (name[0] == '-' && name[1] == '\0')
	{
		return 0;
	}
	i = HashFirst[MakeKey (name) % HASH_SIZE];

	while (i != HASH_END)
	{
		auto tex = Textures[i].Texture;

		if (tex->GetName().CompareNoCase(name) == 0)
		{
			auto texUseType = tex->GetUseType();
			// NULL textures must be ignored.
			if (texUseType!=ETextureType::Null) 
			{
				unsigned int j = list.Size();
				if (!listall)
				{
					for (j = 0; j < list.Size(); j++)
					{
						// Check for overriding definitions from newer WADs
						if (Textures[list[j].GetIndex()].Texture->GetUseType() == texUseType) break;
					}
				}
				if (j==list.Size()) list.Push(FTextureID(i));
			}
		}
		i = Textures[i].HashNext;
	}
	return list.Size();
}

//==========================================================================
//
// FTextureManager :: GetTextures
//
//==========================================================================

FTextureID FTextureManager::GetTextureID (const char *name, ETextureType usetype, BITFIELD flags)
{
	FTextureID i;

	if (name == NULL || name[0] == 0)
	{
		return FTextureID(0);
	}
	else
	{
		i = CheckForTexture (name, usetype, flags | TEXMAN_TryAny);
	}

	if (!i.Exists())
	{
		// Use a default texture instead of aborting like Doom did
		Printf ("Unknown texture: \"%s\"\n", name);
		i = DefaultTexture;
	}
	return FTextureID(i);
}

//==========================================================================
//
// FTextureManager :: FindTexture
//
//==========================================================================

FGameTexture *FTextureManager::FindGameTexture(const char *texname, ETextureType usetype, BITFIELD flags)
{
	FTextureID texnum = CheckForTexture (texname, usetype, flags);
	return GetGameTexture(texnum.GetIndex());
}

//==========================================================================
//
// FTextureManager :: OkForLocalization
//
//==========================================================================

bool FTextureManager::OkForLocalization(FTextureID texnum, const char *substitute, int locmode)
{
	uint32_t langtable = 0;
	if (*substitute == '$') substitute = GStrings.CheckString(substitute+1, &langtable);
	else return true;	// String literals from the source data should never override graphics from the same definition.
	if (substitute == nullptr) return true;	// The text does not exist.

	// Modes 2, 3 and 4 must not replace localized textures.
	int localizedTex = ResolveLocalizedTexture(texnum.GetIndex());
	if (localizedTex != texnum.GetIndex()) return true;	// Do not substitute a localized variant of the graphics patch.

	// For mode 4 we are done now.
	if (locmode == 4) return false;

	// Mode 2 and 3 must reject any text replacement from the default language tables.
	if ((langtable & MAKE_ID(255,0,0,0)) == MAKE_ID('*', 0, 0, 0)) return true;	// Do not substitute if the string comes from the default table.
	if (locmode == 2) return false;

	// Mode 3 must also reject substitutions for non-IWAD content.
	int file = fileSystem.GetFileContainer(Textures[texnum.GetIndex()].Texture->GetSourceLump());
	if (file > fileSystem.GetMaxIwadNum()) return true;

	return false;
}

//==========================================================================
//
// FTextureManager :: AddTexture
//
//==========================================================================

FTextureID FTextureManager::AddGameTexture (FGameTexture *texture, bool addtohash)
{
	int bucket;
	int hash;

	if (texture == NULL) return FTextureID(-1);

	if (texture->GetTexture())
	{
		// Later textures take precedence over earlier ones
		calcShouldUpscale(texture);	// calculate this once at insertion
	}

	// Textures without name can't be looked for
	if (addtohash && texture->GetName().IsNotEmpty())
	{
		bucket = int(MakeKey (texture->GetName().GetChars()) % HASH_SIZE);
		hash = HashFirst[bucket];
	}
	else
	{
		bucket = -1;
		hash = -1;
	}

	TextureDescriptor hasher = { texture, -1, -1, -1, hash };
	int trans = Textures.Push (hasher);
	Translation.Push (trans);
	if (bucket >= 0) HashFirst[bucket] = trans;
	auto id = FTextureID(trans);
	texture->SetID(id);
	return id;
}

//==========================================================================
//
// FTextureManager :: CreateTexture
//
// Calls FTexture::CreateTexture and adds the texture to the manager.
//
//==========================================================================

FTextureID FTextureManager::CreateTexture (int lumpnum, ETextureType usetype)
{
	if (lumpnum != -1)
	{
		FString str;
		if (!usefullnames)
			str = fileSystem.GetFileShortName(lumpnum);
		else
		{
			auto fn = fileSystem.GetFileFullName(lumpnum);
			str = ExtractFileBase(fn);
		}
		auto out = MakeGameTexture(CreateTextureFromLump(lumpnum, usetype == ETextureType::Flat), str.GetChars(), usetype);

		if (out != NULL)
		{
			if (usetype == ETextureType::Flat)
			{
				int w = out->GetTexelWidth();
				int h = out->GetTexelHeight();

				// Auto-scale flats with dimensions 128x128 and 256x256.
				// In hindsight, a bad idea, but RandomLag made it sound better than it really is.
				// Now we're stuck with this stupid behaviour.
				if (w == 128 && h == 128)
				{
					out->SetScale(2, 2);
					out->SetWorldPanning(true);
				}
				else if (w == 256 && h == 256)
				{
					out->SetScale(4, 4);
					out->SetWorldPanning(true);
				}
			}

			return AddGameTexture(out);
		}
		else
		{
			Printf (TEXTCOLOR_ORANGE "Invalid data encountered for texture %s\n", fileSystem.GetFileFullPath(lumpnum).c_str());
			return FTextureID(-1);
		}
	}
	return FTextureID(-1);
}

//==========================================================================
//
// FTextureManager :: ReplaceTexture
//
//==========================================================================

void FTextureManager::ReplaceTexture (FTextureID texid, FGameTexture *newtexture, bool free)
{
	int index = texid.GetIndex();
	if (unsigned(index) >= Textures.Size())
		return;

	if (newtexture->GetTexture())
	{
		calcShouldUpscale(newtexture);	// calculate this once at insertion
	}

	auto oldtexture = Textures[index].Texture;

	newtexture->SetName(oldtexture->GetName().GetChars());
	newtexture->SetUseType(oldtexture->GetUseType());
	Textures[index].Texture = newtexture;
	newtexture->SetID(oldtexture->GetID());
	oldtexture->SetName("");
	AddGameTexture(oldtexture);
}

//==========================================================================
//
// FTextureManager :: AreTexturesCompatible
//
// Checks if 2 textures are compatible for a ranged animation
//
//==========================================================================

bool FTextureManager::AreTexturesCompatible (FTextureID picnum1, FTextureID picnum2)
{
	int index1 = picnum1.GetIndex();
	int index2 = picnum2.GetIndex();
	if (unsigned(index1) >= Textures.Size() || unsigned(index2) >= Textures.Size())
		return false;

	auto texture1 = Textures[index1].Texture;
	auto texture2 = Textures[index2].Texture;

	// both textures must be the same type.
	if (texture1 == NULL || texture2 == NULL || texture1->GetUseType() != texture2->GetUseType())
		return false;

	// both textures must be from the same file
	for(unsigned i = 0; i < FirstTextureForFile.Size() - 1; i++)
	{
		if (index1 >= FirstTextureForFile[i] && index1 < FirstTextureForFile[i+1])
		{
			return (index2 >= FirstTextureForFile[i] && index2 < FirstTextureForFile[i+1]);
		}
	}
	return false;
}


//==========================================================================
//
// FTextureManager :: AddGroup
//
//==========================================================================

void FTextureManager::AddGroup(int wadnum, int ns, ETextureType usetype)
{
	int firsttx = fileSystem.GetFirstEntry(wadnum);
	int lasttx = fileSystem.GetLastEntry(wadnum);

	if (!usefullnames)
	{
		// Go from first to last so that ANIMDEFS work as expected. However,
		// to avoid duplicates (and to keep earlier entries from overriding
		// later ones), the texture is only inserted if it is the one returned
		// by doing a check by name in the list of wads.

		for (; firsttx <= lasttx; ++firsttx)
		{
			auto Name = fileSystem.GetFileShortName(firsttx);
			if (fileSystem.GetFileNamespace(firsttx) == ns)
			{
				if (fileSystem.CheckNumForName(Name, ns) == firsttx)
				{
					CreateTexture(firsttx, usetype);
				}
				progressFunc();
			}
			else if (ns == ns_flats && fileSystem.GetFileFlags(firsttx) & RESFF_MAYBEFLAT)
			{
				if (fileSystem.CheckNumForName(Name, ns) < firsttx)
				{
					CreateTexture(firsttx, usetype);
				}
				progressFunc();
			}
		}
	}
	else
	{
		// The duplicate check does not work with this (yet.)
		for (; firsttx <= lasttx; ++firsttx)
		{
			if (fileSystem.GetFileNamespace(firsttx) == ns)
			{
				CreateTexture(firsttx, usetype);
			}
		}
	}
}

//==========================================================================
//
// Adds all hires texture definitions.
//
//==========================================================================

void FTextureManager::AddHiresTextures (int wadnum)
{
	int firsttx = fileSystem.GetFirstEntry(wadnum);
	int lasttx = fileSystem.GetLastEntry(wadnum);

	TArray<FTextureID> tlist;

	if (firsttx == -1 || lasttx == -1)
	{
		return;
	}

	for (;firsttx <= lasttx; ++firsttx)
	{
		if (fileSystem.GetFileNamespace(firsttx) == ns_hires)
		{
			auto Name = fileSystem.GetFileShortName(firsttx);

			if (fileSystem.CheckNumForName (Name, ns_hires) == firsttx)
			{
				tlist.Clear();
				int amount = ListTextures(Name, tlist);
				if (amount == 0)
				{
					// A texture with this name does not yet exist
					auto newtex = MakeGameTexture(CreateTextureFromLump(firsttx), Name, ETextureType::Override);
					if (newtex != NULL)
					{
						AddGameTexture(newtex);
					}
				}
				else
				{
					for(unsigned int i = 0; i < tlist.Size(); i++)
					{
						FTexture * newtex = CreateTextureFromLump(firsttx);
						if (newtex != NULL)
						{
							auto oldtex = Textures[tlist[i].GetIndex()].Texture;

							// Replace the entire texture and adjust the scaling and offset factors.
							auto gtex = MakeGameTexture(newtex, nullptr, ETextureType::Override);
							gtex->SetWorldPanning(true);
							gtex->SetDisplaySize(oldtex->GetDisplayWidth(), oldtex->GetDisplayHeight());
							double xscale1 = oldtex->GetTexelLeftOffset(0) * gtex->GetScaleX() / oldtex->GetScaleX();
							double xscale2 = oldtex->GetTexelLeftOffset(1) * gtex->GetScaleX() / oldtex->GetScaleX();
							double yscale1 = oldtex->GetTexelTopOffset(0) * gtex->GetScaleY() / oldtex->GetScaleY();
							double yscale2 = oldtex->GetTexelTopOffset(1) * gtex->GetScaleY() / oldtex->GetScaleY();
							gtex->SetOffsets(0, xs_RoundToInt(xscale1), xs_RoundToInt(yscale1));
							gtex->SetOffsets(1, xs_RoundToInt(xscale2), xs_RoundToInt(yscale2));
							ReplaceTexture(tlist[i], gtex, true);
						}
					}
				}
				progressFunc();
			}
		}
	}
}

//==========================================================================
//
// Loads the HIRESTEX lumps
//
//==========================================================================

void FTextureManager::LoadTextureDefs(int wadnum, const char *lumpname, FMultipatchTextureBuilder &build)
{
	int texLump, lastLump;

	lastLump = 0;

	while ((texLump = fileSystem.FindLump(lumpname, &lastLump)) != -1)
	{
		if (fileSystem.GetFileContainer(texLump) == wadnum)
		{
			ParseTextureDef(texLump, build);
		}
	}
}

void FTextureManager::ParseTextureDef(int lump, FMultipatchTextureBuilder &build)
{
	TArray<FTextureID> tlist;

	FScanner sc(lump);
	while (sc.GetString())
	{
		if (sc.Compare("remap")) // remap an existing texture
		{
			sc.MustGetString();

			// allow selection by type
			int mode;
			ETextureType type;
			if (sc.Compare("wall")) type=ETextureType::Wall, mode=FTextureManager::TEXMAN_Overridable;
			else if (sc.Compare("flat")) type=ETextureType::Flat, mode=FTextureManager::TEXMAN_Overridable;
			else if (sc.Compare("sprite")) type=ETextureType::Sprite, mode=0;
			else type = ETextureType::Any, mode = 0;

			if (type != ETextureType::Any) sc.MustGetString();

			sc.String[8]=0;

			tlist.Clear();
			ListTextures(sc.String, tlist);
			FName texname = sc.String;

			sc.MustGetString();
			int lumpnum = fileSystem.CheckNumForFullName(sc.String, true, ns_patches);
			if (lumpnum == -1) lumpnum = fileSystem.CheckNumForFullName(sc.String, true, ns_graphics);

			if (tlist.Size() == 0)
			{
				Printf("Attempting to remap non-existent texture %s to %s\n",
					texname.GetChars(), sc.String);
			}
			else if (lumpnum == -1)
			{
				Printf("Attempting to remap texture %s to non-existent lump %s\n",
					texname.GetChars(), sc.String);
			}
			else
			{
				for(unsigned int i = 0; i < tlist.Size(); i++)
				{
					auto oldtex = Textures[tlist[i].GetIndex()].Texture;
					int sl;

					// only replace matching types. For sprites also replace any MiscPatches
					// based on the same lump. These can be created for icons.
					if (oldtex->GetUseType() == type || type == ETextureType::Any ||
						(mode == TEXMAN_Overridable && oldtex->GetUseType() == ETextureType::Override) ||
						(type == ETextureType::Sprite && oldtex->GetUseType() == ETextureType::MiscPatch &&
						(sl=oldtex->GetSourceLump()) >= 0 && fileSystem.GetFileNamespace(sl) == ns_sprites)
						)
					{
						FTexture * newtex = CreateTextureFromLump(lumpnum);
						if (newtex != NULL)
						{
							// Replace the entire texture and adjust the scaling and offset factors.
							auto gtex = MakeGameTexture(newtex, nullptr, ETextureType::Override);
							gtex->SetWorldPanning(true);
							gtex->SetDisplaySize(oldtex->GetDisplayWidth(), oldtex->GetDisplayHeight());
							double xscale1 = oldtex->GetTexelLeftOffset(0) * gtex->GetScaleX() / oldtex->GetScaleX();
							double xscale2 = oldtex->GetTexelLeftOffset(1) * gtex->GetScaleX() / oldtex->GetScaleX();
							double yscale1 = oldtex->GetTexelTopOffset(0) * gtex->GetScaleY() / oldtex->GetScaleY();
							double yscale2 = oldtex->GetTexelTopOffset(1) * gtex->GetScaleY() / oldtex->GetScaleY();
							gtex->SetOffsets(0, xs_RoundToInt(xscale1), xs_RoundToInt(yscale1));
							gtex->SetOffsets(1, xs_RoundToInt(xscale2), xs_RoundToInt(yscale2));
							ReplaceTexture(tlist[i], gtex, true);
						}
					}
				}
			}
		}
		else if (sc.Compare("define")) // define a new "fake" texture
		{
			sc.GetString();

			FString base = ExtractFileBase(sc.String, false);
			if (!base.IsEmpty())
			{
				FString src = base.Left(8);

				int lumpnum = fileSystem.CheckNumForFullName(sc.String, true, ns_patches);
				if (lumpnum == -1) lumpnum = fileSystem.CheckNumForFullName(sc.String, true, ns_graphics);

				sc.GetString();
				bool is32bit = !!sc.Compare("force32bit");
				if (!is32bit) sc.UnGet();

				sc.MustGetNumber();
				int width = sc.Number;
				sc.MustGetNumber();
				int height = sc.Number;

				if (lumpnum>=0)
				{
					auto newtex = MakeGameTexture(CreateTextureFromLump(lumpnum), src.GetChars(), ETextureType::Override);

					if (newtex != NULL)
					{
						// Replace the entire texture and adjust the scaling and offset factors.
						newtex->SetWorldPanning(true);
						newtex->SetDisplaySize((float)width, (float)height);

						FTextureID oldtex = TexMan.CheckForTexture(src.GetChars(), ETextureType::MiscPatch);
						if (oldtex.isValid()) 
						{
							ReplaceTexture(oldtex, newtex, true);
							newtex->SetUseType(ETextureType::Override);
						}
						else AddGameTexture(newtex);
					}
				}
			}				
			//else Printf("Unable to define hires texture '%s'\n", tex->Name);
		}
		else if (sc.Compare("notrim"))
		{
			sc.MustGetString();

			FTextureID id = TexMan.CheckForTexture(sc.String, ETextureType::Sprite);
			if (id.isValid())
			{
				FGameTexture *tex = TexMan.GetGameTexture(id);

				if (tex)	tex->SetNoTrimming(true);
				else		sc.ScriptError("NoTrim: %s not found", sc.String);
			}
			else
				sc.ScriptError("NoTrim: %s is not a sprite", sc.String);

		}
		else if (sc.Compare("texture"))
		{
			build.ParseTexture(sc, ETextureType::Override, lump);
		}
		else if (sc.Compare("sprite"))
		{
			build.ParseTexture(sc, ETextureType::Sprite, lump);
		}
		else if (sc.Compare("walltexture"))
		{
			build.ParseTexture(sc, ETextureType::Wall, lump);
		}
		else if (sc.Compare("flat"))
		{
			build.ParseTexture(sc, ETextureType::Flat, lump);
		}
		else if (sc.Compare("graphic"))
		{
			build.ParseTexture(sc, ETextureType::MiscPatch, lump);
		}
		else if (sc.Compare("#include"))
		{
			sc.MustGetString();

			// This is not using sc.Open because it can print a more useful error message when done here
			int includelump = fileSystem.CheckNumForFullName(sc.String, true);
			if (includelump == -1)
			{
				sc.ScriptError("Lump '%s' not found", sc.String);
			}
			else
			{
				ParseTextureDef(includelump, build);
			}
		}
		else if (sc.Compare("weaponsprite")) // @Cockatrice - Shortcut for defining offsets and scale for a weapon sprite. Probably Selaco specific
		{
			sc.SetCMode(true);
			sc.MustGetString();
			
			FString name(sc.String);
			name.ToUpper();

			int width = -1, height = -1;
			bool mips = false;

			if (sc.CheckString(",")) {
				sc.MustGetNumber();
				width = sc.Number;
				sc.MustGetStringName(",");
				sc.MustGetNumber();
				height = sc.Number;
			}

			// Confirm that we have a valid texture name here
			FTextureID texID = TexMan.CheckForTexture(name.GetChars(), ETextureType::Sprite, TEXMAN_Overridable);
			FGameTexture *tex = TexMan.GetGameTexture(texID, false);
			
			if (!texID.isValid() || tex == nullptr) {
				// Try full name search
				int wadnum = fileSystem.GetFileContainer(lump);
				int num = fileSystem.CheckNumForName(name.GetChars(), ns_sprites, wadnum, false);
				auto fullName = num < 0 ? nullptr : fileSystem.GetFileFullName(num);
				texID = TexMan.CheckForTexture(fullName, ETextureType::Sprite, TEXMAN_Overridable);
				tex = TexMan.GetGameTexture(texID, false);
			}

			if (!texID.isValid() || tex == nullptr) {
				sc.ScriptMessage("Warning: Unknown sprite: %s", name.GetChars());
			}

			double scalex = 3.0, scaley = 3.0;
			bool bWorldPanning = false, bNoTrim = false;
			bool offset2set = false;
			float LeftOffset[2] = { 0,0 };
			float TopOffset[2] = { 0,0 };

			if (sc.CheckString("{"))
			{
				while (!sc.CheckString("}"))
				{
					sc.MustGetString();
					if (sc.Compare("Scale"))
					{
						sc.MustGetFloat();
						scalex = sc.Float;
						sc.MustGetStringName(",");
						scaley = sc.Float;
						if (scalex == 0 || scaley == 0) sc.ScriptError("Texture %s is defined with null scale\n", name.GetChars());
					}
					else if (sc.Compare("XScale"))
					{
						sc.MustGetFloat();
						scalex = sc.Float;
						if (scalex == 0) sc.ScriptError("Texture %s is defined with null x-scale\n", name.GetChars());
					}
					else if (sc.Compare("YScale"))
					{
						sc.MustGetFloat();
						scaley = sc.Float;
						if (scaley == 0) sc.ScriptError("Texture %s is defined with null y-scale\n", name.GetChars());
					}
					else if (sc.Compare("WorldPanning"))
					{
						bWorldPanning = true;
					}
					else if (sc.Compare("NoTrim"))
					{
						bNoTrim = true;
					}
					else if (sc.Compare("NoMips")) {
						mips = false;
					}
					else if (sc.Compare("Mips")) {
						mips = true;
					}
					else if (sc.Compare("Offset"))
					{
						sc.MustGetFloat();
						LeftOffset[0] = sc.Float;
						sc.MustGetStringName(",");
						sc.MustGetFloat();
						TopOffset[0] = sc.Float;
						if (!offset2set)
						{
							LeftOffset[1] = LeftOffset[0];
							TopOffset[1] = TopOffset[0];
						}
					}
					else if (sc.Compare("Offset2"))
					{
						sc.MustGetFloat();
						LeftOffset[1] = sc.Float;
						sc.MustGetStringName(",");
						sc.MustGetFloat();
						TopOffset[1] = sc.Float;
						offset2set = true;
					}
					else
					{
						sc.ScriptError("Unknown WeaponSprite property '%s'", sc.String);
					}
				}
			}

			if (tex != nullptr) {
				// Don't expand weaponsprites
				// Don't trim them either
				tex->SetNeverExpand(true);

				// @Cockatrice - This is a huge hack but let's detect if we are working with reduced resolution sprites
				// We handle scaling specially, mostly compensating for floating point adjustment in offsets/size
				// This will almost exclusively be used for half-res weapon sprites
				int tw = tex->GetTexelWidth();
				int th = tex->GetTexelHeight();
				bool sizeMatch = (width == tw && height == th);

				if (width > 0 && height > 0 && !sizeMatch && (tw <= width && th <= height)) {
					//if (sizeMatch || (tw > width && th > height)) {
					//	tex->SetDisplaySize(width / scalex, height / scaley);
					//}
					//else {
						// Quick hack
						// TODO: Handle factors != 2
						tex->SetDisplaySize(
							(width % 2) != 0 ? (width - 1) / scalex : width / scalex,
							(height % 2) != 0 ? (height - 1) / scaley : height / scaley
						);
						tex->SetOffsets(0, tex->GetScaleX() / scalex * LeftOffset[0], tex->GetScaleY() / scaley * TopOffset[0]);
						tex->SetOffsets(1, tex->GetScaleX() / scalex * LeftOffset[1], tex->GetScaleY() / scaley * TopOffset[1]);
					//}
				}
				else {
					if (width > 0 && height > 0) tex->SetSize(width, height);
					tex->SetOffsets(0, LeftOffset[0], TopOffset[0]);
					tex->SetOffsets(1, LeftOffset[1], TopOffset[1]);
					tex->SetScale((float)scalex, (float)scaley);
				}

				// If width and height do not match, adjust offsets by the difference
				/*if (!sizeMatch && width > 0 && height > 0) {
					tex->SetOffsets(0, tex->GetScaleX() / scalex * LeftOffset[0], tex->GetScaleY() / scaley * TopOffset[0]);
					tex->SetOffsets(1, tex->GetScaleX() / scalex * LeftOffset[1], tex->GetScaleY() / scaley * TopOffset[1]);
				}
				else {
					tex->SetOffsets(0, LeftOffset[0], TopOffset[0]);
					tex->SetOffsets(1, LeftOffset[1], TopOffset[1]);
				}*/

				/*if (width > 0 && height > 0) {
					tex->SetDisplaySize(
						width % 2 == 0 ? width / scalex : (width - 1) / scalex,
						height % 2 == 0 ? height / scalex : (height - 1) / scaley
					);
				}
				else {
					tex->SetScale((float)scalex, (float)scaley);
					tex->SetOffsets(0, LeftOffset[0], TopOffset[0]);
					tex->SetOffsets(1, LeftOffset[1], TopOffset[1]);
				}

				// If width and height do not match, adjust offsets by the difference
				if (width > 0 && height > 0 && (tex->GetTexelWidth() != width || tex->GetTexelHeight() != height)) {
					//tex->SetOffsets(0, tex->GetScaleX() / scalex * LeftOffset[0], tex->GetScaleY() / scaley * TopOffset[0]);
					//tex->SetOffsets(1, tex->GetScaleX() / scalex * LeftOffset[1], tex->GetScaleY() / scaley * TopOffset[1]);
					// HACK ALERT! Assuming we have half-res sprites here for now
					tex->SetOffsets(0, LeftOffset[0] * 0.5, TopOffset[0] * 0.5);
					tex->SetOffsets(1, LeftOffset[1] * 0.5, TopOffset[1] * 0.5);
				}
				else {
					tex->SetOffsets(0, LeftOffset[0], TopOffset[0]);
					tex->SetOffsets(1, LeftOffset[1], TopOffset[1]);
				}*/

				tex->SetWorldPanning(bWorldPanning);
				tex->SetNoTrimming(bNoTrim);
				tex->SetNoMipmaps(!mips);
			}
			

			sc.SetCMode(false);
		}
		else
		{
			sc.ScriptError("Texture definition expected, found '%s'", sc.String);
		}
	}
}

//==========================================================================
//
// FTextureManager :: AddPatches
//
//==========================================================================

void FTextureManager::AddPatches (int lumpnum)
{
	auto file = fileSystem.ReopenFileReader (lumpnum, true);
	uint32_t numpatches, i;
	char name[9];

	numpatches = file.ReadUInt32();
	name[8] = '\0';

	for (i = 0; i < numpatches; ++i)
	{
		file.Read (name, 8);

		if (CheckForTexture (name, ETextureType::WallPatch, 0) == -1)
		{
			CreateTexture (fileSystem.CheckNumForName (name, ns_patches), ETextureType::WallPatch);
		}
		progressFunc();
	}
}


//==========================================================================
//
// FTextureManager :: LoadTexturesX
//
// Initializes the texture list with the textures from the world map.
//
//==========================================================================

void FTextureManager::LoadTextureX(int wadnum, FMultipatchTextureBuilder &build)
{
	// Use the most recent PNAMES for this WAD.
	// Multiple PNAMES in a WAD will be ignored.
	int pnames = fileSystem.CheckNumForName("PNAMES", ns_global, wadnum, false);

	if (pnames < 0)
	{
		// should never happen except for zdoom.pk3
		return;
	}

	// Only add the patches if the PNAMES come from the current file
	// Otherwise they have already been processed.
	if (fileSystem.GetFileContainer(pnames) == wadnum) TexMan.AddPatches (pnames);

	int texlump1 = fileSystem.CheckNumForName ("TEXTURE1", ns_global, wadnum);
	int texlump2 = fileSystem.CheckNumForName ("TEXTURE2", ns_global, wadnum);
	build.AddTexturesLumps (texlump1, texlump2, pnames);
}


int FTextureManager::ParseBatchTextureDef(int lump, int wadnum) {
	int total = 0, lineCnt = 0;

	auto reader = fileSystem.OpenFileReader(lump);
	char buf[1800];

	auto lastPos = reader.Tell();
	while (reader.Gets(buf, 1800)) {
		// Every line is a definition
		int type = 0, fileType = -1;
		char id[9], path[1024];
		path[0] = '\0';

		int count = sscanf(buf,
			"%d:%8[^:]:%1023[^:]:%d",
			&fileType, id, path, &type
		);

		lineCnt++;

		if (count == 4 && fileType >= 0) {
			int lumpnum = -1;
			if(strnlen(path, 1023) > 1) lumpnum = fileSystem.CheckNumForFullName(path, wadnum);
			else lumpnum = fileSystem.CheckNumForName(path, wadnum);

			if (lumpnum >= 0)
			{
				// Rewind so this line can be read again by the image reader
				reader.Seek(lastPos, FileReader::ESeek::SeekSet);

				bool hasMoreInfo = false;
				auto image = FImageSource::CreateImageFromDef(reader, fileType, lumpnum, &hasMoreInfo);
				auto newtex = image == nullptr ? nullptr : MakeGameTexture(new FImageTexture(image), id, (ETextureType)type);

				if (newtex != NULL)
				{
					// Replace the entire texture and adjust the scaling and offset factors.
					//newtex->SetWorldPanning(true);
					newtex->SetDisplaySize((float)image->GetWidth(), (float)image->GetHeight());

					FTextureID oldtex = TexMan.CheckForTexture(id, (ETextureType)type);
					if (oldtex.isValid())
					{
						ReplaceTexture(oldtex, newtex, true);
						newtex->SetUseType(ETextureType::Override);
					}
					else {
						AddGameTexture(newtex);
					}

					progressFunc();
					total++;
				}
				else {
					Printf("Failed to create texture for %s (%s)\n", id, path);
				}

				
				// Always read extra info to keep the stream intact, even if we couldn't create the texture
				if (hasMoreInfo) {
					image->DeSerializeExtraDataFromTextureDef(reader, newtex);
					lineCnt += 2;	// Cheating, we don't know for sure the extra info is 2 lines because it could be anything, but currently it's always 2 lines
				}
			}
			else {
				Printf("Texture can no longer be found: %s (%s)\n", id, path);
			}
		}
		else {
			if(fileType != -1)	// -1 is either SPI data or deliberate ignore line, so don't error on that
				Printf("Bad line in TEXTURDEF at line %d: %s", lineCnt, buf);
		}

		lastPos = reader.Tell();
	}

	return total;
}

// @Cockatrice - Load a TEXTURDEF file, containing all textures for a WAD so we can skip the scanning phase
int FTextureManager::LoadTextureDefsForWad(int wadnum) {
	int remapLump, lastLump;

	lastLump = 0;

	int total = 0;

	while ((remapLump = fileSystem.FindLump("TEXTURDEF", &lastLump)) != -1)
	{
		if (fileSystem.GetFileContainer(remapLump) == wadnum)
		{
			total += ParseBatchTextureDef(remapLump, wadnum);
		}
	}

	return total;
}


//==========================================================================
//
// FTextureManager :: AddTexturesForWad
//
//==========================================================================

void FTextureManager::AddTexturesForWad(int wadnum, FMultipatchTextureBuilder& build)
{
	int firsttexture = Textures.Size();
	bool iwad = wadnum >= fileSystem.GetIwadNum() && wadnum <= fileSystem.GetMaxIwadNum();

	FirstTextureForFile.Push(firsttexture);

	bool writeCache = Args->CheckParm("-writetexturecache");
	bool defsLoaded = !writeCache && LoadTextureDefsForWad(wadnum) > 0;

	// Check if the wad has pre-defined textures
	if (!defsLoaded) {

		// First step: Load sprites
		AddGroup(wadnum, ns_sprites, ETextureType::Sprite);

		// When loading a Zip, all graphics in the patches/ directory should be
		// added as well.
		AddGroup(wadnum, ns_patches, ETextureType::WallPatch);

		// Second step: TEXTUREx lumps
		LoadTextureX(wadnum, build);

		// Third step: Flats
		AddGroup(wadnum, ns_flats, ETextureType::Flat);

		// Fourth step: Textures (TX_)
		AddGroup(wadnum, ns_newtextures, ETextureType::Override);

		// Sixth step: Try to find any lump in the WAD that may be a texture and load as a TEX_MiscPatch
		int firsttx = fileSystem.GetFirstEntry(wadnum);
		int lasttx = fileSystem.GetLastEntry(wadnum);

		for (int i = firsttx; i <= lasttx; i++)
		{
			bool skin = false;
			const char *Name = fileSystem.GetFileShortName(i);

			// Ignore anything not in the global namespace
			int ns = fileSystem.GetFileNamespace(i);
			if (ns == ns_global)
			{
				// In Zips all graphics must be in a separate namespace.
				if (fileSystem.GetFileFlags(i) & RESFF_FULLPATH) continue;

				// Ignore lumps with empty names.
				if (fileSystem.CheckFileName(i, "")) continue;

				// Ignore anything belonging to a map
				if (fileSystem.CheckFileName(i, "THINGS")) continue;
				if (fileSystem.CheckFileName(i, "LINEDEFS")) continue;
				if (fileSystem.CheckFileName(i, "SIDEDEFS")) continue;
				if (fileSystem.CheckFileName(i, "VERTEXES")) continue;
				if (fileSystem.CheckFileName(i, "SEGS")) continue;
				if (fileSystem.CheckFileName(i, "SSECTORS")) continue;
				if (fileSystem.CheckFileName(i, "NODES")) continue;
				if (fileSystem.CheckFileName(i, "SECTORS")) continue;
				if (fileSystem.CheckFileName(i, "REJECT")) continue;
				if (fileSystem.CheckFileName(i, "BLOCKMAP")) continue;
				if (fileSystem.CheckFileName(i, "BEHAVIOR")) continue;

				bool force = false;
				// Don't bother looking at this lump if something later overrides it.
				if (fileSystem.CheckNumForName(Name, ns_graphics) != i)
				{
					if (iwad)
					{
						// We need to make an exception for font characters of the SmallFont coming from the IWAD to be able to construct the original font.
						if (strncmp(Name, "STCFN", 5) != 0 && strncmp(Name, "FONTA", 5) != 0) continue;
						force = true;
					}
					else continue;
				}

				// skip this if it has already been added as a wall patch.
				if (!force && CheckForTexture(Name, ETextureType::WallPatch, 0).Exists()) continue;
			}
			else if (ns == ns_graphics)
			{
				if (fileSystem.CheckNumForName(Name, ns_graphics) != i)
				{
					if (iwad)
					{
						// We need to make an exception for font characters of the SmallFont coming from the IWAD to be able to construct the original font.
						if (strncmp(Name, "STCFN", 5) != 0 && strncmp(Name, "FONTA", 5) != 0) continue;
					}
					else continue;
				}
			}
			else if (ns >= ns_firstskin)
			{
				// Don't bother looking this lump if something later overrides it.
				if (fileSystem.CheckNumForName(Name, ns) != i) continue;
				skin = true;
			}
			else continue;

			// Try to create a texture from this lump and add it.
			// Unfortunately we have to look at everything that comes through here...
			auto out = MakeGameTexture(CreateTextureFromLump(i), Name, skin ? ETextureType::SkinGraphic : ETextureType::MiscPatch);

			if (out != NULL)
			{
				AddGameTexture(out);
			}
		}

	}
	else {
		build.skipRedefines = true;
	} // End check for predefined textures

	// Check for text based texture definitions
	LoadTextureDefs(wadnum, "TEXTURES", build);
	LoadTextureDefs(wadnum, "HIRESTEX", build);

	// Seventh step: Check for hires replacements.
	AddHiresTextures(wadnum);

	SortTexturesByType(firsttexture, Textures.Size());

	Printf(TEXTCOLOR_GOLD"Added %d textures for file %d\n", Textures.Size() - firsttexture, wadnum);

	if(!defsLoaded && writeCache) WriteCacheForWad(wadnum);
}


void FTextureManager::WriteCacheForWad(int wadnum) {
	// @Cockatrice - Save a temporary file with the deets
	FString fs;
	FString fn = fileSystem.GetResourceFileName(wadnum);
	if (fn.IndexOf("/") >= 0 || fn.IndexOf("\\") >= 0) {
		fn.Format("%d", wadnum);
	}
	fs.AppendFormat("TEXTURDEF.%s.txt", fn.GetChars());


	const char* nms[] =
	{
		"Any",
		"Wall",
		"Flat",
		"Sprite",
		"WallPatch",
		"Build",		// no longer used but needs to remain for ZScript
		"SkinSprite",
		"Decal",
		"MiscPatch",
		"FontChar",
		"Override",	// For patches between TX_START/TX_END
		"Autopage",	// Automap background - used to enable the use of FAutomapTexture
		"SkinGraphic",
		"Null",
		"FirstDefined",
		"Special",
		"SWCanvas",
	};

	int firsttexture = FirstTextureForFile[wadnum];
	int lasttexture = (int)FirstTextureForFile.Size() > wadnum + 1 ? FirstTextureForFile[wadnum + 1] : Textures.Size();

	if (firsttexture >= lasttexture) return;	// No textures, skip
	if (fn.CompareNoCase("game_support.pk3") == 0 || fn.CompareNoCase("gzdoom.pk3") == 0) return; // Skip known useless files

	FILE* f = fopen(fs.GetChars(), "w");

	for (int x = firsttexture; x < lasttexture; x++) {
		FGameTexture* tx = Textures[x].Texture;

		if (tx->GetName().Len() < 1 || !tx->GetTexture()) {
			//fprintf(f, "NO FILE FOR: %s", tx->GetName().GetChars());
			continue;
		}

		FImageTexture* img = dynamic_cast<FImageTexture*>(tx->GetTexture());


		int lump = tx->GetSourceLump();
		int useType = (int)tx->GetUseType();
		if (useType >= countof(nms) || useType < 0) useType = 8;
		const char* fullName = NULL;

		if (lump < 0 || img == nullptr || img->GetImage() == nullptr)
			continue;

		fullName = fileSystem.GetFileFullName(lump, false);

		if (fullName == NULL) continue;

		// Get image source, serialize image from image source
		FString name = tx->GetName();
		img->GetImage()->SerializeForTextureDef(f, name, useType, tx);
		progressFunc();
	}

	fclose(f);
}


void FTextureManager::WriteCache() {
	int wadcnt = fileSystem.GetNumWads();

	for (int wadnum = 0; wadnum < wadcnt; wadnum++) {
		WriteCacheForWad(wadnum);
	}
}

//==========================================================================
//
// FTextureManager :: SortTexturesByType
// sorts newly added textures by UseType so that anything defined
// in TEXTURES and HIRESTEX gets in its proper place.
//
//==========================================================================

void FTextureManager::SortTexturesByType(int start, int end)
{
	TArray<FGameTexture *> newtextures;

	// First unlink all newly added textures from the hash chain
	for (int i = 0; i < HASH_SIZE; i++)
	{
		while (HashFirst[i] >= start && HashFirst[i] != HASH_END)
		{
			HashFirst[i] = Textures[HashFirst[i]].HashNext;
		}
	}
	newtextures.Resize(end-start);
	for(int i=start; i<end; i++)
	{
		newtextures[i-start] = Textures[i].Texture;
	}
	Textures.Resize(start);
	Translation.Resize(start);

	static ETextureType texturetypes[] = {
		ETextureType::Sprite, ETextureType::Null, ETextureType::FirstDefined, 
		ETextureType::WallPatch, ETextureType::Wall, ETextureType::Flat, 
		ETextureType::Override, ETextureType::MiscPatch, ETextureType::SkinGraphic
	};

	for(unsigned int i=0;i<countof(texturetypes);i++)
	{
		for(unsigned j = 0; j<newtextures.Size(); j++)
		{
			if (newtextures[j] != NULL && newtextures[j]->GetUseType() == texturetypes[i])
			{
				AddGameTexture(newtextures[j]);
				newtextures[j] = NULL;
			}
		}
	}
	// This should never happen. All other UseTypes are only used outside
	for(unsigned j = 0; j<newtextures.Size(); j++)
	{
		if (newtextures[j] != NULL)
		{
			Printf("Texture %s has unknown type!\n", newtextures[j]->GetName().GetChars());
			AddGameTexture(newtextures[j]);
		}
	}
}

//==========================================================================
//
// FTextureManager :: AddLocalizedVariants
//
//==========================================================================

void FTextureManager::AddLocalizedVariants()
{
	std::vector<FolderEntry> content;
	fileSystem.GetFilesInFolder("localized/textures/", content, false);
	for (auto &entry : content)
	{
		FString name = entry.name;
		auto tokens = name.Split(".", FString::TOK_SKIPEMPTY);
		if (tokens.Size() == 2)
		{
			auto ext = tokens[1];
			// Do not interpret common extensions for images as language IDs.
			if (ext.CompareNoCase("png") == 0 || ext.CompareNoCase("jpg") == 0 || ext.CompareNoCase("gfx") == 0 || ext.CompareNoCase("tga") == 0 || ext.CompareNoCase("lmp") == 0)
			{
				Printf("%s contains no language IDs and will be ignored\n", entry.name);
				continue;
			}
		}
		if (tokens.Size() >= 2)
		{
			FString base = ExtractFileBase(tokens[0].GetChars());
			FTextureID origTex = CheckForTexture(base.GetChars(), ETextureType::MiscPatch);
			if (origTex.isValid())
			{
				FTextureID tex = CheckForTexture(entry.name, ETextureType::MiscPatch);
				if (tex.isValid())
				{
					auto otex = GetGameTexture(origTex);
					auto ntex = GetGameTexture(tex);
					if (otex->GetDisplayWidth() != ntex->GetDisplayWidth() || otex->GetDisplayHeight() != ntex->GetDisplayHeight())
					{
						Printf("Localized texture %s must be the same size as the one it replaces\n", entry.name);
					}
					else
					{
						tokens[1].ToLower();
						auto langids = tokens[1].Split("-", FString::TOK_SKIPEMPTY);
						for (auto &lang : langids)
						{
							if (lang.Len() == 2 || lang.Len() == 3)
							{
								uint32_t langid = MAKE_ID(lang[0], lang[1], lang[2], 0);
								uint64_t comboid = (uint64_t(langid) << 32) | origTex.GetIndex();
								LocalizedTextures.Insert(comboid, tex.GetIndex());
								Textures[origTex.GetIndex()].Flags |= TEXFLAG_HASLOCALIZATION;
							}
							else
							{
								Printf("Invalid language ID in texture %s\n", entry.name);
							}
						}
					}
				}
				else
				{
					Printf("%s is not a texture\n", entry.name);
				}
			}
			else
			{
				Printf("Unknown texture %s for localized variant %s\n", tokens[0].GetChars(), entry.name);
			}
		}
		else
		{
			Printf("%s contains no language IDs and will be ignored\n", entry.name);
		}

	}
}

//==========================================================================
//
// FTextureManager :: Init
//
//==========================================================================
FGameTexture *CreateShaderTexture(bool, bool);
FImageSource* CreateEmptyTexture();

void FTextureManager::Init()
{
	DeleteAll();

	// Add all the static content 
	auto nulltex = MakeGameTexture(new FImageTexture(CreateEmptyTexture()), nullptr, ETextureType::Null);
	AddGameTexture(nulltex);

	// This is for binding to unused texture units, because accessing an unbound texture unit is undefined. It's a one pixel empty texture.
	auto emptytex = MakeGameTexture(new FImageTexture(CreateEmptyTexture()), nullptr, ETextureType::Override);
	emptytex->SetSize(1, 1);
	AddGameTexture(emptytex);

	// some special textures used in the game.
	AddGameTexture(CreateShaderTexture(false, false));
	AddGameTexture(CreateShaderTexture(false, true));
	AddGameTexture(CreateShaderTexture(true, false));
	AddGameTexture(CreateShaderTexture(true, true));
	// Add two animtexture entries so that movie playback can call functions using texture IDs.
	auto mt = MakeGameTexture(new AnimTexture(), "AnimTextureFrame1", ETextureType::Override);
	mt->SetUpscaleFlag(false, true);
	AddGameTexture(mt);
	mt = MakeGameTexture(new AnimTexture(), "AnimTextureFrame2", ETextureType::Override);
	mt->SetUpscaleFlag(false, true);
	AddGameTexture(mt);
}

void FTextureManager::AddTextures(void (*progressFunc_)(), void (*checkForHacks)(BuildInfo&), void (*customtexturehandler)())
{
	cycle_t texture_time = cycle_t();
	texture_time.Clock();

	progressFunc = progressFunc_;
	//if (BuildTileFiles.Size() == 0) CountBuildTiles ();

	int wadcnt = fileSystem.GetNumWads();

	FMultipatchTextureBuilder build(*this, progressFunc_, checkForHacks);

	for(int i = 0; i< wadcnt; i++)
	{
		AddTexturesForWad(i, build);
	}
	build.ResolveAllPatches();

	// Add one marker so that the last WAD is easier to handle and treat
	// custom textures as a completely separate block.
	FirstTextureForFile.Push(Textures.Size());
	if (customtexturehandler) customtexturehandler();
	FirstTextureForFile.Push(Textures.Size());

	DefaultTexture = CheckForTexture ("-NOFLAT-", ETextureType::Override, 0);

	InitPalettedVersions();
	AdjustSpriteOffsets();
	// Add auto materials to each texture after everything has been set up.
	// Textures array can be reallocated in process, so ranged for loop is not suitable.
	// There is no need to process discovered material textures here,
	// CheckForTexture() did this already.
	for (unsigned int i = 0, count = Textures.Size(); i < count; ++i)
	{
		Textures[i].Texture->AddAutoMaterials();
	}

	glPart2 = TexMan.CheckForTexture("glstuff/glpart2.png", ETextureType::MiscPatch);
	glPart = TexMan.CheckForTexture("glstuff/glpart.png", ETextureType::MiscPatch);
	mirrorTexture = TexMan.CheckForTexture("glstuff/mirror.png", ETextureType::MiscPatch);
	AddLocalizedVariants();

	// Make sure all ID's are correct by resetting them here to the proper index.
	for (unsigned int i = 0, count = Textures.Size(); i < count; ++i)
	{
		Textures[i].Texture->SetID(i);
	}

	texture_time.Unclock();
	Printf(TEXTCOLOR_GOLD"Texture Indexing: %.2fms\n", texture_time.TimeMS());
}

//==========================================================================
//
// FTextureManager :: InitPalettedVersions
//
//==========================================================================

void FTextureManager::InitPalettedVersions()
{
	int lump, lastlump = 0;

	while ((lump = fileSystem.FindLump("PALVERS", &lastlump)) != -1)
	{
		FScanner sc(lump);

		while (sc.GetString())
		{
			FTextureID pic1 = CheckForTexture(sc.String, ETextureType::Any);
			if (!pic1.isValid())
			{
				sc.ScriptMessage("Unknown texture %s to replace", sc.String);
			}
			sc.MustGetString();
			FTextureID pic2 = CheckForTexture(sc.String, ETextureType::Any);
			if (!pic2.isValid())
			{
				sc.ScriptMessage("Unknown texture %s to use as paletted replacement", sc.String);
			}
			if (pic1.isValid() && pic2.isValid())
			{
				Textures[pic1.GetIndex()].Paletted = pic2.GetIndex();
			}
		}
	}
}

//==========================================================================
//
// 
//
//==========================================================================

FTextureID FTextureManager::GetRawTexture(FTextureID texid, bool dontlookup)
{
	int texidx = texid.GetIndex();
	if ((unsigned)texidx >= Textures.Size()) return texid;
	if (Textures[texidx].RawTexture != -1) return FSetTextureID(Textures[texidx].RawTexture);
	if (dontlookup) return texid;

	// Reject anything that cannot have been a front layer for the sky in original Hexen, i.e. it needs to be an unscaled wall texture only using Doom patches.
	auto tex = Textures[texidx].Texture;
	auto ttex = tex->GetTexture();
	auto image = ttex->GetImage();
	// Reject anything that cannot have been a single-patch multipatch texture in vanilla.
	if (image == nullptr || image->IsRawCompatible() || tex->GetUseType() != ETextureType::Wall || ttex->GetWidth() != tex->GetDisplayWidth() ||
		ttex->GetHeight() != tex->GetDisplayHeight())
	{
		Textures[texidx].RawTexture = texidx;
		return texid;
	}

	// Let the hackery begin
	auto mptimage = static_cast<FMultiPatchTexture*>(image);
	auto source = mptimage->GetImageForPart(0);

	// Size must match for this to work as intended
	if (source->GetWidth() != ttex->GetWidth() || source->GetHeight() != ttex->GetHeight())
	{
		Textures[texidx].RawTexture = texidx;
		return texid;
	}

	// Todo: later this can just link to the already existing texture for this source graphic, once it can be retrieved through the image's SourceLump index
	auto RawTexture = MakeGameTexture(new FImageTexture(source), nullptr, ETextureType::Wall);
	texid = TexMan.AddGameTexture(RawTexture);
	Textures[texidx].RawTexture = texid.GetIndex();
	Textures[texid.GetIndex()].RawTexture = texid.GetIndex();
	return texid;
}


//==========================================================================
//
// Same shit for a different hack, this time Hexen's front sky layers.
//
//==========================================================================

FTextureID FTextureManager::GetFrontSkyLayer(FTextureID texid)
{
	int texidx = texid.GetIndex();
	if ((unsigned)texidx >= Textures.Size()) return texid;
	if (Textures[texidx].FrontSkyLayer != -1) return FSetTextureID(Textures[texidx].FrontSkyLayer);

	// Reject anything that cannot have been a front layer for the sky in original Hexen, i.e. it needs to be an unscaled wall texture only using Doom patches.
	auto tex = Textures[texidx].Texture;
	auto ttex = tex->GetTexture();
	auto image = ttex->GetImage();
	if (image == nullptr || !image->SupportRemap0() || tex->GetUseType() != ETextureType::Wall || tex->useWorldPanning() || tex->GetTexelTopOffset() != 0 ||
		ttex->GetWidth() != tex->GetDisplayWidth() || ttex->GetHeight() != tex->GetDisplayHeight())
	{
		Textures[texidx].FrontSkyLayer = texidx;
		return texid;
	}

	// Set this up so that it serializes to the same info as the base texture - this is needed to restore it on load.
	// But do not link the new texture into the hash chain!
	auto itex = new FImageTexture(image);
	itex->SetNoRemap0();
	auto FrontSkyLayer = MakeGameTexture(itex, tex->GetName().GetChars(), ETextureType::Wall);
	FrontSkyLayer->SetUseType(tex->GetUseType());
	texid = TexMan.AddGameTexture(FrontSkyLayer, false);
	Textures[texidx].FrontSkyLayer = texid.GetIndex();
	Textures[texid.GetIndex()].FrontSkyLayer = texid.GetIndex();	// also let it refer to itself as its front sky layer, in case for repeated InitSkyMap calls.
	return texid;
}

//==========================================================================
//
//
//
//==========================================================================
EXTERN_CVAR(String, language)

int FTextureManager::ResolveLocalizedTexture(int tex)
{
	size_t langlen = strlen(language);
	int lang = (langlen < 2 || langlen > 3) ?
		MAKE_ID('e', 'n', 'u', '\0') :
		MAKE_ID(language[0], language[1], language[2], '\0');

	uint64_t index = (uint64_t(lang) << 32) + tex;
	if (auto pTex = LocalizedTextures.CheckKey(index)) return *pTex;
	index = (uint64_t(lang & MAKE_ID(255, 255, 0, 0)) << 32) + tex;
	if (auto pTex = LocalizedTextures.CheckKey(index)) return *pTex;

	return tex;
}

//===========================================================================
//
// R_GuesstimateNumTextures
//
// Returns an estimate of the number of textures R_InitData will have to
// process. Used by D_DoomMain() when it calls ST_Init().
//
//===========================================================================

int FTextureManager::GuesstimateNumTextures ()
{
	int numtex = 0;

	for(int i = fileSystem.GetNumEntries()-1; i>=0; i--)
	{
		int space = fileSystem.GetFileNamespace(i);
		switch(space)
		{
		case ns_flats:
		case ns_sprites:
		case ns_newtextures:
		case ns_hires:
		case ns_patches:
		case ns_graphics:
			numtex++;
			break;

		default:
			if (fileSystem.GetFileFlags(i) & RESFF_MAYBEFLAT) numtex++;

			break;
		}
	}

	//numtex += CountBuildTiles (); // this cannot be done with a lot of overhead so just leave it out.
	numtex += CountTexturesX ();
	return numtex;
}

//===========================================================================
//
// R_CountTexturesX
//
// See R_InitTextures() for the logic in deciding what lumps to check.
//
//===========================================================================

int FTextureManager::CountTexturesX ()
{
	int count = 0;
	int wadcount = fileSystem.GetNumWads();
	for (int wadnum = 0; wadnum < wadcount; wadnum++)
	{
		// Use the most recent PNAMES for this WAD.
		// Multiple PNAMES in a WAD will be ignored.
		int pnames = fileSystem.CheckNumForName("PNAMES", ns_global, wadnum, false);

		// should never happen except for zdoom.pk3
		if (pnames < 0) continue;

		// Only count the patches if the PNAMES come from the current file
		// Otherwise they have already been counted.
		if (fileSystem.GetFileContainer(pnames) == wadnum) 
		{
			count += CountLumpTextures (pnames);
		}

		int texlump1 = fileSystem.CheckNumForName ("TEXTURE1", ns_global, wadnum);
		int texlump2 = fileSystem.CheckNumForName ("TEXTURE2", ns_global, wadnum);

		count += CountLumpTextures (texlump1) - 1;
		count += CountLumpTextures (texlump2) - 1;
	}
	return count;
}

//===========================================================================
//
// R_CountLumpTextures
//
// Returns the number of patches in a PNAMES/TEXTURE1/TEXTURE2 lump.
//
//===========================================================================

int FTextureManager::CountLumpTextures (int lumpnum)
{
	if (lumpnum >= 0)
	{
		auto file = fileSystem.OpenFileReader (lumpnum); 
		uint32_t numtex = file.ReadUInt32();

		return int(numtex) >= 0 ? numtex : 0;
	}
	return 0;
}

//-----------------------------------------------------------------------------
//
// Adjust sprite offsets for GL rendering (IWAD resources only)
//
//-----------------------------------------------------------------------------

void FTextureManager::AdjustSpriteOffsets()
{
	int lump, lastlump = 0;
	int sprid;
	TMap<int, bool> donotprocess;

	int numtex = fileSystem.GetNumEntries();

	for (int i = 0; i < numtex; i++)
	{
		if (fileSystem.GetFileContainer(i) > fileSystem.GetMaxIwadNum()) break; // we are past the IWAD
		if (fileSystem.GetFileNamespace(i) == ns_sprites && fileSystem.GetFileContainer(i) >= fileSystem.GetIwadNum() && fileSystem.GetFileContainer(i) <= fileSystem.GetMaxIwadNum())
		{
			const char *str = fileSystem.GetFileShortName(i);
			FTextureID texid = TexMan.CheckForTexture(str, ETextureType::Sprite, 0);
			if (texid.isValid() && fileSystem.GetFileContainer(GetGameTexture(texid)->GetSourceLump()) > fileSystem.GetMaxIwadNum())
			{
				// This texture has been replaced by some PWAD.
				memcpy(&sprid, str, 4);
				donotprocess[sprid] = true;
			}
		}
	}

	while ((lump = fileSystem.FindLump("SPROFS", &lastlump, false)) != -1)
	{
		FScanner sc;
		sc.OpenLumpNum(lump);
		sc.SetCMode(true);
		int ofslumpno = fileSystem.GetFileContainer(lump);
		while (sc.GetString())
		{
			int x, y;
			bool iwadonly = false;
			bool forced = false;
			FTextureID texno = TexMan.CheckForTexture(sc.String, ETextureType::Sprite);
			sc.MustGetStringName(",");
			sc.MustGetNumber();
			x = sc.Number;
			sc.MustGetStringName(",");
			sc.MustGetNumber();
			y = sc.Number;
			if (sc.CheckString(","))
			{
				sc.MustGetString();
				if (sc.Compare("iwad")) iwadonly = true;
				if (sc.Compare("iwadforced")) forced = iwadonly = true;
			}
			if (texno.isValid())
			{
				auto tex = GetGameTexture(texno);

				int lumpnum = tex->GetSourceLump();
				// We only want to change texture offsets for sprites in the IWAD or the file this lump originated from.
				if (lumpnum >= 0 && lumpnum < fileSystem.GetNumEntries())
				{
					int wadno = fileSystem.GetFileContainer(lumpnum);
					if ((iwadonly && wadno >= fileSystem.GetIwadNum() && wadno <= fileSystem.GetMaxIwadNum()) || (!iwadonly && wadno == ofslumpno))
					{
						if (wadno >= fileSystem.GetIwadNum() && wadno <= fileSystem.GetMaxIwadNum() && !forced && iwadonly)
						{
							memcpy(&sprid, tex->GetName().GetChars(), 4);
							if (donotprocess.CheckKey(sprid)) continue;	// do not alter sprites that only get partially replaced.
						}
						tex->SetOffsets(1, x, y);
					}
				}
			}
		}
	}
}


//==========================================================================
//
// FTextureAnimator :: SetTranslation
//
// Sets animation translation for a texture
//
//==========================================================================

void FTextureManager::SetTranslation(FTextureID fromtexnum, FTextureID totexnum)
{
	if ((size_t)fromtexnum.texnum < Translation.Size())
	{
		if ((size_t)totexnum.texnum >= Textures.Size())
		{
			totexnum.texnum = fromtexnum.texnum;
		}
		Translation[fromtexnum.texnum] = totexnum.texnum;
	}
}


//-----------------------------------------------------------------------------
//
// Adds an alias name to the texture manager.
// Aliases are only checked if no real texture with the given name exists.
//
//-----------------------------------------------------------------------------

void FTextureManager::AddAlias(const char* name, int texindex)
{
	if (texindex < 0 || texindex >= NumTextures()) return;	// Whatever got passed in here was not valid, so ignore the alias.
	aliases.Insert(name, texindex);
}

void FTextureManager::Listaliases()
{
	decltype(aliases)::Iterator it(aliases);
	decltype(aliases)::Pair* pair;

	TArray<FString> list;
	while (it.NextPair(pair))
	{
		auto tex = GetGameTexture(pair->Value);
		list.Push(FStringf("%s -> %s%s", pair->Key.GetChars(), tex ? tex->GetName().GetChars() : "(null)", ((tex && tex->GetUseType() == ETextureType::Null) ? ", null" : "")));
	}
	std::sort(list.begin(), list.end(), [](const FString& l, const FString& r) { return l.CompareNoCase(r) < 0; });
	for (auto& s : list)
	{
		Printf("%s\n", s.GetChars());
	}
}

//==========================================================================
//
// link a texture with a given lump
//
//==========================================================================

void FTextureManager::SetLinkedTexture(int lump, FGameTexture* tex)
{
	if (lump < fileSystem.GetNumEntries())
	{
		linkedMap.Insert(lump, tex);
	}
}

//==========================================================================
//
// retrieve linked texture
//
//==========================================================================

FGameTexture* FTextureManager::GetLinkedTexture(int lump)
{
	if (lump < fileSystem.GetNumEntries())
	{
		auto check = linkedMap.CheckKey(lump);
		if (check) return *check;
	}
	return nullptr;
}


//==========================================================================
//
// FTextureID::operator+
// Does not return invalid texture IDs
//
//==========================================================================

FTextureID FTextureID::operator +(int offset) const noexcept(true)
{
	if (!isValid()) return *this;
	if (texnum + offset >= TexMan.NumTextures()) return FTextureID(-1);
	return FTextureID(texnum + offset);
}

CCMD(flushtextures)
{
	TexMan.FlushAll();
}

CCMD(listtexturealiases)
{
	TexMan.Listaliases();
}

CCMD(analyzetextures)
{	
	TexMan.AnalyzeTextures(argv.argc() > 1 ? atoi(argv[1]) : 100, argv.argc() > 2 ? atoi(argv[2]) : 0);
}
