#ifndef __SNDINT_H
#define __SNDINT_H

#define NOMINMAX

#include <stdio.h>
#include <stdint.h>
#include <algorithm>

#include "vectors.h"
#include "tflags.h"
#include "tarray.h"

enum EChanFlag
{
	// modifier flags
	CHANF_LISTENERZ = 8,
	CHANF_MAYBE_LOCAL = 16,
	CHANF_UI = 32,	// Do not record sound in savegames.
	CHANF_NOPAUSE = 64,	// Do not pause this sound in menus.
	CHANF_AREA = 128,	// Sound plays from all around. Only valid with sector sounds.
	CHANF_LOOP = 256,

	CHANF_PICKUP = CHANF_MAYBE_LOCAL,

	CHANF_NONE = 0,
	CHANF_IS3D = 1,		// internal: Sound is 3D.
	CHANF_EVICTED = 2,		// internal: Sound was evicted.
	CHANF_FORGETTABLE = 4,		// internal: Forget channel data when sound stops.
	CHANF_JUSTSTARTED = 512,	// internal: Sound has not been updated yet.
	CHANF_ABSTIME = 1024,	// internal: Start time is absolute and does not depend on current time.
	CHANF_VIRTUAL = 2048,	// internal: Channel is currently virtual
	CHANF_NOSTOP = 4096,	// only for A_PlaySound. Does not start if channel is playing something.
	CHANF_OVERLAP = 8192, // [MK] Does not stop any sounds in the channel and instead plays over them.
	CHANF_LOCAL = 16384,	// only plays locally for the calling actor
	CHANF_TRANSIENT = 32768,	// Do not record in savegames - used for sounds that get restarted outside the sound system (e.g. ambients in SW and Blood)
	CHANF_FORCE = 65536,		// Start, even if sound is paused.
	CHANF_RESERVED = 0x20000,	// @Cockatrice - internal: Channel is reserved for a queued sound and is not yet playing
	CHANF_SINGULAR = 0x40000,	// Only start if no sound of this name is already playing.
	CHANF_NOREVERB = 0x80000,	// No reverb, passes on SNDF_NOREVERB to the sound
};

typedef TFlags<EChanFlag> EChanFlags;
DEFINE_TFLAGS_OPERATORS(EChanFlags)


// For convenience, this structure matches FMOD_REVERB_PROPERTIES.
// Since I can't very well #include system-specific stuff in the
// main game files, I duplicate it here.
struct REVERB_PROPERTIES
{                
	int			 Instance = -1;
    int			 Environment;
    float        EnvSize;
    float        EnvDiffusion;
    int          Room;
    int          RoomHF;
    int          RoomLF;
    float        DecayTime;
    float        DecayHFRatio;
    float        DecayLFRatio;
    int          Reflections;
    float        ReflectionsDelay;
    float        ReflectionsPan0;
	float        ReflectionsPan1;
	float        ReflectionsPan2;
    int          Reverb;
    float        ReverbDelay;
    float        ReverbPan0;
	float        ReverbPan1;
	float        ReverbPan2;
    float        EchoTime;
    float        EchoDepth;
    float        ModulationTime;
    float        ModulationDepth;
    float        AirAbsorptionHF;
    float        HFReference;
    float        LFReference;
    float        RoomRolloffFactor;
    float        Diffusion;
    float        Density;
    unsigned int Flags;


	// @Cockatrice - Helper functions for interpolating between sound environments
	static inline float Lerp(float a, float b, float t) {
		return a + (b - a) * t;
	}

	static inline int LerpInt(int a, int b, float t) {
		return static_cast<int>( std::lround( Lerp(static_cast<float>(a), static_cast<float>(b), t) ) );
	}

