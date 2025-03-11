#ifndef OALSOUND_H
#define OALSOUND_H

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <unordered_map>

#include "i_sound.h"
#include "s_soundinternal.h"

#include "TSQueue.h"

#ifndef NO_OPENAL

#ifdef DYN_OPENAL
#define AL_NO_PROTOTYPES
#include "thirdparty/al.h"
#include "thirdparty/alc.h"
#else
#include "al.h"
#include "alc.h"
#endif

#include "thirdparty/alext.h"


struct OpenALQueueItem {
	SoundHandle sfx;
	//SoundListener *listener;
	float vol = 0, dist_sqr = 0, distscale = 0, startTime = 0, pitch = 0;
	FVector3 pos = { 0.,0.,0.}, vel = { 0.,0.,0.};
	FRolloffInfo rolloff;
	int channum = -1, chanflags = 0, priority = 0;
	uint64_t chanStartTime = 0;
	ALuint source = 0;
	ALuint buffer = 0;
	ALuint envSlot = 0;
	bool reuseChan = false;		// Only used to signal what type of offset startTime is
	bool inWater = false;		// Play as if in water
};

struct OpenALPlayedItem {
	ALuint source	= 0;
	ALint state		= -1;
	int channum		= -1;
};


class OpenALSoundStream;

class OpenALSoundRenderer : public SoundRenderer
{
public:
	OpenALSoundRenderer();
	virtual ~OpenALSoundRenderer();

	virtual void SetSfxVolume(float volume);
	virtual void SetMusicVolume(float volume);
	virtual SoundHandle LoadSound(uint8_t *sfxdata, int length, int def_loop_start, int def_loop_end);
	virtual SoundHandle LoadSoundRaw(uint8_t *sfxdata, int length, int frequency, int channels, int bits, int loopstart, int loopend = -1);
	virtual void UnloadSound(SoundHandle sfx);
	virtual unsigned int GetMSLength(SoundHandle sfx);
	virtual unsigned int GetSampleLength(SoundHandle sfx);
	virtual float GetOutputRate();

	// Streaming sounds.
	virtual SoundStream *CreateStream(SoundStreamCallback callback, int buffbytes, int flags, int samplerate, void *userdata);

	// Starts a sound.
	FISoundChannel *StartSound(SoundHandle sfx, float vol, float pitch, int chanflags, FISoundChannel *reuse_chan, float startTime) override;
	FISoundChannel *StartSound3D(SoundHandle sfx, SoundListener *listener, float vol, FRolloffInfo *rolloff, float distscale, float pitch, int priority, const FVector3 &pos, const FVector3 &vel, int channum, int chanflags, FISoundChannel *reuse_chan, float startTime) override;
	virtual FISoundChannel *ReserveChannel(int priority);

	// Changes a channel's volume.
	virtual void ChannelVolume(FISoundChannel *chan, float volume);

	// Changes a channel's pitch.
	virtual void ChannelPitch(FISoundChannel *chan, float pitch);

	// Stops a sound channel.
	virtual void StopChannel(FISoundChannel *chan);

	// Returns position of sound on this channel, in samples.
	virtual unsigned int GetPosition(FISoundChannel *chan);

	// Synchronizes following sound startups.
	virtual void Sync(bool sync);

	// Pauses or resumes all sound effect channels.
	virtual void SetSfxPaused(bool paused, int slot);

	// Pauses or resumes *every* channel, including environmental reverb.
	virtual void SetInactive(SoundRenderer::EInactiveState inactive);

	// Updates the volume, separation, and pitch of a sound channel.
	virtual void UpdateSoundParams3D(SoundListener *listener, FISoundChannel *chan, bool areasound, const FVector3 &pos, const FVector3 &vel);

	virtual void UpdateListener(SoundListener *);
	virtual void UpdateSounds();

	virtual void MarkStartTime(FISoundChannel*, float startTime);
	virtual float GetAudibility(FISoundChannel*);


	virtual bool IsValid();
	virtual void PrintStatus();
	virtual void PrintDriversList();
	virtual FString GatherStats();

private:
    struct {
        bool EXT_EFX;
        bool EXT_disconnect;
        bool SOFT_HRTF;
        bool SOFT_pause_device;
		bool SOFT_output_limiter;
    } ALC;
    struct {
        bool EXT_source_distance_model;
        bool EXT_SOURCE_RADIUS;
        bool SOFT_deferred_updates;
        bool SOFT_loop_points;
        bool SOFT_source_latency;
        bool SOFT_source_resampler;
        bool SOFT_source_spatialize;
    } AL;

