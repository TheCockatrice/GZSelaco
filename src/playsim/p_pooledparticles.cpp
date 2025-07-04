#include "doomtype.h"
#include "doomstat.h"

#include "p_pooledparticles.h"
#include "g_levellocals.h"
#include "m_argv.h"
#include "vm.h"
#include "types.h"
#include "texturemanager.h"
#include "d_player.h"
#include "actorinlines.h"

// NL: This is a helper to make sure that the particles are all linked correctly.
//     If something breaks the chain, it can cause particles to stop updating and spawning
//     so if particles suddenly stop appearing, it's recommended to run this after creating or 
//     deleting a particle to track down the culprit. 
//     DO NOT SET THIS TO TRUE UNLESS YOU'RE DIAGNOSING A BUG, IT'S EXPENSIVE!
#define ENABLE_CONTINUITY_CHECKS false

#if ENABLE_CONTINUITY_CHECKS
static int PARTICLE_COUNT = 0;
#endif

// Taken from p_mobj.cpp
#define WATER_SINK_SPEED		0.5

const float DParticleDefinition::INVALID = -99999;
const float DParticleDefinition::BOUNCE_SOUND_ATTENUATION = 1.5f;

DEFINE_FIELD_X(ParticleData, particledata_t, master);
DEFINE_FIELD_X(ParticleData, particledata_t, renderStyle);
DEFINE_FIELD_X(ParticleData, particledata_t, life);
DEFINE_FIELD_X(ParticleData, particledata_t, startLife);
DEFINE_FIELD_X(ParticleData, particledata_t, driftTime);
DEFINE_FIELD_X(ParticleData, particledata_t, pos);
DEFINE_FIELD_X(ParticleData, particledata_t, vel);
DEFINE_FIELD_X(ParticleData, particledata_t, gravity);
DEFINE_FIELD_X(ParticleData, particledata_t, alpha);
DEFINE_FIELD_X(ParticleData, particledata_t, alphaStep);
DEFINE_FIELD_X(ParticleData, particledata_t, scale);
DEFINE_FIELD_X(ParticleData, particledata_t, scaleStep);
DEFINE_FIELD_X(ParticleData, particledata_t, startScale);
DEFINE_FIELD_X(ParticleData, particledata_t, angle);
DEFINE_FIELD_X(ParticleData, particledata_t, angleStep);
DEFINE_FIELD_X(ParticleData, particledata_t, pitch);
DEFINE_FIELD_X(ParticleData, particledata_t, pitchStep);
DEFINE_FIELD_X(ParticleData, particledata_t, roll);
DEFINE_FIELD_X(ParticleData, particledata_t, rollStep);
DEFINE_FIELD_X(ParticleData, particledata_t, bounces);
DEFINE_FIELD_X(ParticleData, particledata_t, maxBounces);
DEFINE_FIELD_X(ParticleData, particledata_t, floorz);
DEFINE_FIELD_X(ParticleData, particledata_t, ceilingz);
DEFINE_FIELD_X(ParticleData, particledata_t, color);
DEFINE_FIELD_X(ParticleData, particledata_t, texture);
DEFINE_FIELD_X(ParticleData, particledata_t, animFrame);
DEFINE_FIELD_X(ParticleData, particledata_t, animTick);
DEFINE_FIELD_X(ParticleData, particledata_t, invalidateTicks);
DEFINE_FIELD_X(ParticleData, particledata_t, sleepFor);
DEFINE_FIELD_X(ParticleData, particledata_t, flags);
DEFINE_FIELD_X(ParticleData, particledata_t, user1);
DEFINE_FIELD_X(ParticleData, particledata_t, user2);
DEFINE_FIELD_X(ParticleData, particledata_t, user3);

DEFINE_ACTION_FUNCTION(_ParticleData, Sleep)
{
	PARAM_SELF_STRUCT_PROLOGUE(particledata_t);
	PARAM_INT(ticks);
	self->definition->SleepParticle(self, ticks);
	return 0;
}

DEFINE_ACTION_FUNCTION(_ParticleData, SpawnActor)
{
	PARAM_SELF_STRUCT_PROLOGUE(particledata_t);
	PARAM_CLASS_NOT_NULL(actorClass, AActor);
	PARAM_FLOAT(offsetX);
	PARAM_FLOAT(offsetY);
	PARAM_FLOAT(offsetZ);
	AActor* actor = self->SpawnActor(actorClass, DVector3(offsetX, offsetY, offsetZ));
	ACTION_RETURN_OBJECT(actor);
}

DEFINE_ACTION_FUNCTION(_ParticleData, PlaySound)
{
	PARAM_SELF_STRUCT_PROLOGUE(particledata_t);
	PARAM_INT(soundid);
	PARAM_FLOAT(volume);
	PARAM_FLOAT(attenuation);
	PARAM_FLOAT(pitch);
	FSoundHandle handle = self->PlaySound(soundid, (float)volume, (float)attenuation, (float)pitch);
	ACTION_RETURN_INT(handle);
};

DEFINE_FIELD_X(ParticleAnimFrame, particleanimframe_t, frame);
DEFINE_FIELD_X(ParticleAnimFrame, particleanimframe_t, duration);

IMPLEMENT_CLASS(DParticleDefinition, false, false)
DEFINE_FIELD(DParticleDefinition, DefaultTexture)
DEFINE_FIELD(DParticleDefinition, DefaultRenderStyle)
DEFINE_FIELD(DParticleDefinition, DefaultParticleFlags)
DEFINE_FIELD(DParticleDefinition, AnimationFrames)
DEFINE_FIELD(DParticleDefinition, StopSpeed)
DEFINE_FIELD(DParticleDefinition, InheritVelocity)
DEFINE_FIELD(DParticleDefinition, MinLife) DEFINE_FIELD(DParticleDefinition, MaxLife)
DEFINE_FIELD(DParticleDefinition, MinAng) DEFINE_FIELD(DParticleDefinition, MaxAng)
DEFINE_FIELD(DParticleDefinition, MinPitch) DEFINE_FIELD(DParticleDefinition, MaxPitch)
DEFINE_FIELD(DParticleDefinition, MinSpeed) DEFINE_FIELD(DParticleDefinition, MaxSpeed)
DEFINE_FIELD(DParticleDefinition, MinFadeVel) DEFINE_FIELD(DParticleDefinition, MaxFadeVel)
DEFINE_FIELD(DParticleDefinition, MinFadeLife) DEFINE_FIELD(DParticleDefinition, MaxFadeLife)
DEFINE_FIELD(DParticleDefinition, BaseScale)
DEFINE_FIELD(DParticleDefinition, MinScale) DEFINE_FIELD(DParticleDefinition, MaxScale)
DEFINE_FIELD(DParticleDefinition, MinFadeScale) DEFINE_FIELD(DParticleDefinition, MaxFadeScale)
DEFINE_FIELD(DParticleDefinition, MinScaleLife) DEFINE_FIELD(DParticleDefinition, MaxScaleLife)
DEFINE_FIELD(DParticleDefinition, MinScaleVel) DEFINE_FIELD(DParticleDefinition, MaxScaleVel)
DEFINE_FIELD(DParticleDefinition, MinRandomBounces) DEFINE_FIELD(DParticleDefinition, MaxRandomBounces)
DEFINE_FIELD(DParticleDefinition, Speed)
DEFINE_FIELD(DParticleDefinition, Drag)
DEFINE_FIELD(DParticleDefinition, MinRoll) DEFINE_FIELD(DParticleDefinition, MaxRoll)
DEFINE_FIELD(DParticleDefinition, MinRollSpeed) DEFINE_FIELD(DParticleDefinition, MaxRollSpeed)
DEFINE_FIELD(DParticleDefinition, RollDamping) DEFINE_FIELD(DParticleDefinition, RollDampingBounce)
DEFINE_FIELD(DParticleDefinition, RestingPitchMin) DEFINE_FIELD(DParticleDefinition, RestingPitchMax) DEFINE_FIELD(DParticleDefinition, RestingPitchSpeed)
DEFINE_FIELD(DParticleDefinition, RestingRollMin) DEFINE_FIELD(DParticleDefinition, RestingRollMax) DEFINE_FIELD(DParticleDefinition, RestingRollSpeed)
DEFINE_FIELD(DParticleDefinition, DriftSlowdown)
DEFINE_FIELD(DParticleDefinition, DriftDirectionSpeed)
DEFINE_FIELD(DParticleDefinition, DriftOscillationSpeed)
DEFINE_FIELD(DParticleDefinition, DriftRandomFactor)
DEFINE_FIELD(DParticleDefinition, DriftVelocityAdjustment)
DEFINE_FIELD(DParticleDefinition, MaxStepHeight)
DEFINE_FIELD(DParticleDefinition, MinGravity) DEFINE_FIELD(DParticleDefinition, MaxGravity)
DEFINE_FIELD(DParticleDefinition, MinBounceFactor) DEFINE_FIELD(DParticleDefinition, MaxBounceFactor)
DEFINE_FIELD(DParticleDefinition, BounceSound)
DEFINE_FIELD(DParticleDefinition, BounceSoundChance)
DEFINE_FIELD(DParticleDefinition, BounceSoundMinSpeed)
DEFINE_FIELD(DParticleDefinition, BounceSoundPitchMin) DEFINE_FIELD(DParticleDefinition, BounceSoundPitchMax)
DEFINE_FIELD(DParticleDefinition, BounceFudge)
DEFINE_FIELD(DParticleDefinition, MinBounceDeflect) DEFINE_FIELD(DParticleDefinition, MaxBounceDeflect)
DEFINE_FIELD(DParticleDefinition, QualityChanceLow)
DEFINE_FIELD(DParticleDefinition, QualityChanceMed)
DEFINE_FIELD(DParticleDefinition, QualityChanceHigh)
DEFINE_FIELD(DParticleDefinition, QualityChanceUlt)
DEFINE_FIELD(DParticleDefinition, QualityChanceInsane)
DEFINE_FIELD(DParticleDefinition, LifeMultLow)
DEFINE_FIELD(DParticleDefinition, LifeMultMed)
DEFINE_FIELD(DParticleDefinition, LifeMultHigh)
DEFINE_FIELD(DParticleDefinition, LifeMultUlt)
DEFINE_FIELD(DParticleDefinition, LifeMultInsane)
DEFINE_FIELD(DParticleDefinition, Flags)
DEFINE_ACTION_FUNCTION(DParticleDefinition, ThinkParticle)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, OnParticleBounce)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);

	self->OnParticleBounce(ParticleData);

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, OnParticleDeath)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);

	ACTION_RETURN_BOOL(true);
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, OnParticleSleep)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, OnParticleCollideWithPlayer)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);
	PARAM_OBJECT(Player, AActor);

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, OnParticleEnterWater)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);
	PARAM_FLOAT(SurfaceHeight);

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, OnParticleExitWater)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);
	PARAM_FLOAT(SurfaceHeight);

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, SetAnimationFrame)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_POINTER(ParticleData, particledata_t);
	PARAM_INT(frame);

	if (frame >= self->AnimationFrames.size())
	{
		ThrowAbortException(X_OTHER, "Tried to set invalid animation frame %d, max is %d", frame, self->AnimationFrames.size());
	}

	ParticleData->animFrame = frame;
	ParticleData->animTick = 0;
	ParticleData->texture = self->AnimationFrames[frame].frame;

	return 0;
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, SpawnParticle)
{
	PARAM_PROLOGUE;
	PARAM_CLASS(definitionClass, DParticleDefinition)
	PARAM_FLOAT(xoff)
	PARAM_FLOAT(yoff)
	PARAM_FLOAT(zoff)
	PARAM_FLOAT(xvel)
	PARAM_FLOAT(yvel)
	PARAM_FLOAT(zvel)
	PARAM_ANGLE(angle)
	PARAM_FLOAT(scale)
	PARAM_INT(flags)
	PARAM_POINTER(refActor, AActor);

	if (!definitionClass || !currentVMLevel)
	{
		return 0;
	}

	if (DParticleDefinition* definition = *currentVMLevel->ParticleDefinitionsByType.CheckKey(definitionClass->TypeName.GetIndex()))
	{
		double bobOffset = 0;

		if (refActor)
		{
			if (!(flags & DPF_ABSOLUTEANGLE)) angle += refActor->Angles.Yaw;
			bobOffset = refActor->GetBobOffset();
		}

		double s = angle.Sin();
		double c = angle.Cos();
		DVector3 pos(xoff, yoff, zoff + bobOffset);
		DVector3 vel(xvel, yvel, zvel);

		//[MC] Code ripped right out of A_SpawnItemEx.
		if (!(flags & DPF_ABSOLUTEPOSITION))
		{
			if (refActor)
			{
				// in relative mode negative y values mean 'left' and positive ones mean 'right'
				// This is the inverse orientation of the absolute mode!
				pos.X = xoff * c + yoff * s;
				pos.Y = xoff * s - yoff * c;
			}
			else
			{
				pos.X = xoff * c - yoff * s;
				pos.Y = xoff * s + yoff * c;
			}
		}
		if (!(flags & DPF_ABSOLUTEVELOCITY))
		{
			vel.X = xvel * c + yvel * s;
			vel.Y = xvel * s - yvel * c;
		}

		P_SpawnDefinedParticle(currentVMLevel, definition, refActor ? refActor->Vec3Offset(pos) : pos, vel, scale, flags, refActor);
	}

	return 0;
}

