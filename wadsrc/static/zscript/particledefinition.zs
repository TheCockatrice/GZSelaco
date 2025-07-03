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

struct ParticleData
{
    native uint8        RenderStyle;
    native int16        Life;       // Tics to live, -1 = forever
    native int16        StartLife;  // The life this particle started with
    native int16        DriftTime;
    native vector3      Pos;
    native vector3      Vel;
    native float        Gravity;
    native float        Alpha, AlphaStep;
    native FVector2     Scale, ScaleStep, StartScale;
    native float        Angle, AngleStep;
    native float        Pitch, PitchStep;
    native float        Roll, RollStep;
    native int16        Bounces, MaxBounces;
    native float        FloorZ, CeilingZ;
    native color        Color;
    native TextureID    Texture;
    native uint8        AnimFrame, AnimTick;
    native uint8        InvalidateTicks;
    native uint16       SleepFor;
    native int          Flags;
    native int          User1;
    native int          User2;
    native int          User3;

    void Destroy()
    {
        Flags |= DPF_DESTROYED;
    }

    native void Sleep(int ticks);
    void Wake()
    {
        SleepFor = 0;
    }

    // Helper to initialize the lifetime of a particle
    void InitLife(int v)
    {
        StartLife = v;
        Life = v;
    }

    // Helper so we don't need to set the X and Y separately
    void SetScale(float s)
    {
        Scale.X = s;
        Scale.Y = s;
    }

    // Helper so we don't need to set the X and Y separately
    void SetScaleStep(float s)
    {
        ScaleStep.X = s;
        ScaleStep.Y = s;
    }

    float GetLifeDelta()
    {
        if (StartLife <= 0)
        {
            return 0;
        }

        return Life / float(StartLife);
    }

    void ChangeVelocity(Actor ref, double x = 0, double y = 0, double z = 0, int flags = 0)
    {
		if (ref == NULL)
		{
			return;
		}

		let newvel = (x, y, z);

        vector3 dir;
        if (Vel.LengthSquared() != 0)
        {
            dir = Vel.Unit();
        }

		double sina = sin(ref.Angle);
		double cosa = cos(ref.Angle);

		if (flags & CVF_RELATIVE)	// relative axes - make x, y relative to particle's current angle
		{
			newvel.X = x * cosa - y * sina;
			newvel.Y = x * sina + y * cosa;
		}

		if (flags & CVF_REPLACE)	// discard old velocity - replace old velocity with new velocity
		{
			Vel = newvel;
		}
		else	// add new velocity to old velocity
		{
			Vel += newvel;
		}
    }

    // Helpers for setting flags
    bool HasFlag(int flag) const { return Flags & flag; }
	void SetFlag(int flag) { Flags |= flag; }
	void ClearFlag(int flag) { Flags &= ~flag; }

    native Actor SpawnActor(class<Actor> actor, double offsetX = 0.0, double offsetY = 0.0, double offsetZ = 0.0);
    native void PlaySound(Sound sound_id, float volume = 1, float attenuation = ATTN_NORM, float pitch = 0.0);
}

class ParticleDefinition native play
{
    native TextureID       DefaultTexture;
    native ERenderStyle    DefaultRenderStyle;
    native int             DefaultParticleFlags;
    
    native float StopSpeed;
	native float InheritVelocity;
	native int MinLife, MaxLife;
	native float MinAng, MaxAng;
	native float MinPitch, MaxPitch;
	native float MinSpeed, MaxSpeed;
	native float MinFadeVel, MaxFadeVel;
	native int MinFadeLife, MaxFadeLife;
    native FVector2 BaseScale;
	native FVector2 MinScale, MaxScale;
	native FVector2 MinFadeScale, MaxFadeScale;
	native float MinScaleLife, MaxScaleLife;
	native float MinScaleVel, MaxScaleVel;
	native int MinRandomBounces, MaxRandomBounces;
    native float Speed;
    native float Drag;
	native float MinRoll, MaxRoll;
	native float MinRollSpeed, MaxRollSpeed;
	native float RollDamping, RollDampingBounce;
	native float RestingPitchMin, RestingPitchMax, RestingPitchSpeed;
	native float RestingRollMin, RestingRollMax, RestingRollSpeed;

    native float DriftSlowdown;
	native float DriftDirectionSpeed;
	native float DriftOscillationSpeed;
	native float DriftRandomFactor;
	native float DriftVelocityAdjustment;

