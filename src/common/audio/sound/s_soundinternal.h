#pragma once

#include "i_sound.h"

struct FRandomSoundList
{
	TArray<uint32_t> Choices;
	uint32_t Owner = 0;
};

enum
{
	sfx_empty = -1
};



//
// SoundFX struct.
//
struct sfxinfo_t
{
	// Next field is for use by the system sound interface.
	// A non-null data means the sound has been loaded.
	SoundHandle	data{};

	FString		name;								// [RH] Sound name defined in SNDINFO
	int 		lumpnum = sfx_empty;				// lump number of sfx

	unsigned int next = -1, index = 0;				// [RH] For hashing
	float		Volume = 1.f;

	int			ResourceId = -1;					// Resource ID as implemented by Blood. Not used by Doom but added for completeness.
	float		LimitRange = 256*256;				// Range for sound limiting (squared for faster computations)
	float		DefPitch = 0.f;						// A defined pitch instead of a random one the sound plays at, similar to A_StartSound.
	float		DefPitchMax = 0.f;					// Randomized range with stronger control over pitch itself.

	int16_t		NearLimit = 4;						// 0 means unlimited.
	uint8_t		PitchMask = 0;
	bool		bRandomHeader = false;
	bool		bLoadRAW = false;
	bool		b16bit = false;
	bool		bUsed = false;
	bool		bSingular = false;
	bool		bTentative = true;

	TArray<int> UserData;

	int			RawRate = 0;				// Sample rate to use when bLoadRAW is true
	int			LoopStart = -1;				// -1 means no specific loop defined

	unsigned int link = NO_LINK;;
	enum { NO_LINK = 0xffffffff };

	FRolloffInfo	Rolloff{};
	float		Attenuation = 1.f;			// Multiplies the attenuation passed to S_Sound.
};

// Rolloff types
enum
{
	ROLLOFF_Doom,		// Linear rolloff with a logarithmic volume scale
	ROLLOFF_Linear,		// Linear rolloff with a linear volume scale
	ROLLOFF_Log,		// Logarithmic rolloff (standard hardware type)
	ROLLOFF_Custom		// Lookup volume from SNDCURVE
};

inline int S_FindSoundByResID(int ndx);
inline int S_FindSound(const char* name);

// An index into the S_sfx[] array.
class FSoundID
{
public:
	FSoundID() = default;

	static FSoundID byResId(int ndx)
	{
		return FSoundID(S_FindSoundByResID(ndx)); 
	}
	FSoundID(int id)
	{
		ID = id;
	}
	FSoundID(const char *name)
	{
		ID = S_FindSound(name);
	}
	FSoundID(const FString &name)
	{
		ID = S_FindSound(name.GetChars());
	}
	FSoundID(const FSoundID &other) = default;
	FSoundID &operator=(const FSoundID &other) = default;
	FSoundID &operator=(const char *name)
	{
		ID = S_FindSound(name);
		return *this;
	}
	FSoundID &operator=(const FString &name)
	{
		ID = S_FindSound(name.GetChars());
		return *this;
	}
	bool operator !=(FSoundID other) const
	{
		return ID != other.ID;
	}
	bool operator !=(int other) const
	{
		return ID != other;
	}
	operator int() const
	{
		return ID;
	}
private:
	int ID;
protected:
	enum EDummy { NoInit };
	FSoundID(EDummy) {}
};

 class FSoundIDNoInit : public FSoundID
{
public:
	FSoundIDNoInit() : FSoundID(NoInit) {}
	using FSoundID::operator=;
};


 // Reference to a playing, or previously played sound
 // Will maintain a pointer to FSoundChan for quick reference
 class FSoundHandle {
 public:
	 FSoundHandle() = default;

	 FSoundHandle(int id) {
		 ID = id;
	 }
	 FSoundHandle(const FSoundHandle& other) = default;
	 FSoundHandle& operator=(const FSoundHandle& other) = default;

	 bool operator !=(FSoundHandle other) const
	 {
		 return ID != other.ID;
	 }
	 bool operator !=(int other) const
	 {
		 return ID != other;
	 }
	 operator int() const
	 {
		 return ID;
	 }

	 bool IsValid() const { return ID != 0; }
	 bool IsPlaying() { return false; }
	 bool StopSound() { return false; }
	 bool SetPitch(double pitch) { return false; }
	 bool SetVolume(double vol) { return false; }

 private:
	 int			ID = 0;
 };



