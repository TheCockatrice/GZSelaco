#pragma once

#include "vectors.h"
#include "doomdef.h"
#include "renderstyle.h"
#include "dthinker.h"
#include "palettecontainer.h"
#include "animations.h"
#include "p_local.h"
#include "p_effect.h"
#include "actor.h"
#include "dobject.h"
#include "serializer.h"

class DParticleDefinition;

enum EParticleDefinitionFlags
{
	PDF_KILLSTOP				= 1 << 0,	// Kill the particle when it stops moving
	PDF_DIRFROMMOMENTUM			= 1 << 1,	// Always face the direction of travel
	PDF_INSTANTBOUNCE			= 1 << 2,	// Clear interpolation on bounce, useful when using DirFromMomentum
	PDF_VELOCITYFADE			= 1 << 3,	// Fade out between min and max fadeVel
	PDF_LIFEFADE				= 1 << 4,	// Fade out between min and max fadeLife
	PDF_ROLLSTOP				= 1 << 5,	// Stop rolling when coming to rest
	PDF_SLEEPSTOP				= 1 << 6,	// Use Sleep() when the particle comes to rest for the rest of it's idle lifetime. Warning: Will probably float if on a moving surface
	PDF_NOTHINK					= 1 << 7,	// Don't call ThinkParticle
	PDF_FADEATBOUNCELIMIT		= 1 << 8,	// Once the bounce limit is reached, start fading out using lifetime settings
	PDF_IMPORTANT				= 1 << 9,	// If not flagged as important, particles will not spawn when reaching the limits
	PDF_SQUAREDSCALE			= 1 << 10,	// Random scale is always square (Match X and Y scale)
	PDF_ISBLOOD					= 1 << 11,	// Treat this particle as blood. Use different spawn variables such as r_BloodQuality instead of r_particleIntensity
	PDF_LIFESCALE				= 1 << 12,	// Follow min/max scale over lifetime
	PDF_BOUNCEONFLOORS			= 1 << 13,	// Bounce on floors
    PDF_CHECKWATERSPAWN         = 1 << 14,  // Only update the water state if the particle originally spawned underwater
    PDF_CHECKWATER              = 1 << 15,  // Constantly check if the particle has entered water, regardless of where it spawned
    PDF_NOSPAWNUNDERWATER       = 1 << 16,  // Immediately destroy the particle if it's created underwater
};

enum EDefinedParticleFlags
{
	DPF_FULLBRIGHT				= 1 << 0,
	DPF_ABSOLUTEPOSITION		= 1 << 1,
	DPF_ABSOLUTEVELOCITY		= 1 << 2,
	DPF_ABSOLUTEACCEL			= 1 << 3,
	DPF_ABSOLUTEANGLE			= 1 << 4,
	DPF_NOTIMEFREEZE			= 1 << 5,
	DPF_ROLL					= 1 << 6,
	DPF_REPLACE					= 1 << 7,
	DPF_NO_XY_BILLBOARD			= 1 << 8,
	DPF_FACECAMERA				= 1 << 9,
	DPF_NOFACECAMERA			= 1 << 10,
	DPF_ROLLCENTER				= 1 << 11,
	DPF_NOMIPMAP				= 1 << 12,
    DPF_FIRSTUPDATE             = 1 << 13,  // Particle has just been created
    DPF_ANIMATING               = 1 << 14,  // Particle is animating
	DPF_ATREST					= 1 << 15,	// Particle has settled and isn't moving
	DPF_NOPROCESS				= 1 << 16,	// This is set when killing the particle, so it may be used after for sprite things
	DPF_CLEANEDUP				= 1 << 17,	// Used by the particle limiter, this value is set once the particle has been marked for cleaning
	DPF_DESTROYED				= 1 << 18,	// Particle has been destroyed, and should be removed from the particle list next update
	DPF_ISBLOOD					= 1 << 19,	// Treat this particle as blood, same as bIsBlood on SelacoParticle
	DPF_FORCETRANSPARENT		= 1 << 20,	// Force this particle to render as transparent if it's set to None or Normal
	DPF_FLAT					= 1 << 21,	// Display as flat
	DPF_COLLIDEWITHPLAYER		= 1 << 22,	// Allow the particle to collide with the player
	DPF_LOOPANIMATION			= 1 << 23,	// Loop the animation once finished
    DPF_UNDERWATER              = 1 << 24,  // Particle is underwater
	DPF_SPAWNEDUNDERWATER		= 1 << 25,	// Particle originally spawned underwater
};

