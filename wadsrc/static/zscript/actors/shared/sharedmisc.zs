
// Default class for unregistered doomednums -------------------------------

class Unknown : Actor
{
	Default
	{
		Radius 32;
		Height 56;
		Scale 0.2;
		+NOGRAVITY
		+NOBLOCKMAP
		+DONTSPLASH
	}
	States
	{
	Spawn:
		UNKN A -1;
		Stop;
	}
}

// Route node for monster patrols -------------------------------------------

class PatrolPoint : Actor
{
	Default
	{
		Radius 8;
		Height 8;
		Mass 10;
		+NOGRAVITY
		+NOBLOCKMAP
		+DONTSPLASH
		+NOTONAUTOMAP
		RenderStyle "None";
	}
}	

// A special to execute when a monster reaches a matching patrol point ------

class PatrolSpecial : Actor
{
	Default
	{
		Radius 8;
		Height 8;
		Mass 10;
		+NOGRAVITY
		+NOBLOCKMAP
		+DONTSPLASH
		+NOTONAUTOMAP
		RenderStyle "None";
	}
}	

// Map spot ----------------------------------------------------------------

class MapSpot : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOSECTOR
		+NOGRAVITY
		+DONTSPLASH
		+NOTONAUTOMAP
		RenderStyle "None";
		CameraHeight 0;
	}

	override void Tick() {
		Super.Tick();

		// Instead of sleeping
		ChangeStatNum(STAT_FIRST_THINKING - 2);
	}
}	

// same with different editor number for Legacy maps -----------------------

class FS_Mapspot : Mapspot 
{
}

// Map spot with gravity ---------------------------------------------------

class MapSpotGravity : MapSpot
{
	Default
	{
		-NOBLOCKMAP
		-NOSECTOR
		-NOGRAVITY
		+NOTONAUTOMAP
	}
}

// Point Pushers -----------------------------------------------------------

class PointPusher : Actor
{
	Default
	{
		+NOBLOCKMAP
		+INVISIBLE
		+NOCLIP
		+NOTONAUTOMAP
	}
}

class PointPuller : Actor
{
	Default
	{
		+NOBLOCKMAP
		+INVISIBLE
		+NOCLIP
		+NOTONAUTOMAP
	}
}

// Bloody gibs -------------------------------------------------------------

class RealGibs : Actor
{
	Default
	{
		+DROPOFF
		+CORPSE
		+NOTELEPORT
		+DONTGIB
	}
	States
	{
	Spawn:
		goto GenericCrush;
	}
}

// Gibs that can be placed on a map. ---------------------------------------
//
// These need to be a separate class from the above, in case someone uses
// a deh patch to change the gibs, since ZDoom actually creates a gib class
// for actors that get crushed instead of changing their state as Doom did.

class Gibs : RealGibs
{
	Default
	{
		ClearFlags;
	}
}

// Needed for loading Build maps -------------------------------------------

class CustomSprite : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOGRAVITY
	}
	States
	{
	Spawn:
		TNT1 A -1;
		Stop;
	}
	
	override void BeginPlay ()
	{
		Super.BeginPlay ();

		String name = String.Format("BTIL%04d", args[0] & 0xffff);
		picnum = TexMan.CheckForTexture (name, TexMan.TYPE_Build);
		if (!picnum.Exists())
		{
			Destroy();
			return;
		}

		Scale.X = args[2] / 64.;
		Scale.Y = args[3] / 64.;

		int cstat = args[4];
		if (cstat & 2)
		{
			A_SetRenderStyle((cstat & 512) ? 0.6666 : 0.3333, STYLE_Translucent);
		}
		if (cstat & 4) bXFlip = true;
		if (cstat & 8) bYFlip = true;
		if (cstat & 16) bWallSprite = true;
		if (cstat & 32) bFlatSprite = true;
	}
}

// SwitchableDecoration: Activate and Deactivate change state --------------

class SwitchableDecoration : Actor
{
	override void Activate (Actor activator)
	{
		SetStateLabel("Active");
	}

	override void Deactivate (Actor activator)
	{
		SetStateLabel("Inactive");
	}
	
}

class SwitchingDecoration : SwitchableDecoration
{
	override void Deactivate (Actor activator)
	{
	}
}

// Sector flag setter ------------------------------------------------------

class SectorFlagSetter : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOGRAVITY
		+DONTSPLASH
		RenderStyle "None";
	}
	
	override void BeginPlay ()
	{
		Super.BeginPlay ();
		CurSector.Flags |= args[0];
		Destroy();
	}
}

// Marker for sounds : Actor -------------------------------------------------------