struct FSoundChan : public FISoundChannel
{
	FSoundChan	*NextChan;	// Next channel in this list.
	FSoundChan **PrevChan;	// Previous channel in this list.
	FSoundID	SoundID;	// Sound ID of playing sound.
	FSoundID	OrgID;		// Sound ID of sound used to start this channel.
	int			HandleID;	// @Cockatrice - Unique ID of the current sound, correlates to a FSoundHandle ID
	float		Volume;
	int 		EntChannel;	// Actor's sound channel.
	int			UserData;	// Not used by the engine, the caller can use this to store some additional info.
	float		Pitch;		// Pitch variation.
	int16_t		NearLimit;
	int8_t		Priority;
	uint8_t		SourceType;
	float		LimitRange;
	const void *Source;
	float Point[3];	// Sound is not attached to any source.
};


// sound channels
// channel 0 never willingly overrides
// other channels (1-7) always override a playing sound on that channel
//
// CHAN_AUTO searches down from channel 7 until it finds a channel not in use
// CHAN_WEAPON is for weapons
// CHAN_VOICE is for oof, sight, or other voice sounds
// CHAN_ITEM is for small things and item pickup
// CHAN_BODY is for generic body sounds

enum EChannel
{
	CHAN_AUTO = 0,
	CHAN_WEAPON = 1,
	CHAN_VOICE = 2,
	CHAN_ITEM = 3,
	CHAN_BODY = 4,
	CHAN_5 = 5,
	CHAN_6 = 6,
	CHAN_7 = 7,
};



// sound attenuation values
#define ATTN_NONE				0.f	// full volume the entire level
#define ATTN_NORM				1.f
#define ATTN_IDLE				1.001f
#define ATTN_STATIC				3.f	// diminish very rapidly with distance

enum // The core source types, implementations may extend this list as they see fit.
{
	SOURCE_Any = -1,	// Input for check functions meaning 'any source'
	SOURCE_Unattached,	// Sound is not attached to any particular emitter.
	SOURCE_None,		// Sound is always on top of the listener.
};


extern ReverbContainer *Environments;
extern ReverbContainer *DefaultEnvironments[26];

void S_ParseReverbDef ();
void S_UnloadReverbDef ();
void S_SetEnvironment (const ReverbContainer *settings);
ReverbContainer *S_FindEnvironment (const char *name);
ReverbContainer *S_FindEnvironment (int id);
void S_AddEnvironment (ReverbContainer *settings);

class SoundEngine
{
protected:
	bool SoundPaused = false;		// whether sound is paused
	int RestartEvictionsAt = 0;		// do not restart evicted channels before this time
	SoundListener listener{};

	FSoundChan* Channels = nullptr;
	FSoundChan* FreeChannels = nullptr;

	// the complete set of sound effects
	TArray<sfxinfo_t> S_sfx;
	FRolloffInfo S_Rolloff{};
	TArray<uint8_t> S_SoundCurve;			// @Cockatrice - As far as I know this is only inited once, but for thread safety do not change this during gameplay
	TMap<int, int> ResIdMap;
	TArray<FRandomSoundList> S_rnd;
	bool blockNewSounds = false;

private:
	void LinkChannel(FSoundChan* chan, FSoundChan** head);
	void UnlinkChannel(FSoundChan* chan);
	void ReturnChannel(FSoundChan* chan);
	void RestartChannel(FSoundChan* chan);
	void RestoreEvictedChannel(FSoundChan* chan);

	bool IsChannelUsed(int sourcetype, const void* actor, int channel, int* seen);
	// This is the actual sound positioning logic which needs to be provided by the client.
	virtual void CalcPosVel(int type, const void* source, const float pt[3], int channel, int chanflags, FSoundID chanSound, FVector3* pos, FVector3* vel, FSoundChan *chan) = 0;
	// This can be overridden by the clent to provide some diagnostics. The default lets everything pass.
	virtual bool ValidatePosVel(int sourcetype, const void* source, const FVector3& pos, const FVector3& vel) { return true; }