static void DParticleDefinition_Emit(DParticleDefinition* definition, AActor* master, double chance, int numTries, double angle, double pitch, double speed, double offsetX, double offsetY, double offsetZ, double velocityX, double velocityY, double velocityZ, int flags, double scaleBoost, int particleSpawnOffsets, double particleLifetimeModifier, double additionalAngleScale, double additionalAngleChance)
{
	definition->Emit(master, (float)chance, numTries, angle, pitch, speed, DVector3((float)offsetX, (float)offsetY, (float)offsetZ), DVector3(velocityX, velocityY, velocityZ), flags, (float)scaleBoost, particleSpawnOffsets, (float)particleLifetimeModifier, (float)additionalAngleScale, (float)additionalAngleChance);
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, EmitNative)
{
	PARAM_PROLOGUE;
	PARAM_CLASS(definitionClass, DParticleDefinition);
	PARAM_POINTER(master, AActor);
	PARAM_FLOAT(chance);
	PARAM_INT(numTries);
	PARAM_FLOAT(angle);
	PARAM_FLOAT(pitch);
	PARAM_FLOAT(speed);
	PARAM_FLOAT(offsetX);
	PARAM_FLOAT(offsetY);
	PARAM_FLOAT(offsetZ);
	PARAM_FLOAT(velocityX);
	PARAM_FLOAT(velocityY);
	PARAM_FLOAT(velocityZ);
	PARAM_INT(flags);
	PARAM_FLOAT(scaleBoost);
	PARAM_INT(particleSpawnOffsets);
	PARAM_FLOAT(particleLifetimeModifier);
	PARAM_FLOAT(additionalAngleScale);
	PARAM_FLOAT(additionalAngleChance);

	if (!currentVMLevel || !definitionClass)
	{
		return 0;
	}

	if (DParticleDefinition* definition = *currentVMLevel->ParticleDefinitionsByType.CheckKey(definitionClass->TypeName.GetIndex()))
	{
		DParticleDefinition_Emit(definition, master, chance, numTries, angle, pitch, speed, offsetX, offsetY, offsetZ, velocityX, velocityY, velocityZ, flags, scaleBoost, particleSpawnOffsets, particleLifetimeModifier, additionalAngleScale, additionalAngleChance);
	}

	return 0;
}

static int DParticleDefinition_AddAnimationSequence(DParticleDefinition* self)
{
	if (self->AnimationSequences.Size() >= 255)
	{
		ThrowAbortException(X_OTHER, "Exceeded maximum number of sequences (256) for ParticleDefinition: %s", self->GetClass()->TypeName.GetChars());
	}

	int firstFrame = self->AnimationFrames.Size();
	self->AnimationSequences.Push({ (uint8_t)firstFrame, (uint8_t)firstFrame, (uint8_t)self->AnimationSequences.Size() });

	return (int)self->AnimationSequences.size() - 1;
}

DEFINE_ACTION_FUNCTION_NATIVE(DParticleDefinition, AddAnimationSequence, DParticleDefinition_AddAnimationSequence)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);

	int sequenceIndex = DParticleDefinition_AddAnimationSequence(self);
	
	ACTION_RETURN_INT(sequenceIndex);
}

static void CheckSequence(DParticleDefinition* self, int sequence)
{
	if (sequence > self->AnimationSequences.size())
	{
		ThrowAbortException(X_OTHER, "Animation sequence %d does not exist (Did you call forget to call AddAnimationSequence?)", sequence);
	}
}

static void DParticleDefinition_AddAnimationFrame(DParticleDefinition* self, int sequence, const FString& textureName, int ticks)
{
	CheckSequence(self, sequence);

	if (self->AnimationFrames.Size() >= 255)
	{
		ThrowAbortException(X_OTHER, "Exceeded maximum number of frames for a Particle animation (256) for ParticleDefinition: %s", self->GetClass()->TypeName.GetChars());
	}

	particleanimsequence_t& animSequence = self->AnimationSequences[sequence];
	FTextureID frame = TexMan.CheckForTexture(textureName.GetChars(), ETextureType::Any);

	// Don't add invalid frames
	if (!frame.isValid())
	{
		return;
	}

	self->AnimationFrames.Insert(animSequence.endFrame, { frame, (uint8_t)ticks, (uint8_t)sequence });

	animSequence.endFrame++;
	animSequence.lengthInTicks += ticks;

	// Shift the start and endframes down for any sequences that follow this one
	for (size_t i = sequence + 1; i < self->AnimationSequences.size(); i++)
	{
		self->AnimationSequences[i].startFrame++;
		self->AnimationSequences[i].endFrame++;
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(DParticleDefinition, AddAnimationFrame, DParticleDefinition_AddAnimationFrame)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_UINT(sequence);
	PARAM_STRING(textureName);
	PARAM_UINT(ticks);
	DParticleDefinition_AddAnimationFrame(self, sequence, textureName, ticks);
	return 0;
}

static void DParticleDefinition_AddAnimationFrames(DParticleDefinition* self, int sequence, const FString& spriteName, const FString& frames, int ticks)
{
	for (size_t i = 0; i < frames.Len(); i++)
	{
		std::string textureName = std::string(spriteName.GetChars()) + frames[i] + "0";
		DParticleDefinition_AddAnimationFrame(self, sequence, textureName, (uint8_t)ticks);
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(DParticleDefinition, AddAnimationFrames, DParticleDefinition_AddAnimationFrames)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_UINT(sequence);
	PARAM_STRING(spriteName);
	PARAM_STRING(frames);
	PARAM_UINT(ticksPerFrame);

	DParticleDefinition_AddAnimationFrames(self, sequence, spriteName, frames, ticksPerFrame);

	return 0;
}

static int DParticleDefinition_GetAnimationFrameCount(DParticleDefinition* self, int sequence)
{
	CheckSequence(self, sequence);

	particleanimsequence_t& animSequence = self->AnimationSequences[sequence];
	return animSequence.endFrame - animSequence.startFrame;
}

DEFINE_ACTION_FUNCTION_NATIVE(DParticleDefinition, GetAnimationFrameCount, DParticleDefinition_GetAnimationFrameCount)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_UINT(sequence);

	ACTION_RETURN_INT(DParticleDefinition_GetAnimationFrameCount(self, sequence));
}

static int DParticleDefinition_GetAnimationLengthInTicks(DParticleDefinition* self, int sequence)
{
	CheckSequence(self, sequence);

	return self->AnimationSequences[sequence].lengthInTicks;
}

DEFINE_ACTION_FUNCTION_NATIVE(DParticleDefinition, GetAnimationLengthInTicks, DParticleDefinition_GetAnimationLengthInTicks)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_UINT(sequence);

	ACTION_RETURN_INT(DParticleDefinition_GetAnimationLengthInTicks(self, sequence));
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, GetAnimationSequenceCount)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);

	ACTION_RETURN_INT((int)self->AnimationSequences.size());
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, GetAnimationStartFrame)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_UINT(sequence);

	CheckSequence(self, sequence);

	ACTION_RETURN_INT(self->AnimationSequences[sequence].startFrame);
}