class SpeakerIcon : Unknown
{
	States
	{
	Spawn:
		SPKR A -1 BRIGHT;
		Stop;
	}
	Default
	{
		Scale 0.125;
	}
}


class BulletPuff : Actor
{
	Default
	{
		+NOBLOCKMAP
		+NOGRAVITY
		+ALLOWPARTICLES
		+RANDOMIZE
		+ZDOOMTRANS
		RenderStyle "Translucent";
		Alpha 0.5;
		VSpeed 1;
		Mass 5;
	}
	States
	{
	Spawn:
		PUFF A 4 Bright;
		PUFF B 4;
	Melee:
		PUFF CD 4;
		Stop;
	}
}


class ArmorBonus : BasicArmorBonus
{
	Default
	{
		Radius 20;
		Height 16;
		Inventory.Pickupmessage "$GOTARMBONUS";
		Inventory.Icon "BON2A0";
		Armor.Savepercent 33.335;
		Armor.Saveamount 1;
		Armor.Maxsaveamount 200;
		+COUNTITEM
		+INVENTORY.ALWAYSPICKUP
	}
	States
	{
	Spawn:
		BON2 ABCDCB 6;
		loop;
	}
}

class LightGroup : Actor
{
	enum AnimationType
	{
		LGAT_Normal,
		LGAT_Flicker,
		LGAT_FlickerRandom,
		LGAT_Pulse
	}

	int user_animationType;
	int user_animationInterval;
	float user_primaryIntensity;
	float user_secondaryIntensity;
	bool user_allowColorChange;

	void Init()
	{
		cvarReduceFlicker = CVar.FindCVar("g_photosensitivitymode2");

		enabled = !bDormant;

		if (user_animationInterval <= 0)
		{
			user_animationInterval = NormalizeAngle(spawnAngle);
		}

		// For non-pulse types, make sure the secondary intensity is the largest one, mimicking FLightDefaults::OrderIntensities()
		if (user_animationType != LGAT_Pulse && user_primaryIntensity > user_secondaryIntensity)
		{
			float temp = user_primaryIntensity;
			user_primaryIntensity = user_secondaryIntensity;
			user_secondaryIntensity = temp;
		}
	}

	override void Activate(Actor activator)
	{
		activeTics = 0;
		enabled = true;
		ActivateLightGroup();
	}

	override void Deactivate(Actor activator)
	{
		enabled = false;
		DeactivateLightGroup();
	}

	override void Tick() 
	{
		Super.Tick();

		if (!initialized)
		{
			initialized = true;
			Init();
		}

		// Need to update the enabled flag every tic.
		if (enabled)
		{
			ActivateLightGroup();
		}
		else
		{
			DeactivateLightGroup();
		}

		// Mark this LightGroup to render this frame
		if (!bDormant)
		{
			int reduceFlickering = cvarReduceFlicker ? cvarReduceFlicker.GetInt() : 0;

			if (reduceFlickering == 0)
			{
				if (user_animationType == LGAT_Flicker)
				{
					SetIntensity(random(0, 360) < user_animationInterval ? user_primaryIntensity : user_secondaryIntensity);
				}
				else if (user_animationType == LGAT_FlickerRandom)
				{
					float flickerRange = user_secondaryIntensity - user_primaryIntensity;
					float amt = frandom(0, 1);
		
					if (activeTics > user_animationInterval)
					{
						activeTics = 0;
					}

					if (activeTics == 0 || GetIntensity() > user_secondaryIntensity)
					{
						SetIntensity(user_primaryIntensity + (amt * flickerRange));
					}
				}
				else if (user_animationType == LGAT_Pulse)
				{
					float time = float(activeTics) / TICRATE;
					float pulseTime = float(user_animationInterval) / TICRATE;
					float cycle = time / pulseTime;
					float mult = (sin(cycle * 360) + 1) / 2;
					float intensity = user_secondaryIntensity + (user_primaryIntensity - user_secondaryIntensity) * mult;

					SetIntensity(intensity);
				}
			}
			else
			{
				SetIntensity(1.0);
			}

			activeTics++;

			if (GetIntensity() > 0)
			{
				if (user_allowColorChange)
				{
					UpdateTintColor();
				}
			}
		}
	}

	double NormalizeAngle(double value, double start = 0, double end = 360) 
	{
        double width       = end - start;
        double offsetValue = value - start;

        return (offsetValue - (floor(offsetValue / width) * width)) + start;
    }

	private native void UpdateTintColor();
	private native void SetIntensity(float intensity);
	private native float GetIntensity();
	private native void ActivateLightGroup();
	private native void DeactivateLightGroup();

	bool initialized;
	int activeTics;

	bool enabled;

	private CVar cvarReduceFlicker;

}