	bool ValidatePosVel(const FSoundChan* const chan, const FVector3& pos, const FVector3& vel);

	// Checks if a copy of this sound is already playing.
	bool CheckSingular(int sound_id);
	virtual TArray<uint8_t> ReadSound(int lumpnum) = 0;
protected:
	virtual bool CheckSoundLimit(sfxinfo_t* sfx, const FVector3& pos, int near_limit, float limit_range, int sourcetype, const void* actor, int channel, float attenuation, sfxinfo_t* compareOrgID = nullptr);
	virtual FSoundID ResolveSound(const void *ent, int srctype, FSoundID soundid, float &attenuation);

public:
	virtual ~SoundEngine()
	{
		Shutdown();
	}
	void EvictAllChannels();

	void BlockNewSounds(bool on)
	{
		blockNewSounds = on;
	}

	int LastSoundHandle = 0;					// @Cockatrice - global counter for sound handle ID. I don't want this public but it has to be accessible to Doomsound's serializer

	virtual void StopChannel(FSoundChan* chan);
	sfxinfo_t* LoadSound(sfxinfo_t* sfx);
	sfxinfo_t* CheckLinks(sfxinfo_t *sfx);

	const sfxinfo_t* GetSfx(unsigned snd)
	{
		if (snd >= S_sfx.Size()) return nullptr;
		return &S_sfx[snd];
	}

	// Initializes sound stuff, including volume
	// Sets channels, SFX and music volume,
	//	allocates channel buffer, sets S_sfx lookup.
	//
	void Init(TArray<uint8_t> &sndcurve);
	void InitData();
	void Clear();
	void Shutdown();

	void StopAllChannels(void);
	void SetPitch(FSoundChan* chan, float dpitch);
	void SetVolume(FSoundChan* chan, float vol);
	bool SetPitch(FSoundHandle &handle, float dpitch);
	bool SetVolume(FSoundHandle &handle, float vol);

	FSoundChan* GetChannel(void* syschan);
	FSoundChan* FindChannel(void* syschan);
	bool IsPlaying(FSoundHandle& handle);
	void RestoreEvictedChannels();
	void CalcPosVel(FSoundChan* chan, FVector3* pos, FVector3* vel);

	// Loads a sound, including any random sounds it might reference.
	virtual void CacheSound(sfxinfo_t* sfx);
	void CacheSound(int sfx) { CacheSound(&S_sfx[sfx]); }
	void UnloadSound(sfxinfo_t* sfx);
	void UnloadSound(int sfx)
	{
		UnloadSound(&S_sfx[sfx]);
	}

	void UpdateSounds(int time);

	FSoundChan* StartSound(int sourcetype, const void* source,
		const FVector3* pt, int channel, EChanFlags flags, FSoundID sound_id, float volume, float attenuation, FRolloffInfo* rolloff = nullptr, float spitch = 0.0f, float startTime = 0.0f, FSoundHandle* handleOut = nullptr);
	
	FSoundChan* StartSoundER(sfxinfo_t *sfx, int type, const void *source,
		FVector3 pos, FVector3 vel, int channel, EChanFlags flags, FSoundID sound_id, FSoundID org_sound_id, float volume, float attenuation,
		FRolloffInfo *forcedrolloff, float spitch, float startTime, bool usePosVel = true, FSoundHandle reservedHandle = 0);

	// Stops an origin-less sound from playing from this channel.
	void StopSoundID(int sound_id);
	void StopSound(int channel, int sound_id = -1);
	void StopSound(int sourcetype, const void* actor, int channel, int sound_id = -1);
	bool StopSound(FSoundHandle& handle);
	void StopActorSounds(int sourcetype, const void* actor, int chanmin, int chanmax);