DEFINE_ACTION_FUNCTION(DParticleDefinition, GetAnimationEndFrame)
{
	PARAM_SELF_PROLOGUE(DParticleDefinition);
	PARAM_UINT(sequence);

	CheckSequence(self, sequence);

	ACTION_RETURN_INT(self->AnimationSequences[sequence].endFrame);
}

int ParticleRandom(FRandom& random, int min, int max)
{
	if (min == max)
	{
		return min;
	}

	if (min > max)
	{
		std::swap(min, max);
	}

	return min + random(max - min);
}

int ParticleRandom(int min, int max)
{
	if (min == max)
	{
		return min;
	}

	if (min > max)
	{
		std::swap(min, max);
	}

	return min + M_Random(max - min);
}

float ParticleRandom(float min, float max)
{
	if (min == max)
	{
		return min;
	}

	if (min > max)
	{
		std::swap(min, max);
	}

	return (float)(min + M_Random.GenRand_Real1() * (max - min));
}

double ParticleRandom(double min, double max)
{
	if (min == max)
	{
		return min;
	}

	if (min > max)
	{
		std::swap(min, max);
	}

	return min + M_Random.GenRand_Real1() * (max - min);
}

bool ApproxZero(float v)
{
	return fabsf(v) < VM_EPSILON;
}

bool ApproxZero(double v)
{
	return fabs(v) < VM_EPSILON;
}

bool ApproxZero(FVector3 v)
{
	return ApproxZero(v.X) && ApproxZero(v.Y) && ApproxZero(v.Z);
}

bool ApproxZero(DVector3 v)
{
	return ApproxZero(v.X) && ApproxZero(v.Y) && ApproxZero(v.Z);
}

inline float dsin(float degrees)
{
	return (float)g_sin(degrees * (pi::pif() / 180.0));
}

inline float dcos(float degrees)
{
	return (float)g_cos(degrees * (pi::pif() / 180.0));
}

inline float dasin(float degrees)
{
	return (float)(g_asin(degrees) * (180.0 / pi::pif()));
}

inline float dacos(float degrees)
{
	return (float)(g_acos(degrees) * (180.0 / pi::pif()));
}

inline float datan(float degrees)
{
	return (float)(g_atan(degrees) * (180.0 / pi::pif()));
}

inline float datan2(float y, float x)
{
	return (float)(g_atan2(y, x) * (180.0 / pi::pif()));
}

inline double dsin(double degrees)
{
	return g_sin(degrees * (pi::pif() / 180.0));
}

inline double dcos(double degrees)
{
	return g_cos(degrees * (pi::pif() / 180.0));
}

inline double dasin(double degrees)
{
	return g_asin(degrees) * (180.0 / pi::pif());
}

inline double dacos(double degrees)
{
	return g_acos(degrees) * (180.0 / pi::pif());
}

inline double datan(double degrees)
{
	return g_atan(degrees) * (180.0 / pi::pif());
}

inline double datan2(double y, double x)
{
	return g_atan2(y, x) * (180.0 / pi::pif());
}

FVector3 RotVec(FVector3 p, float angle, float pitch)
{
	float ca = dcos(angle);
	float cp = ApproxZero(pitch) ? 1 : dcos(pitch);
	float sa = dsin(angle);
	float sp = ApproxZero(pitch) ? 0 : dsin(pitch);

	// Pitch
	FVector3 r = p;
	r.Z = p.Z * cp - p.X * sp;
	r.X = p.Z * sp + p.X * cp;

	// Yaw
	p.X = r.X;
	r.X = r.X * ca - p.Y * sa;
	r.Y = p.Y * ca + p.X * sa;

	return r;
}

DVector3 RotVec(DVector3 p, double angle, double pitch)
{
	double ca = dcos(angle);
	double cp = ApproxZero(pitch) ? 1 : dcos(pitch);
	double sa = dsin(angle);
	double sp = ApproxZero(pitch) ? 0 : dsin(pitch);

	// Pitch
	DVector3 r = p;
	r.Z = p.Z * cp - p.X * sp;
	r.X = p.Z * sp + p.X * cp;

	// Yaw
	p.X = r.X;
	r.X = r.X * ca - p.Y * sa;
	r.Y = p.Y * ca + p.X * sa;

	return r;
}

FVector3 VecFromAngle(float yaw, float pitch, float length = 1.0f)
{
	FVector3 r;

	float hcosb = dcos(pitch);
	r.X = dcos(yaw) * hcosb;
	r.Y = dsin(yaw) * hcosb;
	r.Z = -dsin(pitch);

	return r * length;
}

DVector3 VecFromAngle(double yaw, double pitch, double length = 1.0f)
{
	DVector3 r;

	double hcosb = dcos(pitch);
	r.X = dcos(yaw) * hcosb;
	r.Y = dsin(yaw) * hcosb;
	r.Z = -dsin(pitch);

	return r * length;
}

float SpreadRandomizer3::NewRandom(float min /*= 0.0f*/, float max /*= 1.0f*/, float spread /*= 0.15f*/)
{
	float rndav = (delta[0] + delta[1] + delta[2]) * 0.33333333f;
	float rnd = ParticleRandom(min, max);

	float gap = max - min;

	if (fabs(rndav - rnd) < spread || fabs(delta[2] - rnd) < spread)
	{
		rnd += ParticleRandom(min, max);
		rnd = (rnd - (floorf(rnd / gap) * gap)) + min;
	}

	delta[0] = delta[1];
	delta[1] = delta[2];
	delta[2] = rnd;

	return rnd;
}

void particledata_t::Init(FLevelLocals* Level, DVector3 initialPos)
{
	subsector = Level->PointInRenderSubsector(initialPos);
	sector_t* s = subsector->sector;

	master = nullptr;
	renderStyle = definition->DefaultRenderStyle;
	startLife = life = 35;
	pos = prevpos = initialPos;
	vel = DVector3();
	gravity = 0;
	alpha = 1;
	alphaStep = 0;
	fadeAlpha = 1;
	scale = definition->BaseScale;
	scaleStep = FVector2(1, 1);
	startScale = scale;
	fadeScale = FVector2(1, 1);
	angle = 0;
	angleStep = 0;
	pitch = 0;
	pitchStep = 0;
	roll = 0;
	rollStep = 0;
	bounces = 0;
	maxBounces = -1;
	floorz = GetFloorHeight();
	ceilingz = (float)s->ceilingplane.ZatPoint(initialPos);
	restplane = nullptr;
	color = 0xffffff;
	animFrame = 0;
	animTick = 0;
	invalidateTicks = 0;
	sleepFor = 0;
	flags = flags | definition->DefaultParticleFlags | DPF_FIRSTUPDATE;
	user1 = user2 = user3 = user4 = 0;
	lastTexture = {};

	if ((definition->HasFlag(PDF_CHECKWATERSPAWN) || definition->HasFlag(PDF_CHECKWATER) || definition->HasFlag(PDF_NOSPAWNUNDERWATER)) && CheckWater(nullptr))
	{
		flags |= DPF_SPAWNEDUNDERWATER;
		flags |= DPF_UNDERWATER;

		if (definition->HasFlag(PDF_NOSPAWNUNDERWATER))
		{
			// Immediately die if we spawned underwater
			flags |= DPF_DESTROYED;
		}
	}

	if (definition->AnimationFrames.Size())
	{
		flags |= DPF_ANIMATING;
		texture = definition->AnimationFrames[0].frame;
	}
	else
	{
		texture = definition->DefaultTexture;
	}
}

bool particledata_t::CheckWater(double* outSurfaceHeight)
{
	if (!subsector || !subsector->sector)
	{
		return false;
	}

	sector_t* sector = subsector->sector;
	double fh = -FLT_MAX;
	double waterDepth = 0;

	if (sector->MoreFlags & SECMF_UNDERWATER)
	{
		if (outSurfaceHeight) *outSurfaceHeight = pos.Z; // Weird position to choose, but it follows what UpdateWaterDepth does
		return true;
	}
	else
	{
		// Check 3D floors as well!
		for (auto rover : sector->e->XFloor.ffloors)
		{
			if (!(rover->flags & FF_EXISTS)) continue;
			if (rover->flags & FF_SOLID) continue;

			if (!(rover->flags & FF_SWIMMABLE)) continue;

			double ff_bottom = rover->bottom.plane->ZatPoint(pos);
			double ff_top = rover->top.plane->ZatPoint(pos);

			if (ff_top >= pos.Z && ff_bottom < pos.Z)
			{
				if (outSurfaceHeight) *outSurfaceHeight = ff_top;
				return true;
			}
		}
	}

	return false;
}