	// EFX Extension function pointer variables. Loaded after context creation
	// if EFX is supported. These pointers may be context- or device-dependant,
	// thus can't be static
	// Effect objects
	LPALGENEFFECTS alGenEffects;
	LPALDELETEEFFECTS alDeleteEffects;
	LPALISEFFECT alIsEffect;
	LPALEFFECTI alEffecti;
	LPALEFFECTIV alEffectiv;
	LPALEFFECTF alEffectf;
	LPALEFFECTFV alEffectfv;
	LPALGETEFFECTI alGetEffecti;
	LPALGETEFFECTIV alGetEffectiv;
	LPALGETEFFECTF alGetEffectf;
	LPALGETEFFECTFV alGetEffectfv;
	// Filter objects
	LPALGENFILTERS alGenFilters;
	LPALDELETEFILTERS alDeleteFilters;
	LPALISFILTER alIsFilter;
	LPALFILTERI alFilteri;
	LPALFILTERIV alFilteriv;
	LPALFILTERF alFilterf;
	LPALFILTERFV alFilterfv;
	LPALGETFILTERI alGetFilteri;
	LPALGETFILTERIV alGetFilteriv;
	LPALGETFILTERF alGetFilterf;
	LPALGETFILTERFV alGetFilterfv;
	// Auxiliary slot objects
	LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots;
	LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots;
	LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot;
	LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti;
	LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv;
	LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf;
	LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv;
	LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti;
	LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv;
	LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf;
	LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv;

    ALvoid (AL_APIENTRY*alDeferUpdatesSOFT)(void);
    ALvoid (AL_APIENTRY*alProcessUpdatesSOFT)(void);

    LPALGETSTRINGISOFT alGetStringiSOFT;

    LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT;

    void (ALC_APIENTRY*alcDevicePauseSOFT)(ALCdevice *device);
    void (ALC_APIENTRY*alcDeviceResumeSOFT)(ALCdevice *device);

    void BackgroundProc();
	void BackgroundQueueProc();
	void FlushPlayQueue();
	void UpdatePlayedSounds();
    void AddStream(OpenALSoundStream *stream);
    void RemoveStream(OpenALSoundStream *stream);

	void LoadReverb(const ReverbContainer *env);
	void PurgeStoppedSources();
	static FSoundChan *FindLowestChannel();

    std::thread StreamThread, QueueThread;
	std::mutex StreamLock, QueueThreadLock;
    std::condition_variable StreamWake, QueueWake;
    std::atomic<bool> QuitThread, QuitQueueThread;

	ALCdevice *Device;
	ALCcontext *Context;

	TArray<ALuint> Sources;

	std::atomic<ALfloat> SfxVolume;
	std::atomic<ALfloat> MusicVolume;

	struct SFXStatus {
		ALuint source = 0;
		ALint state = AL_INITIAL;
		bool canReverb, canPause, wasInWater;
	};

	std::atomic<int> SFXPaused;
	TArray<ALuint> FreeSfx;
	//TArray<ALuint> PausableSfx;
	//TArray<ALuint> ReverbSfx;
	// TODO: Change this to a map with SOURCE as the key instead of an array
	std::unordered_map<ALuint, SFXStatus> SfxGroup;

	inline SFXStatus *statusForSource(ALuint src) {
		auto r = SfxGroup.find(src);
		return r == SfxGroup.end() ? nullptr : &r->second;
	}

	const ReverbContainer *PrevEnvironment;

    typedef TMap<uint16_t,ALuint> EffectMap;
    typedef TMapIterator<uint16_t,ALuint> EffectMapIter;
    ALuint EnvSlot;
    ALuint EnvFilters[2];
    EffectMap EnvEffects;

    bool WasInWater;

    TArray<OpenALSoundStream*> Streams;
	TSQueue<OpenALQueueItem> PlayQueue;			// Fill PlayQueue to play, once played appears in PlayedQueue
	TSQueue<OpenALPlayedItem> PlayedQueue;
	
	friend class OpenALSoundStream;

	ALCdevice *InitDevice();

	// @Cockatrice: Thread safe(ish) - start sound from queue item
	// Unfortunately makes a lot of assumptions about the safe nature of other funcs
	// For instance GetRolloff should be safe for now, but if the sound curve is ever changed
	// at runtime, it's no longer safe. Same goes for AL extensions, if they are changed at 
	// at runtime this is no longer thread safe
	bool StartSound3D(OpenALQueueItem &playInfo);
	bool StartSound(OpenALQueueItem &playInfo);
};

#endif // NO_OPENAL

#endif