enum EParticleEmitterFlags 
{
    PE_SPEED_IS_MULTIPLIER      = 1 << 0,
    PE_ABSOLUTE_PITCH           = 1 << 1,
    PE_ABSOLUTE_ANGLE           = 1 << 2,
    PE_ABSOLUTE_OFFSET          = 1 << 3,	// Offset position from emitter in absolute coords
    PE_FORCE_VELOCITY           = 1 << 4,	// Do not generate velocity from speed, set it with velocity arg
    PE_ABSOLUTE_POSITION        = 1 << 5,	// Position particle at coordinates specified by offset arg
    PE_IGNORE_CHANCE            = 1 << 6,	// Force fire, don't use random chance
    PE_ABSOLUTE_SPEED           = 1 << 7,	// Ignore random speed
    PE_ABSOLUTE_VELOCITY        = 1 << 8,	// Velocity is added in absolute coords, ignoring emitter orientation
    PE_NO_INSANE_PARTICLES      = 1 << 9,	// Do not spawn additional particles on INSANE
    PE_ISBLOOD                  = 1 << 10	// Treat this particle as blood, same as bIsBlood on SelacoParticle
};

struct particledata_t
{
	DParticleDefinition* definition;			// +8
	AActor* master;								// +8
	uint8_t renderStyle;						// +1
	int16_t life;								// +2 
	int16_t startLife;							// +2 
	int16_t driftTime;							// +2
	DVector3 prevpos;							// +24
	DVector3 pos;								// +24
	DVector3 vel;								// +24
	float gravity;								// +4
	float alpha, alphaStep;						// +8 
	float fadeAlpha;							// +4
	FVector2 scale, scaleStep, startScale;		// +24
	FVector2 fadeScale;							// +8
	float angle, angleStep;						// +8
	float pitch, pitchStep;						// +8
	float roll, rollStep;						// +8 
	int16_t bounces, maxBounces;				// +4
	float floorz, ceilingz;						// +8
	secplane_t* restplane;						// +8
	int color;									// +4
	FTextureID texture, lastTexture;			// +8
	uint8_t animFrame, animTick;				// +2 
	uint8_t invalidateTicks;					// +1
	uint16_t sleepFor;							// +2
	uint32_t flags;								// +4 
	int user1, user2, user3, user4;				// +16
	uint16_t tnext, tprev;						// +4 

	subsector_t* subsector;						// +8 
	uint16_t snext;								// +2 

	void Init(FLevelLocals* Level, DVector3 initialPos);

	bool CheckWater(double* outSurfaceHeight);
	float GetFloorHeight();
	secplane_t* GetFloorPlane();
	void UpdateUnderwater();
	void UpdateDrift();

	AActor* SpawnActor(PClassActor* actorClass, const DVector3& offset);
	FSoundHandle PlaySound(int soundid, float volume, float attenuation, float pitch);

	bool HasFlag(int flag) const { return flags & flag; }
	void SetFlag(int flag) { flags |= flag; }
	void ClearFlag(int flag) { flags &= ~flag; }
};

struct particleanimsequence_t
{
	uint8_t startFrame;
	uint8_t endFrame;
	uint8_t lengthInTicks;
};

struct particleanimframe_t
{
	FTextureID frame;
	uint8_t duration;
	uint8_t sequence;
};

struct SpreadRandomizer3
{
	float delta[3];

	float NewRandom(float min = 0.0f, float max = 1.0f, float spread = 0.15f);
};

class DParticleDefinition : public DObject
{
	DECLARE_CLASS(DParticleDefinition, DObject);

public:
	DParticleDefinition();
	virtual ~DParticleDefinition();

	FTextureID DefaultTexture;
	ERenderStyle DefaultRenderStyle;
	int DefaultParticleFlags = DPF_ROLL | DPF_LOOPANIMATION;

	static const float INVALID;
	static const float BOUNCE_SOUND_ATTENUATION;

	float StopSpeed = 0.4f;
	float InheritVelocity = 0;
	int MinLife = -1, MaxLife = -1;
	float MinAng = 0, MaxAng = 0;
	float MinPitch = 0, MaxPitch = 0;
	float MinSpeed = INVALID, MaxSpeed = INVALID;
	float MinFadeVel = 0.4f, MaxFadeVel = 2;
	int MinFadeLife = 0, MaxFadeLife = 25;
	FVector2 BaseScale = FVector2(1, 1);
	FVector2 MinScale = FVector2(-1, -1), MaxScale = FVector2(-1, -1);
	FVector2 MinFadeScale = FVector2(1, 1), MaxFadeScale = FVector2(0.2f, 0.2f);
	float MinScaleLife = 0, MaxScaleLife = 0.1f;
	float MinScaleVel = 0, MaxScaleVel = 0;
	int MinRandomBounces = -1, MaxRandomBounces = -1;
	float Speed = 5;
	float Drag = 0;
	float MinRoll = 0, MaxRoll = 0;
	float MinRollSpeed = 0, MaxRollSpeed = 0;
	float RollDamping = 0, RollDampingBounce = 0.3f;
	float RestingPitchMin = 0, RestingPitchMax = 0, RestingPitchSpeed = -1;
	float RestingRollMin = 0, RestingRollMax = 0, RestingRollSpeed = -1;