float particledata_t::GetFloorHeight()
{
	// Only check for 3D floors if we REALLY need to
	if (definition->Flags & PDF_BOUNCEONFLOORS && vel.Z <= VM_EPSILON)
	{
		for (auto rover : subsector->sector->e->XFloor.ffloors)
		{
			if (!(rover->flags & FF_EXISTS)) continue;

			if (rover->flags & FF_SOLID)
			{
				// If the ceiling of the 3D floor is below the particle, then consider us above the floor
				float roverZ = (float)rover->bottom.plane->ZatPoint(pos);
				if (roverZ < pos.Z)
				{
					// Add a liiittle bit of an offset for 3D floors, to make sure particles get the right color
					return (float)rover->top.plane->ZatPoint(pos) + 0.1f;
				}
			}
		}
	}

	return (float)subsector->sector->floorplane.ZatPoint(pos);
}

secplane_t* particledata_t::GetFloorPlane()
{
	for (auto rover : subsector->sector->e->XFloor.ffloors)
	{
		if (!(rover->flags & FF_EXISTS)) continue;

		if (rover->flags & FF_SOLID)
		{
			// If the ceiling of the 3D floor is below the particle, then consider us above the floor
			float roverZ = (float)rover->bottom.plane->ZatPoint(pos);
			if (roverZ < pos.Z)
			{
				return rover->top.plane;
			}
		}
	}

	return &subsector->sector->floorplane;
}

void particledata_t::UpdateUnderwater()
{
	double surfaceHeight = 0;

	bool isUnderwater = CheckWater(&surfaceHeight);
	if (HasFlag(DPF_UNDERWATER) != isUnderwater)
	{
		if (isUnderwater)
		{
			SetFlag(DPF_UNDERWATER);
			definition->CallOnParticleEnterWater(this, surfaceHeight);
		}
		else
		{
			ClearFlag(DPF_UNDERWATER);
			definition->CallOnParticleExitWater(this, surfaceHeight);
		}
	}
}

void particledata_t::UpdateDrift()
{
	// Introduce randomness to add variability
	float randomFactor = ParticleRandom(-definition->DriftRandomFactor, definition->DriftRandomFactor); // Random small offsets for waving

	// Gradually slow down the velocity
	vel *= definition->DriftSlowdown;

	// Simulate chaotic directional changes
	driftTime += definition->DriftDirectionSpeed;

	float pitchOscillation = dcos(driftTime + randomFactor) * ParticleRandom(-definition->DriftOscillationSpeed, definition->DriftOscillationSpeed);
	float angleOscillation = dsin(driftTime + randomFactor) * ParticleRandom(-definition->DriftOscillationSpeed, definition->DriftOscillationSpeed); // Smaller random waves

	// Add extra random shaking to make it messy
	pitch += pitchOscillation + ParticleRandom(-25.0f, 25.0f);
	angle += angleOscillation + ParticleRandom(-25.0f, 25.0f);

	// Calculate velocity relative to the current angle and pitch
	DVector3 direction = 
	{
		dcos(pitch) * dcos(angle),
		dcos(pitch) * dsin(angle),
		dsin(pitch)
	};

	// Update velocity direction
	vel += direction * ParticleRandom(0.05f, definition->DriftVelocityAdjustment); // Random small adjustments
}

AActor* particledata_t::SpawnActor(PClassActor* actorClass, const DVector3& offset)
{
	return Spawn(definition->Level, actorClass, pos + offset, ALLOW_REPLACE);
}

FSoundHandle particledata_t::PlaySound(int soundid, float volume, float attenuation, float pitch)
{
	return S_SoundPitch(definition->Level, pos, CHAN_AUTO, CHANF_OVERLAP, FSoundID::fromInt(soundid), volume, attenuation, pitch);
}

DParticleDefinition::DParticleDefinition()
	: DefaultTexture()
	, DefaultRenderStyle(STYLE_Normal)
{
	// We don't want to save ParticleDefinitions, since the definition could have changed since the game was saved.
	// This way we always use the most up-to-date ParticleDefinition
	ObjectFlags |= OF_Transient;

	// Tell the Garbage Collector not to destroy us, we'll destroy ourselves when the level unloads
	ObjectFlags |= OF_Fixed;
}

DParticleDefinition::~DParticleDefinition()
{

}

void DParticleDefinition::Emit(AActor* master, double chance, int numTries, double angle, double pitch, double speed, DVector3 offset, DVector3 velocity, int flags, float scaleBoost, int particleSpawnOffsets, float particleLifetimeModifier, float additionalAngleScale, float additionalAngleChance)
{
	// Multiply the emit chance based on particle settings
	if (!(flags & PE_IGNORE_CHANCE))
	{
		int spawnSetting = (((flags & PE_ISBLOOD) || (Flags & PDF_ISBLOOD)) ? cvarBloodQuality : cvarParticleIntensity)->ToInt();

		double distance = 0;

		AActor* mo = players[consoleplayer].mo;
		if (master && mo)
		{
			distance = mo ? (mo->Pos() - master->Pos()).Length() : 0;
		}

		switch (spawnSetting)
		{
			case 1:
				chance *= QualityChanceLow;
				chance *= 1.0 - ((std::clamp(distance, 400.0, 1200.0) - 600.0) / 800.0);
				break;
			case 2:
				chance *= QualityChanceMed;
				chance *= 1.0 - ((std::clamp(distance, 512.0, 1500.0) - 600.0) / 988.0);
				break;
			case 4:
				chance *= QualityChanceUlt;
				chance *= 1.0 - ((std::clamp(distance, 600.0, 1800.0) - 600.0) / 1200.0);
				break;
			case 5:
				chance *= QualityChanceInsane;
				if (!(flags & PE_NO_INSANE_PARTICLES)) numTries = (int)ceilf(numTries * 1.2f);    // Spawn additional particles on INSANE
				chance *= 1.0 - ((std::clamp(distance, 700.0, 1800.0) - 600.0) / 1100.0);
				break;
			default:
				chance *= QualityChanceHigh;
				chance *= 1.0 - ((std::clamp(distance, 600.0, 1800.0) - 600.0) / 1200.0);
				break;
		}
	}

	if (master)
	{
		if ((flags & PE_ABSOLUTE_ANGLE) == 0) angle += master->Angles.Yaw.Degrees();
		if ((flags & PE_ABSOLUTE_PITCH) == 0) pitch += master->Angles.Pitch.Degrees();
	}

	int numCreated = 0;

	for (int x = 0; x < numTries; x++)
	{
		SpreadRandomizer3 angleRandomizer;
		SpreadRandomizer3 speedRandomizer;

		if (flags & PE_IGNORE_CHANCE || randomEmit() / 256.0 <= chance)
		{
			DVector3 pos;

			// Find position+offset
			if (flags & PE_ABSOLUTE_POSITION) 
			{
				pos = offset;
			}
			else 
			{
				pos = master ? master->Pos() : offset;

				if (master && flags & PE_ABSOLUTE_OFFSET) 
				{
					pos += offset;
				}
				else if (master) 
				{
					pos += RotVec(offset, angle, pitch);
				}
			}

			// Move around if particle offset is set
			if (particleSpawnOffsets > 0)
			{
				pos.X += ParticleRandom(-(double)particleSpawnOffsets, (double)particleSpawnOffsets);
				pos.Y += ParticleRandom(-(double)particleSpawnOffsets, (double)particleSpawnOffsets);
				pos.Z += ParticleRandom(0.0, (double)particleSpawnOffsets);
			}

			particledata_t* p = NewDefinedParticle(Level, this, true);

			if (!p)
			{
				return;
			}

			p->master = master;
			p->Init(Level, pos);

			double angleDelta = MaxAng - MinAng;
			double pitchDelta = MaxPitch - MinPitch;

			if (additionalAngleScale != 0 && additionalAngleChance > 0)
			{
				if (additionalAngleChance >= ParticleRandom(0.0, 1.0))
				{
					angleDelta += MaxAng * additionalAngleScale;
					pitchDelta += MaxPitch * additionalAngleScale;
				}
			}

			// Configure orientation, used both for angling flatsprites and for fire-direction
			double pAngle = angle + (angleRandomizer.NewRandom(0.0, 1.0) * angleDelta) + MinAng;
			double pPitch = pitch + (angleRandomizer.NewRandom(0.0, 1.0) * pitchDelta) + MinPitch;
			p->angle = (float)pAngle;
			p->roll = (ParticleRandom(0.0f, 1.0f) * (MaxRoll - MinRoll)) + MinRoll;
			p->rollStep = (ParticleRandom(0.0f, 1.0f) * (MaxRollSpeed - MinRollSpeed)) + MinRollSpeed;
			
			if (MinScale.X >= 0)
			{
				p->scale.X *= (ParticleRandom(0.0f, 1.0f) * (MaxScale.X - MinScale.X)) + MinScale.X;
			}
			
			if (!HasFlag(PDF_SQUAREDSCALE) && MinScale.Y >= 0)
			{
				p->scale.Y *= (ParticleRandom(0.0f, 1.0f) * (MaxScale.Y - MinScale.Y)) + MinScale.Y;
			}
			else if (MinScale.X >= 0 && HasFlag(PDF_SQUAREDSCALE))
			{
				p->scale.Y = p->scale.X;
			}

			if (scaleBoost)
			{
				p->scale *= scaleBoost;
			}

			p->startScale = p->scale;
			p->gravity = ParticleRandom(MinGravity, MaxGravity);

			// Set speed
			double pSpeed = Speed;
			if (!(flags & PE_FORCE_VELOCITY)) 
			{
				if (flags & PE_ABSOLUTE_SPEED) 
				{
					pSpeed = speed;
				}
				else 
				{
					// Use the default.speed arg if the random args are invalid (which they are by default)
					if (MinSpeed == INVALID || MaxSpeed == INVALID) 
					{
						pSpeed = flags & PE_SPEED_IS_MULTIPLIER ? pSpeed * speed : pSpeed + speed;
					}
					else 
					{
						pSpeed = (speedRandomizer.NewRandom(0.0, 1.0) * (MaxSpeed - MinSpeed)) + MinSpeed;
						pSpeed = flags & PE_SPEED_IS_MULTIPLIER ? pSpeed * speed : pSpeed + speed;
					}
				}

				p->vel = VecFromAngle(pAngle, pPitch, pSpeed);
				if (master) 
				{
					p->vel += master->Vel * InheritVelocity;
				}
				
				p->vel += flags & PE_ABSOLUTE_VELOCITY ? velocity : (ApproxZero(velocity) ? DVector3() : RotVec(velocity, pAngle, pPitch));
			}
			else 
			{
				if (flags & PE_ABSOLUTE_VELOCITY) 
				{
					p->vel = velocity;
				}
				else 
				{
					p->vel = ApproxZero(velocity) ? DVector3() : RotVec(velocity, pAngle, pPitch);
				}
			}

			// Set life
			if (MinLife > 0 || MaxLife > 0) 
			{
				int minLife = std::max(0, MinLife);
				p->life = ParticleRandom(randomLife, minLife, std::max(minLife, MaxLife));

				switch (cvarParticleLifespan->ToInt())
				{
					case 1:
						p->life = (int16_t)roundf(p->life * LifeMultLow);
						break;
					case 2:
						p->life = (int16_t)roundf(p->life * LifeMultMed);
						break;
					case 3:
						p->life = (int16_t)roundf(p->life * LifeMultHigh);
						break;
					case 4:
						p->life = (int16_t)roundf(p->life * LifeMultUlt) * 2;
						break;
					case 5:
						p->life = (int16_t)roundf(p->life * LifeMultInsane);
						break;
					default:
						p->life = (int16_t)roundf(p->life * LifeMultHigh);
						break;
				}

				if (particleLifetimeModifier > 0) p->life = (int16_t)(p->life * particleLifetimeModifier);
			}
			else 
			{
				p->life = -1;
			}

			p->startLife = p->life;

			// Set bounces
			if (MinRandomBounces >= MaxRandomBounces) 
			{
				p->maxBounces = ParticleRandom(randomBounce, MinRandomBounces, MaxRandomBounces);
			}
			else 
			{
				p->maxBounces = -1;
			}

			//if (returnAr) 
			//{
			//	returnAr.push(p);
			//}

			CallOnCreateParticle(p);
		}
	}
}