	void RelinkSound(int sourcetype, const void* from, const void* to, const FVector3* optpos);
	void ChangeSoundVolume(int sourcetype, const void* source, int channel, double dvolume);
	void ChangeSoundPitch(int sourcetype, const void* source, int channel, double pitch, int sound_id = -1);
	bool IsSourcePlayingSomething(int sourcetype, const void* actor, int channel, int sound_id = -1);

	// Stop and resume music, during game PAUSE.
	int GetSoundPlayingInfo(int sourcetype, const void* source, int sound_id, int chan = -1);
	void UnloadAllSounds();
	void Reset();
	void MarkUsed(int num);
	void CacheMarkedSounds();
	TArray<FSoundChan*> AllActiveChannels();

	void MarkAllUnused()
	{
		for (auto & s: S_sfx) s.bUsed = false;
	}

	bool isListener(const void* object) const
	{
		return object && listener.ListenerObject == object;
	}
	void SetListener(SoundListener& l)
	{
		listener = l;
	}
	const SoundListener& GetListener() const
	{
		return listener;
	}
	void SetRestartTime(int time)
	{
		RestartEvictionsAt = time;
	}
	void SetPaused(bool on)
	{
		SoundPaused = on;
	}
	FSoundChan* GetChannels()
	{
		return Channels;
	}
	const char *GetSoundName(FSoundID id)
	{
		return id == 0 ? "" : S_sfx[id].name.GetChars();
	}
	TArray<sfxinfo_t> &GetSounds()	//This should only be used for constructing the sound list or for diagnostics code prinring information about the sound list.
	{
		return S_sfx;
	}
	FRolloffInfo& GlobalRolloff() // like GetSounds this is meant for sound list generators, not for gaining cheap access to the sound engine's innards.
	{
		return S_Rolloff;
	}
	FRandomSoundList *ResolveRandomSound(sfxinfo_t* sfx)
	{
		return &S_rnd[sfx->link];
	}
	void ClearRandoms()
	{
		S_rnd.Clear();
	}
	int *GetUserData(int snd)
	{
		return S_sfx[snd].UserData.Data();
	}
	bool isValidSoundId(int id)
	{
		return id > 0 && id < (int)S_sfx.Size() && !S_sfx[id].bTentative && S_sfx[id].lumpnum != sfx_empty;
	}

	template<class func> bool EnumerateChannels(func callback)
	{
		FSoundChan* chan = Channels;
		while (chan)
		{
			auto next = chan->NextChan;
			int res = callback(chan);
			if (res) return res > 0;
			chan = next;
		}
		return false;
	}

	void SetDefaultRolloff(FRolloffInfo* ro)
	{
		S_Rolloff = *ro;
	}

	void ChannelVirtualChanged(FISoundChannel* ichan, bool is_virtual);
	FString ListSoundChannels();

	// Allow this to be overridden for special needs.
	virtual float GetRolloff(const FRolloffInfo* rolloff, float distance);
	virtual void ChannelEnded(FISoundChannel* ichan); // allows the client to do bookkeeping on the sound.
	virtual void SoundDone(FISoundChannel* ichan); // gets called when the sound has been completely taken down.

	// Lookup utilities.
	int FindSound(const char* logicalname);
	int FindSoundByResID(int rid);
	int FindSoundNoHash(const char* logicalname);
	int FindSoundByLump(int lump);
	virtual int AddSoundLump(const char* logicalname, int lump, int CurrentPitchMask, int resid = -1, int nearlimit = 2);
	int FindSoundTentative(const char* name);
	void CacheRandomSound(sfxinfo_t* sfx);
	unsigned int GetMSLength(FSoundID sound);
	int PickReplacement(int refid);
	void HashSounds();
	void AddRandomSound(int Owner, TArray<uint32_t> list);
};


extern SoundEngine* soundEngine;

struct FReverbField
{
	int Min, Max;
	float REVERB_PROPERTIES::* Float;
	int REVERB_PROPERTIES::* Int;
	unsigned int Flag;
};


inline int S_FindSoundByResID(int ndx)
{
	return soundEngine->FindSoundByResID(ndx);
}

inline int S_FindSound(const char* name)
{
	return soundEngine->FindSound(name);
}

int SoundEnabled();