    native float MaxStepHeight;
    native float MinGravity, MaxGravity;
    native float MinBounceFactor, MaxBounceFactor;
	native Sound BounceSound;
	native float BounceSoundChance;
	native float BounceSoundMinSpeed;
	native float BounceSoundPitchMin, BounceSoundPitchMax;
	native float BounceFudge;
	native float MinBounceDeflect, MaxBounceDeflect;

    native int Flags;

	native float QualityChanceLow, QualityChanceMed, QualityChanceHigh, QualityChanceUlt, QualityChanceInsane;
	native float LifeMultLow, LifeMultMed, LifeMultHigh, LifeMultUlt, LifeMultInsane;

    static void Emit(class<ParticleDefinition> definition, Actor master = NULL, float chance = 1.0, int numTries = 1, float angle = 0, float pitch = 0, float speed = 0, Vector3 offset = (0,0,0), Vector3 velocity = (0,0,0), int flags = 0, float scaleBoost = 0, float additionalAngleScale = 0, float additionalAngleChance = 0)
    {
        EmitNative(definition, master, chance, numTries, angle, pitch, speed, offset.x, offset.y, offset.z, velocity.x, velocity.y, velocity.z, flags, scaleBoost, 0, 0, additionalAngleScale, additionalAngleChance);
    }

    void SetLife(int life)                                  { MinLife = life; MaxLife = life; }
    void SetBaseScale(float scale)                          { BaseScale = (scale, scale); }
    void SetBaseScaleXY(float x, float y)                   { BaseScale = (x, y); }
    void SetGravity(float gravity)                          { MinGravity = gravity; MaxGravity = gravity; }
    
    void SetQualityChances(float low, float med, float high, float ult, float insane)
    {
        QualityChanceLow = low;
        QualityChanceMed = med;
        QualityChanceHigh = high;
        QualityChanceUlt = ult;
        QualityChanceInsane = insane;
    }
    
    void SetLifespanMultipliers(float low, float med, float high, float ult, float insane)
    {
        LifeMultLow = low;
        LifeMultMed = med;
        LifeMultHigh = high;
        LifeMultUlt = ult;
        LifeMultInsane = insane;
    }

    void RandomGravity(float min, float max)                { MinGravity = min; MaxGravity = max; }
    void RandomAngle(float min, float max)                  { MinAng = min; MaxAng = max; }
    void RandomPitch(float min, float max)                  { MinPitch = min; MaxPitch = max; }
    void RandomSpeed(float min, float max)                  { MinSpeed = min; MaxSpeed = max; }
    void RandomLife(int min, int max)                       { MinLife = min; MaxLife = max; }
    void RandomRoll(float min, float max)                   { MinRoll = min; MaxRoll = max; }
    void RandomRollSpeed(float min, float max)              { MinRollSpeed = min; MaxRollSpeed = max;}
    void RandomScaleX(float min, float max)                 { MinScale.X = min; MaxScale.X = max; }
    void RandomScaleY(float min, float max)                 { MinScale.Y = min; MaxScale.Y = max; }
    void RandomScale(float xMin, float xMax, float yMin, float yMax) { MinScale.x = xMin; MaxScale.x = xMax; MinScale.y = yMin; MaxScale.y = yMax; }
    void BounceDeflect(float min, float max)                { MinBounceDeflect = min; MaxBounceDeflect = max; }
    void FadeVelRange(float min, float max)                 { MinFadeVel = min; MaxFadeVel = max; }
    void FadeLifeRange(int min, int max)                    { MinFadeLife = min; MaxFadeLife = max; }
    void ScaleLifeRange(float min, float max)               { MinScaleLife = min; MaxScaleLife = max; }
    void ScaleRangeX(float min, float max)                  { MinFadeScale.X = min; MaxFadeScale.X = max; }
    void ScaleRangeY(float min, float max)                  { MinFadeScale.Y = min; MaxFadeScale.Y = max; }
    void ScaleRange(float xMin, float xMax, float yMin, float yMax) { MinFadeScale.x = xMin; MaxFadeScale.x = xMax; MinFadeScale.y = yMin; MaxFadeScale.y = yMax; }
    void BounceFactor(float factor)                         { MinBounceFactor = factor; MaxBounceFactor = factor; }
    void RandomBounceFactor(float min, float max)           { MinBounceFactor = min; MaxBounceFactor = max; }
    void BouncesRange(int min, int max)                     { MinRandomBounces = min; MaxRandomBounces = max; }
    void BounceSoundPitchRange(float min, float max)        { BounceSoundPitchMin = min; BounceSoundPitchMax = max; }
    void RestorePitch(float min, float max, float speed)    { RestingPitchMin = min; RestingPitchMax = max; RestingPitchSpeed = speed; }
    void RestoreRoll(float min, float max, float speed)     { RestingRollMin = min; RestingRollMax = max; RestingRollSpeed = speed; }