void DParticleDefinition::CallInit()
{
	IFVIRTUAL(DParticleDefinition, Init)
	{
		VMValue params[] = { this };
		VMCall(func, params, 1, nullptr, 0);
	}
}

void DParticleDefinition::CallOnCreateParticle(particledata_t* particle)
{
	IFVIRTUAL(DParticleDefinition, OnCreateParticle)
	{
		VMValue params[] = { this, particle };
		VMCall(func, params, 3, nullptr, 0);
	}
}

bool DParticleDefinition::CallOnParticleDeath(particledata_t* particle)
{
	int result = true;

	IFVIRTUAL(DParticleDefinition, OnParticleDeath)
	{
		VMValue params[] = { this, particle };
		VMReturn ret(&result);

		VMCall(func, params, 2, &ret, 1);
	}

	return result;
}

void DParticleDefinition::CallThinkParticle(particledata_t* particle)
{
	IFVIRTUAL(DParticleDefinition, ThinkParticle)
	{
		VMValue params[] = { this, particle };
		VMCall(func, params, 2, nullptr, 0);
	}
}

void DParticleDefinition::CallOnParticleBounce(particledata_t* particle)
{
	IFVIRTUAL(DParticleDefinition, OnParticleBounce)
	{
		VMValue params[] = { this, particle };
		VMCall(func, params, 2, nullptr, 0);
	}
	else
	{
		OnParticleBounce(particle);
	}
}

void DParticleDefinition::CallOnParticleSleep(particledata_t* particle)
{
	IFVIRTUAL(DParticleDefinition, OnParticleSleep)
	{
		VMValue params[] = { this, particle };
		VMCall(func, params, 2, nullptr, 0);
	}
}

void DParticleDefinition::CallOnParticleCollideWithPlayer(particledata_t* particle, AActor* player)
{
	IFVIRTUAL(DParticleDefinition, OnParticleCollideWithPlayer)
	{
		VMValue params[] = { this, particle, player };
		VMCall(func, params, 3, nullptr, 0);
	}
}

void DParticleDefinition::CallOnParticleEnterWater(particledata_t* particle, double surfaceHeight)
{
	IFVIRTUAL(DParticleDefinition, OnParticleEnterWater)
	{
		VMValue params[] = { this, particle, surfaceHeight };
		VMCall(func, params, 3, nullptr, 0);
	}
}

void DParticleDefinition::CallOnParticleExitWater(particledata_t* particle, double surfaceHeight)
{
	IFVIRTUAL(DParticleDefinition, OnParticleExitWater)
	{
		VMValue params[] = { this, particle, surfaceHeight };
		VMCall(func, params, 3, nullptr, 0);
	}
}

void DParticleDefinition::HandleFading(particledata_t* particle)
{
	float velocityFade = HasFlag(PDF_VELOCITYFADE) ? ((float)particle->vel.Length() - MinFadeVel) / (MaxFadeVel - MinFadeVel) : 1.0f;
	float lifeFade = HasFlag(PDF_LIFEFADE) ? float(particle->life - MinFadeLife) / float(MaxFadeLife - MinFadeLife) : 1.0f;
	particle->fadeAlpha = std::clamp(velocityFade * lifeFade, 0.0f, 1.0f);

	if (particle->fadeAlpha < 0.999 && particle->renderStyle != STYLE_Translucent) 
	{
		switch (particle->renderStyle) 
		{
			case STYLE_Normal:
			case STYLE_None:
				particle->renderStyle = STYLE_Translucent;
				break;
			default:
				break;
		}
	}
}

void DParticleDefinition::HandleScaling(particledata_t* particle)
{
	float life = particle->startLife > 0 ? particle->life / float(particle->startLife) : 1.0f;
	float lifeScale = /*bLifeScale ? */float(life - MinScaleLife) / (MaxScaleLife - MinScaleLife)/* : 1.0*/;
	FVector2 mScale = MaxFadeScale;
	FVector2 finalScale = mScale + lifeScale * (MinFadeScale - mScale);

	particle->fadeScale = FVector2(
		std::clamp(finalScale.X, min(MaxFadeScale.X, MinFadeScale.X), max(MaxFadeScale.X, MinFadeScale.X)),
		std::clamp(finalScale.Y, min(MaxFadeScale.Y, MinFadeScale.Y), max(MaxFadeScale.Y, MinFadeScale.Y))
		);
}

void DParticleDefinition::OnParticleBounce(particledata_t* particle)
{
	// Roll speed damping
	particle->rollStep *= 1.0f - RollDampingBounce;

	float speed = (float)particle->vel.Length();

	// Clean up if we reached the bounce limit
	if (particle->maxBounces >= 0) 
	{
		if (particle->bounces++ >= particle->maxBounces)
		{
			if (HasFlag(PDF_FADEATBOUNCELIMIT) && particle->bounces - 1 == particle->maxBounces)
			{
				// Shortcut life
				if (particle->life > MaxFadeLife)
				{
					particle->life = MaxFadeLife;
				}
			}
			else if (!HasFlag(PDF_FADEATBOUNCELIMIT))
			{
				CleanupParticle(particle);
				return;
			}
		}
	}

	if (speed < StopSpeed)
	{
		particle->vel.Zero();

		if (HasFlag(PDF_KILLSTOP))
		{
			CleanupParticle(particle);
			return;
		}
		else
		{
			RestParticle(particle);
		}
	}
	else if (BounceSound && speed > BounceSoundMinSpeed && ParticleRandom(0.0f, 1.0f) <= BounceSoundChance)
	{
		S_SoundPitch(Level, particle->pos, CHAN_AUTO, 0, FSoundID::fromInt(BounceSound), 1.0f, BOUNCE_SOUND_ATTENUATION, ParticleRandom(BounceSoundPitchMin, BounceSoundPitchMax));
	}

	if (RestingRollSpeed >= 0 && speed <= RestingRollSpeed) 
	{
		particle->roll += (((round(particle->roll / 180.0f) * 180.0f) + ParticleRandom(RestingRollMin, RestingRollMax)) - particle->roll) * 0.65f;
	}

	if (RestingPitchSpeed >= 0 && speed <= RestingPitchSpeed) 
	{
		particle->pitch += (((round(particle->pitch / 180.0f) * 180.0f) + ParticleRandom(RestingPitchMin, RestingPitchMax)) - particle->pitch) * 0.65f;
	}
}

int DParticleDefinition::GetParticleLimit()
{
	int numParticles = 1000; // Probably will never be hit, but let's just set it to some safe number just in case

	IFVM(ParticleDefinition, GetParticleLimit)
	{
		VMReturn ret(&numParticles);
		VMCall(func, nullptr, 0, &ret, 1);
	}

	return numParticles;
}

int DParticleDefinition::GetParticleCullLimit()
{
	int numParticles = 500; // Probably will never be hit, but let's just set it to some safe number just in case

	IFVM(ParticleDefinition, GetParticleCullLimit)
	{
		VMReturn ret(&numParticles);
		VMCall(func, nullptr, 0, &ret, 1);
	}

	return numParticles;
}

