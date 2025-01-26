#pragma once

#include <stdint.h>
#include "textureid.h"
#include "i_modelvertexbuffer.h"
#include "matrix.h"
#include "palettecontainer.h"
#include "TRS.h"
#include "tarray.h"
#include "name.h"
#include "fs_files.h"

#include "bonecomponents.h"

class DBoneComponents;
class FModelRenderer;
class FGameTexture;
class IModelVertexBuffer;
class FModel;
struct FSpriteModelFrame;

FTextureID LoadSkin(const char* path, const char* fn);
void FlushModels();

extern TDeletingArray<FModel*> Models;
extern TArray<FSpriteModelFrame> SpriteModelFrames;
extern TMap<void*, FSpriteModelFrame> BaseSpriteModelFrames;

#define MD3_MAX_SURFACES	32
#define MIN_MODELS	4

struct FSpriteModelFrame
{
	uint8_t modelsAmount = 0;
	TArray<int> modelIDs;
	TArray<FTextureID> skinIDs;
	TArray<FTextureID> surfaceskinIDs;
	TArray<int> modelframes;
	TArray<int> animationIDs;
	float xscale, yscale, zscale;
	// [BB] Added zoffset, rotation parameters and flags.
	// Added xoffset, yoffset
	float xoffset, yoffset, zoffset;
	float xrotate, yrotate, zrotate;
	float rotationCenterX, rotationCenterY, rotationCenterZ;
	float rotationSpeed;
private:
	unsigned int flags;
public:
	const void* type;	// used for hashing, must point to something usable as identifier for the model's owner.
	short sprite;
	short frame;
	int hashnext;
	float angleoffset;
	// added pithoffset, rolloffset.
	float pitchoffset, rolloffset; // I don't want to bother with type transformations, so I made this variables float.
	bool isVoxel;
	unsigned int getFlags(class DActorModelData * defs) const;
	friend void InitModels();
	friend void ParseModelDefLump(int Lump);
};


enum ModelRendererType
{
	GLModelRendererType,
	SWModelRendererType,
	PolyModelRendererType,
	NumModelRendererTypes
};

enum EFrameError
{
	FErr_NotFound = -1,
	FErr_Voxel = -2,
	FErr_Singleframe = -3
};

class FModel
{
public:
	enum LoadState
	{
		NONE = 0,		// Uninitialized
		LOADING = 1,	// Waiting on texture load op
		READY = 2		// Fully loaded
	};

	FModel();
	virtual ~FModel();

	virtual bool Load(const char * fn, int lumpnum, const char * buffer, int length) = 0;

	virtual int FindFrame(const char * name, bool nodefault = false) = 0;

	// [RL0] these are used for decoupled iqm animations
	virtual int FindFirstFrame(FName name) { return FErr_NotFound; }
	virtual int FindLastFrame(FName name) { return FErr_NotFound; }
	virtual double FindFramerate(FName name) { return FErr_NotFound; }

	virtual void RenderFrame(FModelRenderer *renderer, FGameTexture * skin, int frame, int frame2, double inter, FTranslationID translation, const FTextureID* surfaceskinids, int boneStartPosition) = 0;
	virtual void BuildVertexBuffer(FModelRenderer *renderer) = 0;
	virtual void AddSkins(uint8_t *hitlist, const FTextureID* surfaceskinids) = 0;
	virtual float getAspectFactor(float vscale) { return 1.f; }
	virtual const TArray<TRS>* AttachAnimationData() { return nullptr; };

	virtual ModelAnimFrame PrecalculateFrame(const ModelAnimFrame &from, const ModelAnimFrameInterp &to, float inter, const TArray<TRS>* animationData) { return nullptr; };

	virtual const TArray<VSMatrix>* CalculateBones(const ModelAnimFrame &from, const ModelAnimFrameInterp &to, float inter, const TArray<TRS>* animationData) { return nullptr; };

	void SetVertexBuffer(int type, IModelVertexBuffer *buffer) { mVBuf[type] = buffer; }
	IModelVertexBuffer *GetVertexBuffer(int type) const { return mVBuf[type]; }
	void DestroyVertexBuffer();

	LoadState GetLoadState() const { return loadState; }
	void SetLoadState(LoadState state) { loadState = state; }

	virtual void LoadGeometry(FileSys::FileData* lumpData);
	int GetLumpNum() const { return mLumpNum; }

	bool hasSurfaces = false;

	FString mFileName;
	std::pair<FString, FString> mFilePath;
	
	FSpriteModelFrame *baseFrame = nullptr;
private:
	IModelVertexBuffer *mVBuf[NumModelRendererTypes];
	LoadState loadState = NONE;
protected:
	int mLumpNum = -1;
};

int ModelFrameHash(FSpriteModelFrame* smf);
unsigned FindModel(const char* path, const char* modelfile, bool silent = false);