	static inline float SmoothStep(float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// @Cockatrice - Create a custom interpolation between environments.
	// We can use this in real time to transition environments smoothly
    static void Interpolate(const REVERB_PROPERTIES& a, const REVERB_PROPERTIES& b, float t, REVERB_PROPERTIES& out) {
        t = SmoothStep(t);	// Lerp sounds a bit artificial without this

        // don't touch Instance or Environment data, it's meaningless here
        // flags can't be interpolated, so just switch after the halfway point
        out.Flags = (t < 0.5f) ? a.Flags : b.Flags;

        // density = powf(props.EnvSize, 3.0f) * 0.0625f
        // lerp converted value first
        const float densityA = a.EnvSize * a.EnvSize * a.EnvSize * 0.0625f;
        const float densityB = b.EnvSize * b.EnvSize * b.EnvSize * 0.0625f;
        const float density = Lerp(densityA, densityB, t);

        // now invert density back to original values
        out.EnvSize = std::cbrt(density * 16.0f);
        out.EnvDiffusion = Lerp(a.EnvDiffusion, b.EnvDiffusion, t);

        // these are converted by mB2Gain() later
        out.Room = LerpInt(a.Room, b.Room, t);
        out.RoomHF = LerpInt(a.RoomHF, b.RoomHF, t);
        out.RoomLF = LerpInt(a.RoomLF, b.RoomLF, t);
        out.Reflections = LerpInt(a.Reflections, b.Reflections, t);
        out.Reverb = LerpInt(a.Reverb, b.Reverb, t);

        out.AirAbsorptionHF = Lerp(a.AirAbsorptionHF, b.AirAbsorptionHF, t);

        // these are all time values
        out.DecayTime = Lerp(a.DecayTime, b.DecayTime, t);
        out.ReflectionsDelay = Lerp(a.ReflectionsDelay, b.ReflectionsDelay, t);
        out.ReverbDelay = Lerp(a.ReverbDelay, b.ReverbDelay, t);
        out.EchoTime = Lerp(a.EchoTime, b.EchoTime, t);
        out.ModulationTime = Lerp(a.ModulationTime, b.ModulationTime, t);

        // these are all normalized values, but we won't clamp here
        out.DecayHFRatio = Lerp(a.DecayHFRatio, b.DecayHFRatio, t);
        out.DecayLFRatio = Lerp(a.DecayLFRatio, b.DecayLFRatio, t);
        out.EchoDepth = Lerp(a.EchoDepth, b.EchoDepth, t);
        out.ModulationDepth = Lerp(a.ModulationDepth, b.ModulationDepth, t);
        out.RoomRolloffFactor = Lerp(a.RoomRolloffFactor, b.RoomRolloffFactor, t);
    
        // vectors, lerp should work fine even though the result may not be fully normalized?
        out.ReflectionsPan0 = Lerp(a.ReflectionsPan0, b.ReflectionsPan0, t);
        out.ReflectionsPan1 = Lerp(a.ReflectionsPan1, b.ReflectionsPan1, t);
        out.ReflectionsPan2 = Lerp(a.ReflectionsPan2, b.ReflectionsPan2, t);
        out.ReverbPan0 = Lerp(a.ReverbPan0, b.ReverbPan0, t);
        out.ReverbPan1 = Lerp(a.ReverbPan1, b.ReverbPan1, t);
        out.ReverbPan2 = Lerp(a.ReverbPan2, b.ReverbPan2, t);


        // I was told to interpolate these logarithmically and I believed it
        const float hfA = (std::max)(a.HFReference, 1.0e-6f);
        const float hfB = (std::max)(b.HFReference, 1.0e-6f);
        const float lfA = (std::max)(a.LFReference, 1.0e-6f);
        const float lfB = (std::max)(b.LFReference, 1.0e-6f);

        out.HFReference = std::exp( Lerp(std::log(hfA), std::log(hfB), t) );
        out.LFReference = std::exp( Lerp(std::log(lfA), std::log(lfB), t) );


        // these two don't seem to be used by anything, but just in case we'll lerp em
        out.Diffusion = Lerp(a.Diffusion, b.Diffusion, t);
        out.Density = Lerp(a.Density, b.Density, t);
    }
};

enum EReverbFlags
{
	REVERB_FLAGS_DECAYTIMESCALE        = 0x00000001,
	REVERB_FLAGS_REFLECTIONSSCALE      = 0x00000002,
	REVERB_FLAGS_REFLECTIONSDELAYSCALE = 0x00000004,
	REVERB_FLAGS_REVERBSCALE           = 0x00000008,
	REVERB_FLAGS_REVERBDELAYSCALE      = 0x00000010,
	REVERB_FLAGS_DECAYHFLIMIT          = 0x00000020,
	REVERB_FLAGS_ECHOTIMESCALE         = 0x00000040,
	REVERB_FLAGS_MODULATIONTIMESCALE   = 0x00000080,
};

struct ReverbContainer
{
	ReverbContainer *Next;
	const char *Name;
	uint16_t ID;
	bool Builtin;
	bool Modified;
	REVERB_PROPERTIES Properties;
	bool SoftwareWater;
};

struct SoundListener
{
	FVector3 position;
	FVector3 velocity;
	float angle;
	bool underwater;
	bool valid;
	ReverbContainer *Environment;
	void* ListenerObject;
};

// Default rolloff information.
struct FRolloffInfo
{
	int RolloffType;
	float MinDistance;
	union { float MaxDistance; float RolloffFactor; };
};

struct SoundHandle
{
	void *data;

	bool isValid() const { return data != NULL; }
	void Clear() { data = NULL; }

	bool operator==(const SoundHandle &rhs) const
	{ return data == rhs.data; }
	bool operator!=(const SoundHandle &rhs) const
	{ return !(*this == rhs); }
};

struct FISoundChannel
{
	void		*SysChannel;	// Channel information from the system interface.
	uint64_t	StartTime;		// Sound start time in DSP clocks.

	// The sound interface doesn't use these directly but it needs to pass them to a
	// callback that can't be passed a sound channel pointer
	FRolloffInfo Rolloff;
	float		DistanceScale;
	float		DistanceSqr;
	bool		ManualRolloff;
	EChanFlags	ChanFlags;
};

struct AudioFXValueInt
{
	int type;
	int value;
};

struct AudioFXValueFloat
{
	int type;
	float value;
};

struct AudioFXEffectDef
{
	int type;
	float gain;
	TArray<AudioFXValueInt> intValues;
	TArray<AudioFXValueFloat> floatValues;
};

struct AudioFXEffectChannelDef
{
	const char* name;
	TArray<AudioFXEffectDef> effects;
};

void S_SetSoundPaused(int state);

#undef NOMINMAX

#endif