void DParticleDefinition::RestParticle(particledata_t* particle)
{
	particle->SetFlag(DPF_ATREST);
	particle->prevpos = particle->pos;
	particle->restplane = particle->GetFloorPlane();

	if (HasFlag(PDF_ROLLSTOP)) 
	{
		particle->rollStep = 0;
		particle->pitchStep = 0;
		particle->angleStep = 0;
	}

	if (RestingRollSpeed >= 0) 
	{
		particle->roll = (round(particle->roll / 180.0f) * 180.0f) + ParticleRandom(RestingRollMin, RestingRollMax);
	}

	if (RestingPitchSpeed >= 0) 
	{
		particle->pitch = (round(particle->pitch / 180.0f) * 180.0f) + ParticleRandom(RestingPitchMin, RestingPitchMax);
	}

	if (HasFlag(PDF_SLEEPSTOP) && particle->life > 10) 
	{
		int lifeLeft = particle->life - (MinFadeLife + MaxFadeLife);

		if (lifeLeft > 0) 
		{
			SleepParticle(particle, lifeLeft);
			particle->life = MinFadeLife + MaxFadeLife;
		}
	}
}

void DParticleDefinition::SleepParticle(particledata_t* particle, int sleepTime)
{
	bool wasAsleep = particle->sleepFor > 0;
	particle->sleepFor = sleepTime;

	if (!wasAsleep && sleepTime > 0)
	{
		CallOnParticleSleep(particle);
	}
}

// Clean up this particle as quickly as possible, triggered by the particle limiter
void DParticleDefinition::CullParticle(particledata_t* particle)
{
	if (HasFlag(PDF_LIFEFADE) && particle->life > 0)
	{
		if (particle->life > MaxFadeLife || particle->life > TICRATE)
		{
			// Impose a practical limit of 1 second for fading out
			particle->life = min(TICRATE, MaxFadeLife);
			particle->sleepFor = 0;    // Just in case we slept for whatever reason
		}
		// Otherwise this particle should be already on it's way out
	}
	else
	{
		particle->SetFlag(DPF_DESTROYED);
	}
}

void DParticleDefinition::CleanupParticle(particledata_t* particle)
{
	particle->SetFlag(DPF_NOPROCESS);

	if (CallOnParticleDeath(particle))
	{
		// Mark this as destroyed so we can destroy it the next update
		particle->SetFlag(DPF_DESTROYED);
	}
}

#if ENABLE_CONTINUITY_CHECKS
void CheckContinuity(FLevelLocals* Level)
{
	particlelevelpool_t& pool = Level->DefinedParticlePool;

	int count = 0;
	int i = pool.ActiveParticles;
	int prev = NO_PARTICLE;
	bool inNoMansLand = false;

	while (i != NO_PARTICLE)
	{
		if (i == pool.InactiveParticles)
		{
			inNoMansLand = true;
		}

		particledata_t& particle = pool.Particles[i];

		if (!inNoMansLand)
		{
			assert(particle.tprev == prev);

			if (prev != NO_PARTICLE)
			{
				assert(pool.Particles[prev].tnext == i);
			}
		}

		prev = i;
		i = particle.tnext;
		count++;
	}

	assert(count == 0 || pool.OldestParticle != NO_PARTICLE);
	assert(count < pool.Particles.size() || pool.InactiveParticles == NO_PARTICLE);

	assert(count == PARTICLE_COUNT);
}
#endif

particledata_t* NewDefinedParticle(FLevelLocals* Level, DParticleDefinition* definition, bool replace /* = false */)
{
	particledata_t* result = nullptr;

	particlelevelpool_t& pool = Level->DefinedParticlePool;

	// Array's filled up
	if (pool.InactiveParticles == NO_PARTICLE)
	{
		if (replace)
		{
			result = &pool.Particles[pool.OldestParticle];

			// There should be NO_PARTICLE for the oldest's tnext
			if (result->tprev != NO_PARTICLE)
			{
				// tnext: youngest to oldest
				// tprev: oldest to youngest

				// 2nd oldest -> oldest
				particledata_t* nbottom = &pool.Particles[result->tprev];
				nbottom->tnext = NO_PARTICLE;

				// now oldest becomes youngest
				pool.OldestParticle = result->tprev;
				result->tnext = pool.ActiveParticles;
				result->tprev = NO_PARTICLE;
				pool.ActiveParticles = uint32_t(result - pool.Particles.Data());

				// youngest -> 2nd youngest
				particledata_t* ntop = &pool.Particles[result->tnext];
				ntop->tprev = pool.ActiveParticles;
			}
			// [MC] Future proof this by resetting everything when replacing a particle.
			auto tnext = result->tnext;
			auto tprev = result->tprev;
			*result = {};
			result->definition = definition;
			result->tnext = tnext;
			result->tprev = tprev;

#if ENABLE_CONTINUITY_CHECKS
			CheckContinuity(Level);
#endif
		}

		return result;
	}

	// Array isn't full.
	uint32_t current = pool.ActiveParticles;
	result = &pool.Particles[pool.InactiveParticles];
	result->definition = definition;
	result->master = nullptr;
	pool.InactiveParticles = result->tnext;
	result->tnext = current;
	result->tprev = NO_PARTICLE;
	pool.ActiveParticles = uint32_t(result - pool.Particles.Data());

	if (current != NO_PARTICLE) // More than one active particles
	{
		particledata_t* next = &pool.Particles[current];
		next->tprev = pool.ActiveParticles;
	}
	else // Just one active particle
	{
		pool.OldestParticle = pool.ActiveParticles;
	}

#if ENABLE_CONTINUITY_CHECKS
	PARTICLE_COUNT++;
	CheckContinuity(Level);
#endif

	return result;
}

void P_InitParticleDefinitions(FLevelLocals* Level)
{
	PClass* baseClass = PClass::FindClass("ParticleDefinition");

	for (unsigned int i = 0; i < PClass::AllClasses.Size(); i++)
	{
		PClass* cls = PClass::AllClasses[i];

		if (cls != baseClass && cls->IsDescendantOf(baseClass))
		{
			DParticleDefinition* definition = (DParticleDefinition*)cls->CreateNew();
			definition->Level = Level;
			definition->cvarParticleIntensity = FindCVar("r_particleIntensity", nullptr);
			definition->cvarParticleLifespan = FindCVar("r_particlelifespan", nullptr);
			definition->cvarBloodQuality = FindCVar("r_bloodquality", nullptr);
			definition->CallInit();

			Level->ParticleDefinitionsByType.Insert(cls->TypeName.GetIndex(), definition);
		}
	}

	int numParticles = DParticleDefinition::GetParticleLimit();

	Level->DefinedParticlePool.Particles.Resize(numParticles);
	P_ClearAllDefinedParticles(Level);

#if ENABLE_CONTINUITY_CHECKS
	PARTICLE_COUNT = 0;
#endif
}

void P_ClearAllDefinedParticles(FLevelLocals* Level)
{
	particlelevelpool_t& pool = Level->DefinedParticlePool;

	int i = 0;
	pool.OldestParticle = NO_PARTICLE;
	pool.ActiveParticles = NO_PARTICLE;
	pool.InactiveParticles = 0;
	for (auto& p : pool.Particles)
	{
		p = {};
		p.tprev = i - 1;
		p.tnext = ++i;
	}
	pool.Particles.Last().tnext = NO_PARTICLE;
	pool.Particles.Data()->tprev = NO_PARTICLE;
}

void P_DestroyAllParticleDefinitions(FLevelLocals* Level)
{
	TMapIterator<int, DParticleDefinition*> it(Level->ParticleDefinitionsByType);
	TMap<int, DParticleDefinition*>::Pair* pair;
	while (it.NextPair(pair))
	{
		pair->Value->Destroy();
	}

	Level->ParticleDefinitionsByType.Clear();
	Level->DefinedParticlePool.Particles.Clear();
}

void P_ResizeDefinedParticlePool(FLevelLocals* Level, int particleLimit)
{
	particlelevelpool_t& pool = Level->DefinedParticlePool;
	TArray<particledata_t> newParticles(particleLimit, true);

	int added = 0;
	int oldestParticle = NO_PARTICLE;

	for (uint16_t i = pool.ActiveParticles; i != NO_PARTICLE; i = pool.Particles[i].tnext)
	{
		if (added >= particleLimit)
		{
			break;
		}

		particledata_t& particle = newParticles[added];

			oldestParticle = added;

		particle = pool.Particles[i];
		particle.tprev = added - 1;
		particle.tnext = (particle.tnext != NO_PARTICLE) ? (added + 1) : NO_PARTICLE;

		added++;
	}

	assert(oldestParticle != NO_PARTICLE || added == 0);

#if ENABLE_CONTINUITY_CHECKS
	PARTICLE_COUNT = added;
#endif

	pool.OldestParticle = oldestParticle;
	pool.ActiveParticles = added > 0 ? 0 : NO_PARTICLE;
	pool.InactiveParticles = added < particleLimit ? added : NO_PARTICLE;

	for (; added < particleLimit; added++)
	{
		particledata_t& particle = newParticles[added];
		particle = {};
		particle.tprev = added - 1;
		particle.tnext = added + 1;
	}

	newParticles.Last().tnext = NO_PARTICLE;
	newParticles.Data()->tprev = NO_PARTICLE;

	Level->DefinedParticlePool.Particles = newParticles;

#if ENABLE_CONTINUITY_CHECKS
	CheckContinuity(Level);
#endif
}

// Group particles by subsectors. Because particles are always
// in motion, there is little benefit to caching this information
// from one frame to the next.
// [MC] VisualThinkers hitches a ride here