	float DriftSlowdown = 0.96f;
	float DriftDirectionSpeed = 35;
	float DriftOscillationSpeed = 45;
	float DriftRandomFactor = 25;
	float DriftVelocityAdjustment = 0;

	float MaxStepHeight = 8;
	float MinGravity = 0, MaxGravity = 0;
	float MinBounceFactor = 0.5f, MaxBounceFactor = 0.7f;
	int BounceSound = 0;
	float BounceSoundChance = 0.2f;
	float BounceSoundMinSpeed = 2;
	float BounceSoundPitchMin = 0.95f, BounceSoundPitchMax = 1.05f;
	float BounceFudge = 0.15f;
	float MinBounceDeflect = -25, MaxBounceDeflect = 25;

	float QualityChanceLow = 0.1f, QualityChanceMed = 0.25f, QualityChanceHigh = 0.6f, QualityChanceUlt = 1, QualityChanceInsane = 9999;
	float LifeMultLow = 0.07f, LifeMultMed = 0.1f, LifeMultHigh = 0.3f, LifeMultUlt = 1, LifeMultInsane = 3;

	uint32_t Flags = 
		PDF_BOUNCEONFLOORS |
		PDF_INSTANTBOUNCE |
		PDF_SQUAREDSCALE |
		PDF_CHECKWATERSPAWN;

	TArray<particleanimsequence_t> AnimationSequences;
	TArray<particleanimframe_t> AnimationFrames;

	void Emit(AActor* master, double chance, int numTries, double angle, double pitch, double speed, DVector3 offset, DVector3 velocity, int flags, float scaleBoost, int particleSpawnOffsets, float particleLifetimeModifier, float additionalAngleScale, float additionalAngleChance);

	void CallInit();
	void CallOnCreateParticle(particledata_t* particle);
	bool CallOnParticleDeath(particledata_t* particle);
	void CallThinkParticle(particledata_t* particle);
	void CallOnParticleBounce(particledata_t* particle);
	void CallOnParticleSleep(particledata_t* particle);
	void CallOnParticleCollideWithPlayer(particledata_t* particle, AActor* player);
	void CallOnParticleEnterWater(particledata_t* particle, double surfaceHeight);
	void CallOnParticleExitWater(particledata_t* particle, double surfaceHeight);

	void HandleFading(particledata_t* particle);
	void HandleScaling(particledata_t* particle);
	void OnParticleBounce(particledata_t* particle);

	static int GetParticleLimit();
	static int GetParticleCullLimit();

	void RestParticle(particledata_t* particle);
	void SleepParticle(particledata_t* particle, int sleepTime);
	void CullParticle(particledata_t* particle);
	void CleanupParticle(particledata_t* particle);

	bool HasFlag(EParticleDefinitionFlags flag) const { return Flags & flag; }
	void SetFlag(EParticleDefinitionFlags flag) { Flags |= flag; }
	void ClearFlag(EParticleDefinitionFlags flag) { Flags &= ~flag; }

	FLevelLocals* Level;
	FBaseCVar* cvarParticleIntensity;
	FBaseCVar* cvarParticleLifespan;
	FBaseCVar* cvarBloodQuality;

	FRandom randomEmit;
	FRandom randomLife;
	FRandom randomBounce;
};

struct particlelevelpool_t
{
	uint32_t					OldestParticle; // Oldest particle for replacing with SPF_REPLACE
	uint32_t					ActiveParticles;
	uint32_t					InactiveParticles;
	TArray<particledata_t>		Particles;
};

inline particledata_t* NewDefinedParticle(FLevelLocals* Level, DParticleDefinition* definition, bool replace = false);
void P_InitParticleDefinitions(FLevelLocals* Level);
void P_ClearAllDefinedParticles(FLevelLocals* Level);
void P_DestroyAllParticleDefinitions(FLevelLocals* Level);
void P_ResizeDefinedParticlePool(FLevelLocals* Level, int particleLimit);

void P_FindDefinedParticleSubsectors(FLevelLocals* Level);
bool P_DestroyDefinedParticle(FLevelLocals* Level, int particleIndex);
void P_ThinkDefinedParticles(FLevelLocals* Level);
particledata_t* P_SpawnDefinedParticle(FLevelLocals* Level, DParticleDefinition* definition, const DVector3& pos, const DVector3& vel, double scale, int flags, AActor* refActor);

void P_LoadDefinedParticles(FSerializer& arc, FLevelLocals* Level, const char* key);
FSerializer& Serialize(FSerializer& arc, const char* key, particlelevelpool_t& lp, particlelevelpool_t* def);
FSerializer& Serialize(FSerializer& arc, const char* key, particledata_t& p, particledata_t* def);