    virtual void Init() { }
    virtual void OnCreateParticle(in out ParticleData particle, Actor refActor) { }
    
    native virtual void ThinkParticle(in out ParticleData particle);
    native virtual void OnParticleBounce(in out ParticleData particle);
    native virtual bool OnParticleDeath(in out ParticleData particle);
    native virtual void OnParticleSleep(in out ParticleData particle);
    native virtual void OnParticleCollideWithPlayer(in out ParticleData particle, Actor player);
    native virtual void OnParticleEnterWater(in out ParticleData particle, float surfaceHeight);
    native virtual void OnParticleExitWater(in out ParticleData particle, float surfaceHeight);

    native int AddAnimationSequence();
    native void AddAnimationFrame(int sequence, string textureName, int ticks = 1);
    native void AddAnimationFrames(int sequence, string spriteName, string frames, int ticksPerFrame);
    native int GetAnimationFrameCount(int sequence);
    native int GetAnimationLengthInTicks(int sequence);

    native int GetAnimationSequenceCount();
    native int GetAnimationStartFrame(int sequence);
    native int GetAnimationEndFrame(int sequence);

    void PlayAnimationSequence(in out ParticleData particle, int sequence, bool looping = true)
    {
        SetAnimationFrame(particle, GetAnimationStartFrame(sequence));
        particle.SetFlag(DPF_ANIMATING);

        if (looping)
        {
            particle.SetFlag(DPF_LOOPANIMATION);
        }
        else
        {
            particle.ClearFlag(DPF_LOOPANIMATION);
        }
    }

    native void SetAnimationFrame(in out ParticleData particle, int frame);

    void StopAnimation(in out ParticleData particle)
    {
        particle.ClearFlag(DPF_ANIMATING);
    }

    // Particles are removed immediately when going over this number
    static const int particleLimits[] = 
    { 
        500,    // Low
        800,    // Med
        1600,   // High
        2000,   // Ultra    
        6000    // Insane
    };

    // Particles start to fade out where possible when going over this number
    static const int cullLimits[] = 
    { 
        250,    // Low
        400,    // Med
        800,    // High
        1000,   // Ultra   
        3000    // Insane
    };

    static int GetParticleLimit()
    {
        int currentParticleSetting = clamp(CVar.FindCVar("r_particleIntensity").GetInt() - 1, 0, 5);
        return ParticleDefinition.particleLimits[currentParticleSetting];
    }

    static int GetParticleCullLimit()
    {
        int currentParticleSetting = clamp(CVar.FindCVar("r_particleIntensity").GetInt() - 1, 0, 5);
        return ParticleDefinition.cullLimits[currentParticleSetting];
    }




    // Helper for selecting a random frame for a sprite
    static TextureID SelectRandomFrame(string sprite, string frames)
    {
        string textureName = string.Format("%s%c0", sprite, frames.ByteAt(Random(0, frames.Length() - 1)));
        
        return TexMan.CheckForTexture(textureName, TexMan.TYPE_ANY);
    }

    // Helpers for setting flags
    bool HasFlag(EParticleDefinitionFlags flag) const { return Flags & flag; }
	void SetFlag(EParticleDefinitionFlags flag) { Flags |= flag; }
	void ClearFlag(EParticleDefinitionFlags flag) { Flags &= ~flag; }
    
    bool HasDefaultParticleFlag(int flag) const { return DefaultParticleFlags & flag; }
	void SetDefaultParticleFlag(int flag) { DefaultParticleFlags |= flag; }
	void ClearDefaultParticleFlag(int flag) { DefaultParticleFlags &= ~flag; }

    native static native bool SpawnParticle(class<ParticleDefinition> definition, double xoff = 0, double yoff = 0, double zoff = 0, double xvel = 0, double yvel = 0, double zvel = 0, double angle = 0, double scale = 1, int flags = 0, actor refActor = null);

    // Don't use this directly, use either ParticleDefinition.Emit or ParticleDefinitionEmitter.Emit
    // Not private because ParticleDefinitionEmitter needs access to it.
    native static void EmitNative(class<ParticleDefinition> definition, Actor master, double chance, int numTries, double angle, double pitch, double speed, double offsetX, double offsetY, double offsetZ, double velocityX, double velocityY, double velocityZ, int flags, float scaleBoost, int particleSpawnOffsets, float particleLifetimeModifier, float additionalAngleScale, float additionalAngleChance);
}