void P_FindDefinedParticleSubsectors(FLevelLocals* Level)
{
	if (Level->DefinedParticlesInSubsec.Size() < Level->subsectors.Size())
	{
		Level->DefinedParticlesInSubsec.Reserve(Level->subsectors.Size() - Level->DefinedParticlesInSubsec.Size());
	}

	uint16_t* b2 = &Level->DefinedParticlesInSubsec[0];
	for (size_t i = 0; i < Level->DefinedParticlesInSubsec.Size(); ++i)
	{
		b2[i] = NO_PARTICLE;
	}

	fillshort(&Level->DefinedParticlesInSubsec[0], Level->subsectors.Size(), NO_PARTICLE);

	particlelevelpool_t& pool = Level->DefinedParticlePool;

	for (uint16_t i = pool.ActiveParticles; i != NO_PARTICLE; i = pool.Particles[i].tnext)
	{
		// Try to reuse the subsector from the last portal check, if still valid.
		if (pool.Particles[i].subsector == nullptr) pool.Particles[i].subsector = Level->PointInRenderSubsector(pool.Particles[i].pos);
		int ssnum = pool.Particles[i].subsector->Index();
		pool.Particles[i].snext = Level->DefinedParticlesInSubsec[ssnum];
		Level->DefinedParticlesInSubsec[ssnum] = i;
	}
}

bool P_DestroyDefinedParticle(FLevelLocals* Level, int particleIndex)
{
	particlelevelpool_t& pool = Level->DefinedParticlePool;
	particledata_t& particle = pool.Particles[particleIndex];

	if (!particle.HasFlag(DPF_DESTROYED) && particle.definition && !particle.definition->CallOnParticleDeath(&particle))
	{
		return false;
	}

	if (particle.tprev != NO_PARTICLE)
		pool.Particles[particle.tprev].tnext = particle.tnext;
	else
		pool.ActiveParticles = particle.tnext;

	if (particle.tnext != NO_PARTICLE)
	{
		particledata_t& next = pool.Particles[particle.tnext];
		next.tprev = particle.tprev;
	}

	if (particleIndex == pool.OldestParticle)
	{
		pool.OldestParticle = particle.tprev;
	}

	particle = {};
	particle.tnext = pool.InactiveParticles;
	pool.InactiveParticles = particleIndex;

#if ENABLE_CONTINUITY_CHECKS
	PARTICLE_COUNT--;
	CheckContinuity(Level);
#endif

	return true;
}

void P_ThinkDefinedParticles(FLevelLocals* Level)
{
	particlelevelpool_t* pool = &Level->DefinedParticlePool;

	int particleCount = 0;
	int particleLimit = DParticleDefinition::GetParticleLimit();
	int cullLimit = DParticleDefinition::GetParticleCullLimit();

	if (particleLimit != pool->Particles.Size())
	{
		P_ResizeDefinedParticlePool(Level, particleLimit);
	}

	int i = pool->ActiveParticles;
	particledata_t* particle = nullptr;
	while (i != NO_PARTICLE)
	{
		particle = &pool->Particles[i];
		DParticleDefinition* definition = particle->definition;

		int particleIndex = i;
		i = particle->tnext;
		if (Level->isFrozen() && !(particle->flags & DPF_NOTIMEFREEZE))
		{
			continue;
		}

		particle->prevpos = particle->pos;
		float prevFloorZ = particle->floorz;

		if (particle->sleepFor > 0)
		{
			particle->sleepFor--;

			if (particle->HasFlag(DPF_ATREST))
			{
				particle->floorz = (float)particle->restplane->ZatPoint(particle->pos) + 0.1f;

				// We're setting the vel rather than the pos so that we get proper interpolation for moving floors
				particle->pos.Z += particle->vel.Z;
				particle->vel.Z = (particle->floorz - prevFloorZ);
			}

			continue;
		}

		int prevAnimFrame = particle->animFrame;

		definition->CallThinkParticle(particle);

		if (particle->life > 0)
		{
			particle->life--;
		}

		if ((particle->life == 0) || particle->HasFlag(DPF_DESTROYED))
		{ // The particle has expired, so free it
			if (P_DestroyDefinedParticle(Level, particleIndex))
			{
				continue;
			}
		}

		if (particle->HasFlag(DPF_ANIMATING))
		{
			uint8_t animFrameCount = (uint8_t)definition->AnimationFrames.Size();
			if (definition->AnimationSequences.size() > 0 && particle->animFrame < animFrameCount)
			{
				const particleanimframe_t& animFrame = definition->AnimationFrames[particle->animFrame];

				uint8_t sequenceIndex = animFrame.sequence;
				const particleanimsequence_t& sequence = definition->AnimationSequences[sequenceIndex];

				// Don't update the frame on the first update, or if the animFrame has been changed during CallThinkParticle
				if (!particle->HasFlag(DPF_FIRSTUPDATE) && particle->animFrame == prevAnimFrame)
				{
					if (++particle->animTick >= animFrame.duration)
					{
						particle->animTick = 0;
						particle->animFrame++;

						if (particle->animFrame >= sequence.endFrame)
						{
							if (particle->HasFlag(DPF_LOOPANIMATION))
							{
								// Loop the animation if it's finished
								particle->animFrame = sequence.startFrame;
							}
							else
							{
								// Go back to the previous frame and stop
								particle->animFrame--;
								particle->ClearFlag(DPF_ANIMATING);
							}
						}

						particle->texture = definition->AnimationFrames[particle->animFrame].frame;
					}
				}
				else
				{
					particle->texture = definition->AnimationFrames[particle->animFrame].frame;
				}
			}
		}

		if (definition->DriftVelocityAdjustment > 0) 
		{
			particle->UpdateDrift();
		}

		particle->vel *= 1.0f - definition->Drag;

		particle->alpha += particle->alphaStep;
		particle->scale = FVector2(particle->scale.X * particle->scaleStep.X, particle->scale.Y * particle->scaleStep.Y);
		particle->angle += particle->angleStep;
		particle->pitch += particle->pitchStep;
		particle->roll += particle->rollStep;

		// Handle crossing a line portal
		double movex = (particle->pos.X - particle->prevpos.X) + particle->vel.X;
		double movey = (particle->pos.Y - particle->prevpos.Y) + particle->vel.Y;
		DVector2 newxy = Level->GetPortalOffsetPosition(particle->prevpos.X, particle->prevpos.Y, movex, movey);
		particle->pos.X = newxy.X;
		particle->pos.Y = newxy.Y;

		particle->subsector = Level->PointInRenderSubsector(particle->pos);
		sector_t* s = particle->subsector->sector;

		if (particle->gravity != 0)
		{
			if (!particle->HasFlag(DPF_ATREST))
			{
				if ((definition->HasFlag(PDF_CHECKWATERSPAWN) && particle->HasFlag(DPF_SPAWNEDUNDERWATER)) || definition->HasFlag(PDF_CHECKWATER))
				{
					particle->UpdateUnderwater();
				}

				if (particle->HasFlag(DPF_UNDERWATER))
				{
					// Do sinking logic, cut down from AActor::FallAndSink
					double sinkspeed = -WATER_SINK_SPEED * 0.01;

					if (particle->vel.Z < sinkspeed)
					{ // Dropping too fast, so slow down toward sinkspeed.
						particle->vel.Z -= max(sinkspeed * 2, -8.);
						if (particle->vel.Z > sinkspeed)
						{
							particle->vel.Z = sinkspeed;
						}
					}
					else if (particle->vel.Z > sinkspeed)
					{ // Dropping too slow/going up, so trend toward sinkspeed.
						particle->vel.Z += max(sinkspeed / 3, -8.);
						if (particle->vel.Z < sinkspeed)
						{
							particle->vel.Z = sinkspeed;
						}
					}
				}
				else
				{
					float gravity = (float)(Level->gravity * s->gravity * (double)particle->gravity * 0.00125);
					particle->vel.Z -= gravity;
				}
			}
		}

		particle->floorz = particle->GetFloorHeight();
		particle->ceilingz = (float)s->ceilingplane.ZatPoint(particle->pos);

		if (particle->HasFlag(DPF_ATREST))
		{
			// We're setting the vel rather than the pos so that we get proper interpolation for moving floors
			particle->pos.Z += particle->vel.Z;
			particle->vel.Z = (particle->floorz - prevFloorZ);
		}
		else
		{
			particle->pos.Z += particle->vel.Z;
		}

		// Handle crossing a sector portal.
		if (!s->PortalBlocksMovement(sector_t::ceiling))
		{
			if (particle->pos.Z > s->GetPortalPlaneZ(sector_t::ceiling))
			{
				particle->pos += s->GetPortalDisplacement(sector_t::ceiling);
				particle->subsector = NULL;
			}
		}
		else if (!s->PortalBlocksMovement(sector_t::floor))
		{
			if (particle->pos.Z < s->GetPortalPlaneZ(sector_t::floor))
			{
				particle->pos += s->GetPortalDisplacement(sector_t::floor);
				particle->subsector = NULL;
			}
		}

		bool bounced = false;

		if (!particle->HasFlag(DPF_NOPROCESS))
		{
			if (particle->flags & DPF_COLLIDEWITHPLAYER)
			{
				player_t* player = Level->Players[0];
				if (player && player->mo)
				{
					DVector3 pos = player->mo->Pos();
					double radius = player->mo->radius;
					double height = player->mo->Height;

					double minx = pos.X - radius;
					double maxx = pos.X + radius;
					double miny = pos.Y - radius;
					double maxy = pos.Y + radius;
					double minz = pos.Z;
					double maxz = pos.Z + height;

					if (particle->pos.X >= minx && particle->pos.X <= maxx &&
						particle->pos.Y >= miny && particle->pos.Y <= maxy &&
						particle->pos.Z >= minz && particle->pos.Z <= maxz)
					{
						definition->CallOnParticleCollideWithPlayer(particle, player->mo);
					}
				}
			}

			if (definition->Flags & PDF_BOUNCEONFLOORS)
			{
				if (particle->pos.Z < particle->floorz && particle->vel.Z < 0)
				{
					float bounceFactor = ParticleRandom(definition->MinBounceFactor, definition->MaxBounceFactor);

					if (particle->pos.Z - particle->vel.Z - particle->floorz >= -definition->MaxStepHeight)
					{
						particle->pos.Z = particle->floorz;
						particle->vel.Z *= -(bounceFactor * ParticleRandom(1.0f - definition->BounceFudge, 1.0f));
						bounced = true;
						particle->invalidateTicks = 0;
					}
					else
					{
						particle->vel.Z = 0;
						particle->invalidateTicks++;
					}

					particle->vel.X *= bounceFactor;
					particle->vel.Y *= bounceFactor;

					DVector2 deflected = particle->vel.XY().Rotated(ParticleRandom(definition->MinBounceDeflect, definition->MaxBounceDeflect));
					particle->vel.X = deflected.X;
					particle->vel.Y = deflected.Y;
				}
				else if (particle->pos.Z > particle->ceilingz && particle->vel.Z > 0)
				{
					float bounceFactor = ParticleRandom(definition->MinBounceFactor, definition->MaxBounceFactor);

					if (particle->pos.Z - particle->vel.Z - particle->ceilingz <= -definition->MaxStepHeight)
					{
						particle->pos.Z = particle->ceilingz;
						particle->vel.Z *= -(bounceFactor * ParticleRandom(1.0f - definition->BounceFudge, 1.0f));
						bounced = true;
						particle->invalidateTicks = 0;
					}
					else
					{
						particle->vel.Z = 0;
						particle->invalidateTicks++;
					}

					particle->vel.X *= bounceFactor;
					particle->vel.Y *= bounceFactor;

					DVector2 deflected = particle->vel.XY().Rotated(ParticleRandom(definition->MinBounceDeflect, definition->MaxBounceDeflect));
					particle->vel.X = deflected.X;
					particle->vel.Y = deflected.Y;
				}
				else
				{
					particle->invalidateTicks = 0;
				}

				if (bounced)
				{
					definition->CallOnParticleBounce(particle);
				}

				bool onFloor = abs(particle->pos.Z - particle->floorz) < 0.01 || abs(particle->prevpos.Z - particle->floorz) < 0.01;

				// Check for becoming at rest while on the floor
				if (!bounced && !particle->HasFlag(DPF_ATREST) && onFloor)
				{
					if (particle->vel.Length() < definition->StopSpeed) 
					{
						particle->vel.Zero();

						if (definition->HasFlag(PDF_KILLSTOP)) 
						{
							definition->CleanupParticle(particle);
							continue;
						}
						else 
						{
							definition->RestParticle(particle);
						}
					}
				}


				if (particle->invalidateTicks > 5 && P_DestroyDefinedParticle(Level, particleIndex))
				{
					continue;
				}
			}
		}

		if (particle->HasFlag(DPF_DESTROYED))
		{
			P_DestroyDefinedParticle(Level, particleIndex);
			continue;
		}

		if (definition->HasFlag(PDF_VELOCITYFADE) || definition->HasFlag(PDF_LIFEFADE))
		{
			definition->HandleFading(particle);
		}

		if (definition->HasFlag(PDF_LIFESCALE)) 
		{
			definition->HandleScaling(particle);
		}

		if (definition->HasFlag(PDF_DIRFROMMOMENTUM))
		{
			DVector3 dir = particle->vel.Unit();
			particle->angle = (float)datan2(dir.Y, dir.X);
			particle->pitch = -(float)dasin(dir.Z);
			particle->roll = 90;

			if (bounced && definition->HasFlag(PDF_INSTANTBOUNCE))
			{
				particle->prevpos = particle->pos;
			}
		}

		if (particleCount > cullLimit)
		{
			definition->CullParticle(particle);
		}

		particle->ClearFlag(DPF_FIRSTUPDATE);

		particleCount++;
	}
}

particledata_t* P_SpawnDefinedParticle(FLevelLocals* Level, DParticleDefinition* definition, const DVector3& pos, const DVector3& vel, double scale, int flags, AActor* refActor)
{
	particledata_t* particle = NewDefinedParticle(Level, definition, (bool)(flags & DPF_REPLACE));

	if (particle)
	{
		particle->master = refActor;
		particle->flags = flags;

		particle->Init(Level, pos);
		particle->vel = vel;
		particle->scale.X *= (float)scale;
		particle->scale.Y *= (float)scale;
		particle->startScale = particle->scale;
		particle->gravity = ParticleRandom(definition->MinGravity, definition->MaxGravity);

		particle->startLife = particle->life = std::max(ParticleRandom(definition->MinLife, definition->MaxLife), 0);
		particle->roll = ParticleRandom(definition->MinRoll, definition->MaxRoll);
		particle->rollStep = ParticleRandom(definition->MinRollSpeed, definition->MaxRollSpeed);
		particle->maxBounces = ParticleRandom(definition->MinRandomBounces, definition->MaxRandomBounces);

		if (definition->cvarParticleLifespan)
		{
			switch (definition->cvarParticleLifespan->ToInt())
			{
				case 1:
					particle->life = (int16_t)round(particle->life * definition->LifeMultLow);
					break;
				case 2:
					particle->life = (int16_t)round(particle->life * definition->LifeMultMed);
					break;
				case 4:
					particle->life = (int16_t)round(particle->life * definition->LifeMultUlt);
					break;
				case 5:
					particle->life = (int16_t)round(particle->life * definition->LifeMultInsane);
					break;
				default:
					particle->life = (int16_t)round(particle->life * definition->LifeMultHigh);
					break;
			}
		}

		definition->CallOnCreateParticle(particle);
	}

	return particle;
}

static FLevelLocals* ParticleDefinitionLoadingLevel = nullptr;

void P_LoadDefinedParticles(FSerializer& arc, FLevelLocals* Level, const char* key)
{
	assert(arc.isReading());

	if (!arc.isReading())
	{
		return;
	}

	// Reinitialize the pools, since even if they've already been initialized, the ParticleDefinitions
	// would have been destroyed by the load, so we need to recreate them.
	P_InitParticleDefinitions(Level);

	particlelevelpool_t& pool = Level->DefinedParticlePool;

	ParticleDefinitionLoadingLevel = Level;
	Serialize(arc, key, pool, &pool);
	ParticleDefinitionLoadingLevel = nullptr;

	// Go through all the particles and destroy any that are lacking a definition
	int i = pool.ActiveParticles;
	while (i != NO_PARTICLE)
	{
		particledata_t& particle = pool.Particles[i];
	
		int particleIndex = i;
		i = particle.tnext;
		
		if (!particle.definition)
		{
			P_DestroyDefinedParticle(Level, particleIndex);
		}
	}
}


FSerializer& Serialize(FSerializer& arc, const char* key, particlelevelpool_t& lp, particlelevelpool_t* def)
{
	if (arc.BeginObject(key))
	{
		if (arc.BeginArray("particles"))
		{
			if (arc.isWriting())
			{
				// Write out the particles from newest to oldest and then stop, so we only store the particles we *need*
				for (uint16_t i = lp.ActiveParticles; i != NO_PARTICLE; i = lp.Particles[i].tnext)
				{
					particledata_t& p = lp.Particles[i];
					arc(nullptr, p);
				}
			}
			else
			{
				unsigned int count = min(arc.ArraySize(), lp.Particles.Size());

				lp.OldestParticle = NO_PARTICLE;
				lp.ActiveParticles = count > 0 ? 0 : NO_PARTICLE;
				lp.InactiveParticles = 0;

				for (unsigned int i = 0; i < count; i++)
				{
					particledata_t& p = lp.Particles[i];
					arc(nullptr, p);

					// Since the particles are stored newest-to-oldest, we can figure out the tprev and tnext
					p.tprev = i > 0 ? i - 1 : NO_PARTICLE;
					p.tnext = i < count - 1 ? i + 1 : NO_PARTICLE;

					lp.OldestParticle = i;
					lp.InactiveParticles = i < lp.Particles.Size() - 1 ? i + 1 : NO_PARTICLE;
				}

#if ENABLE_CONTINUITY_CHECKS
				PARTICLE_COUNT = count;
#endif
			}

			arc.EndArray();
		}

		arc.EndObject();
	}
	return arc;
}

FSerializer& Serialize(FSerializer& arc, const char* key, particledata_t& p, particledata_t* def)
{
	if (arc.BeginObject(key))
	{
		if (arc.isWriting())
		{
			FName definitionName;
			if (p.definition)
			{
				definitionName = p.definition->GetClass()->TypeName;
			}

			arc("definition", definitionName);
		}
		else
		{
			FName definitionName;
			arc("definition", definitionName);

			if (DParticleDefinition** definition = ParticleDefinitionLoadingLevel->ParticleDefinitionsByType.CheckKey(definitionName.GetIndex()))
			{
				p.definition = *definition;
			}
			else
			{
				p.definition = nullptr;
			}
		}

		arc ("master", p.master)
			("renderStyle", p.renderStyle)
		    ("life", p.life)
			("startLife", p.startLife)
			("prevpos", p.prevpos)
			("pos", p.pos)
			("vel", p.vel)
			("alpha", p.alpha)
			("alphastep", p.alphaStep)
			("scale", p.scale)
			("scalestep", p.scaleStep)
			("startScale", p.startScale)
			("pitch", p.angle)
			("pitchstep", p.angleStep)
			("pitch", p.pitch)
			("pitchstep", p.pitchStep)
			("roll", p.roll)
			("rollstep", p.rollStep)
			("bounces", p.bounces)
			("maxbounces", p.maxBounces)
			("floorz", p.floorz)
			("ceilingz", p.ceilingz)
			("color", p.color)
			("texture", p.texture)
			("animframe", p.animFrame)
			("animTick", p.animTick)
			("invalidateTicks", p.invalidateTicks)
			("flags", p.flags)
			("user1", p.user1)
			("user2", p.user2)
			("user3", p.user3)
			("user4", p.user4)
			// Deliberately not saving tprev or tnext, since they're calculated during load
			// Deliberately not saving subsector or snext, since they're calculated every frame.
			.EndObject();
	}
	return arc;
}
