/*
** i_joystick.cpp
**
**---------------------------------------------------------------------------
** Copyright 2005-2016 Randy Heit
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
*/
#include <SDL.h>

#include "basics.h"
#include "cmdlib.h"

#include "m_joy.h"
#include "i_joystick.h"
#include "keydef.h"
#include "d_event.h"


EXTERN_CVAR(Bool, joy_feedback)
EXTERN_CVAR(Float, joy_feedback_scale)

CUSTOM_CVAR(Int, joy_sdl_queuesize, 5, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
	if (self > 10) self = 10;
	if (self < 1) self = 1;
}

CUSTOM_CVAR(Int, joy_sdl_squaremove, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
	if (self > 2) self = 2;
	if (self < 0) self = 0;
}

CUSTOM_CVAR(Int, joy_sdl_squarelook, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
	if (self > 2) self = 2;
	if (self < 0) self = 0;
}

void PostDeviceChangeEvent() {
	event_t event = { EV_DeviceChange };
	D_PostEvent(&event);
}



class SDLInputJoystick: public SDLInputJoystickBase
{
public:
	SDLInputJoystick(int DeviceIndex)
	{
		this->DeviceIndex = DeviceIndex;
		this->Multiplier = 1.0f;

		Device = SDL_JoystickOpen(DeviceIndex);
		if(Device != NULL)
		{
			NumAxes = SDL_JoystickNumAxes(Device);
			NumHats = SDL_JoystickNumHats(Device);
			
			SetDefaultConfig();

			Connected = true;
			DeviceID = SDL_JoystickGetDeviceInstanceID(DeviceIndex);
		}
	}
	~SDLInputJoystick()
	{
		if(Device != NULL)
			M_SaveJoystickConfig(this);
		SDL_JoystickClose(Device);
	}

	bool IsValid() const
	{
		return Device != NULL;
	}

	FString GetName()
	{
		return SDL_JoystickName(Device);
	}
	
	int GetNumAxes() override
	{
		return NumAxes + NumHats*2;
	}

	// Used by the saver to not save properties that are at their defaults.
	bool IsSensitivityDefault()
	{
		return Multiplier == 1.0f;
	}
	bool IsAxisDeadZoneDefault(int axis)
	{
		if(axis >= 5)
			return Axes[axis].DeadZone == 0.1f;
		return Axes[axis].DeadZone == DefaultAxes[axis].DeadZone;
	}
	bool IsAxisMapDefault(int axis)
	{
		if(axis >= 5)
			return Axes[axis].GameAxis == JOYAXIS_None;
		return Axes[axis].GameAxis == DefaultAxes[axis].GameAxis;
	}
	bool IsAxisScaleDefault(int axis)
	{
		if(axis >= 5)
			return Axes[axis].Multiplier == 1.0f;
		return Axes[axis].Multiplier == DefaultAxes[axis].Multiplier;
	}
	bool IsAxisAccelerationDefault(int axis) override
	{
		if (axis >= 5) return Axes[axis].Acceleration == 0.0f;
		return Axes[axis].Acceleration == DefaultAxes[axis].Acceleration;
	}


	void SetDefaultConfig()
	{
		// Remove existing axis information
		Axes.Clear();

		// Create the axes
		for(int i = 0; i < GetNumAxes(); i++) {
			AxisInfo info;
			if(i < NumAxes)
				info.Name.Format("Axis %d", i+1);
			else
				info.Name.Format("Hat %d (%c)", (i-NumAxes)/2 + 1, (i-NumAxes)%2 == 0 ? 'x' : 'y');

			//info.DeadZone = MIN_DEADZONE;
			info.Value = 0.0;
			info.RawValue = 0.0;
			info.ButtonValue = 0;
			if (i >= 5) {
				info.GameAxis = JOYAXIS_None;
				info.Acceleration = 0.0f;
				info.DeadZone = 0.1;
				info.Multiplier = 1.0f;
			} else {
				info.GameAxis = DefaultAxes[i].GameAxis;
				info.Acceleration = DefaultAxes[i].Acceleration;
				info.DeadZone = DefaultAxes[i].DeadZone;
				info.Multiplier = DefaultAxes[i].Multiplier;
			}

			Axes.Push(info);
		}
	}

	void AddAxes(float axes[NUM_JOYAXIS])
	{
		// Add to game axes.
		for (int i = 0; i < GetNumAxes(); ++i)
		{
			if (Axes[i].GameAxis != JOYAXIS_None) {
				if (Axes[i].GameAxis == JOYAXIS_Forward || Axes[i].GameAxis == JOYAXIS_Side) {
					axes[Axes[i].GameAxis] -= float(Axes[i].Value * Axes[i].Multiplier);
				}
				else {
					axes[Axes[i].GameAxis] -= float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
				}
			}
		}
	}


	inline void SquareStick(EJoyAxis axisX, EJoyAxis axisY) {
		// Find the two axes for directional movement
		AxisInfo *axisXInfo = NULL, *axisYInfo = NULL;
		for (int x = 0; x < NumAxes; x++) {
			if (Axes[x].GameAxis == axisY) {
				axisYInfo = &Axes[x];
			}
			else if (Axes[x].GameAxis == axisX) {
				axisXInfo = &Axes[x];
			}
		}

		if (axisXInfo && axisYInfo) {
			
			// Assume for now we share a physical stick, so let's un-circularize the inputs
			float x = axisXInfo->Value * 1.15f;	// Add a slightly arbitrary bonus value to make up for many sticks that don't fully commit a proper circle
			float y = axisYInfo->Value * 1.15f;
			float r = sqrt(x*x + y * y);
			float maxMove = max(abs(x), abs(y));
			if (maxMove > 0) {
				axisYInfo->Value = clamp(y * (r / maxMove), -1.0f, 1.0f);
				axisXInfo->Value = clamp(x * (r / maxMove), -1.0f, 1.0f);
			}
		}
	}


	void ProcessInput()
	{
		uint8_t buttonstate;

		bool con = SDL_JoystickGetAttached(Device);
		if(!con && Connected) {
			// We have disconnected
			// TODO: Process this event and disable the gamepad
			Connected = false;
		} else if(con && !Connected) {
			Connected = true;
		}

		if(!Connected) {
			return;
		}
		
		UpdateFeedback();

		for (int i = 0; i < NumAxes; ++i)
		{
			buttonstate = 0;

			double axisval = SDL_JoystickGetAxis(Device, i)/32767.0;

			Axes[i].RawValue = axisval;

			axisval = Joy_RemoveDeadZone(axisval, Axes[i].DeadZone, &buttonstate);
			ProcessAcceleration(&Axes[i], axisval);			

			// Map button to axis
			// X and Y are handled differently so if we have 2 or more axes then we'll use that code instead.
			if (NumAxes == 1 || (i >= 2 && i < NUM_JOYAXISBUTTONS))
			{
				Joy_GenerateButtonEvents(Axes[i].ButtonValue, buttonstate, 2, KEY_JOYAXIS1PLUS + i*2);
				Axes[i].ButtonValue = buttonstate;
			}
		}

		if(NumAxes > 1)
		{
			buttonstate = Joy_XYAxesToButtons(Axes[0].Value, Axes[1].Value);
			Joy_GenerateButtonEvents(Axes[0].ButtonValue, buttonstate, 4, KEY_JOYAXIS1PLUS);
			Axes[0].ButtonValue = buttonstate;
		}

		// Map POV hats to buttons and axes.  Why axes?  Well apparently I have
		// a gamepad where the left control stick is a POV hat (instead of the
		// d-pad like you would expect, no that's pressure sensitive).  Also
		// KDE's joystick dialog maps them to axes as well.
		for (int i = 0; i < NumHats; ++i)
		{
			AxisInfo &x = Axes[NumAxes + i*2];
			AxisInfo &y = Axes[NumAxes + i*2 + 1];

			buttonstate = SDL_JoystickGetHat(Device, i);

			// If we're going to assume that we can pass SDL's value into
			// Joy_GenerateButtonEvents then we might as well assume the format here.
			if(buttonstate & 0x1) // Up
				y.Value = -1.0;
			else if(buttonstate & 0x4) // Down
				y.Value = 1.0;
			else
				y.Value = 0.0;
			if(buttonstate & 0x2) // Left
				x.Value = 1.0;
			else if(buttonstate & 0x8) // Right
				x.Value = -1.0;
			else
				x.Value = 0.0;

			if(i < 4)
			{
				Joy_GenerateButtonEvents(x.ButtonValue, buttonstate, 4, KEY_JOYPOV1_UP + i*4);
				x.ButtonValue = buttonstate;
			}
		}

		// Square inputs if desired
		if(joy_sdl_squarelook > 0) {
			SquareStick(JOYAXIS_Yaw, JOYAXIS_Pitch);
		}

		if(joy_sdl_squaremove > 0) {
			SquareStick(JOYAXIS_Side, JOYAXIS_Forward);
		}
	}

protected:
	static const DefaultAxisConfig DefaultAxes[5];

	SDL_Joystick		*Device;
	int					NumHats;
	

	bool InternalSetVibration(float l, float r) override {
		if (Device) {
			if (!joy_feedback) {
				if (lFeed != 0.0f || rFeed != 0.0f) {
					rFeed = lFeed = 0.0f;

					SDL_JoystickRumble(Device, 0, 0, 100);
				}
				return false;
			}

			lFeed = clamp(l, 0.0f, 1.0f);
			rFeed = clamp(r, 0.0f, 1.0f);

			return SDL_JoystickRumble(
				Device,
				(Uint16)round(clamp(lFeed * joy_feedback_scale, 0.0f, 1.0f) * 65535.0),
				(Uint16)round(clamp(rFeed * joy_feedback_scale, 0.0f, 1.0f) * 65535.0),
				50
			) == 0;
		}
		return false;
	}
};

const SDLInputJoystickBase::DefaultAxisConfig SDLInputJoystick::DefaultAxes[5] = {
	{ 0.234f, JOYAXIS_Side,		1,		0.25f },
	{ 0.234f, JOYAXIS_Forward,	1,		0.25f },
	{ 0.117f, JOYAXIS_None,		1,		0.0f  },
	{ 0.265f, JOYAXIS_Yaw,		1,		0.25f },
	{ 0.265f, JOYAXIS_Pitch,	0.4,	0.25f }
};



// Gamepad version - Geared more towards standard/modern gamepad layouts akin to XInput ==
// =======================================================================================

static const char *AxisNames[] =
{
	"Left Thumb X Axis",
	"Left Thumb Y Axis",
	"Right Thumb X Axis",
	"Right Thumb Y Axis",
	"Left Trigger",
	"Right Trigger"
};

class SDLInputGamepad: public SDLInputJoystickBase
{
public:
	SDLInputGamepad(int DeviceIndex)
	{
		this->DeviceIndex = DeviceIndex;
		this->Multiplier = 1.0f;

		Device = SDL_GameControllerOpen(DeviceIndex);
		if(Device != NULL)
		{
			NumAxes = 6;
			DeviceID = SDL_JoystickGetDeviceInstanceID(DeviceIndex);
			Name = SDL_GameControllerName(Device);
			M_LoadJoystickConfig(this);
		} else {
			Name = "Invalid Device";
		}
	}
	~SDLInputGamepad()
	{
		if(Device != NULL)
			M_SaveJoystickConfig(this);
		SDL_GameControllerClose(Device);
	}

	bool IsValid() const
	{
		return Device != NULL;
	}

	FString GetName()
	{
		FString nam;
		nam.Format("Gamepad: %s", Name.GetChars());
		return nam;
	}

	FString GetIdentifier() override {
		return "GM:" + DeviceIndex;
	}
	
	int GetNumAxes() override
	{
		return NumAxes;
	}

	// Used by the saver to not save properties that are at their defaults.
	bool IsSensitivityDefault() {
		return Multiplier == 1.0f;
	}

	bool IsAxisDeadZoneDefault(int axis) {
		return Axes[axis].DeadZone == DefaultAxes[axis].DeadZone;
	}

	bool IsAxisMapDefault(int axis) {
		return Axes[axis].GameAxis == DefaultAxes[axis].GameAxis;
	}

	bool IsAxisScaleDefault(int axis) {
		return Axes[axis].Multiplier == DefaultAxes[axis].Multiplier;
	}

	bool IsAxisAccelerationDefault(int axis) override {
		return Axes[axis].Acceleration == DefaultAxes[axis].Acceleration;
	}

	bool IsGamepad() override { return true; }


	void SetDefaultConfig()
	{
		// Remove existing axis information
		Axes.Clear();

		// Create the axes
		for(int i = 0; i < GetNumAxes(); i++) {
			AxisInfo info;
			info.Name = AxisNames[i];

			info.Value = 0.0;
			info.ButtonValue = 0;
			info.GameAxis = DefaultAxes[i].GameAxis;
			info.Acceleration = DefaultAxes[i].Acceleration;
			info.DeadZone = DefaultAxes[i].DeadZone;
			info.Multiplier = DefaultAxes[i].Multiplier;

			Axes.Push(info);
		}
	}

	void AddAxes(float axes[NUM_JOYAXIS])
	{
		// Add to game axes.
		for (int i = 0; i < GetNumAxes(); ++i)
		{
			if (Axes[i].GameAxis != JOYAXIS_None) {
				if (Axes[i].GameAxis != JOYAXIS_None) {
					if (Axes[i].GameAxis == JOYAXIS_Forward || Axes[i].GameAxis == JOYAXIS_Side) {
						axes[Axes[i].GameAxis] -= float(Axes[i].Value * Axes[i].Multiplier);
					}
					else {
						axes[Axes[i].GameAxis] -= float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
					}
				}
			}
		}
	}

	void Detached() override {
		int i;

		Connected = false;
		
		for (i = 0; i < 4; i += 2)
		{
			ProcessThumbstick(0, &Axes[i], 0, &Axes[i+1], KEY_PAD_LTHUMB_RIGHT + i*2);
		}

		for (i = 0; i < 2; ++i)
		{
			ProcessTrigger(0, &Axes[4+i], KEY_PAD_LTRIGGER + i);
		}

		SDL_GameControllerClose(Device);
		Device = NULL;
		
	}

	void Attached() override {
		SDLInputJoystickBase::Attached();

		// Re-establish the device object
		if(Device) {
			SDL_GameControllerClose(Device);
		}

		Device = SDL_GameControllerOpen(DeviceIndex);
		DeviceID = SDL_JoystickGetDeviceInstanceID(DeviceIndex);
		Name = SDL_GameControllerName(Device);
		M_LoadJoystickConfig(this);
	}

	inline void SquareStick(EJoyAxis axisX, EJoyAxis axisY, int cv) {
		AxisInfo *axisYInfo = NULL, *axisXInfo = NULL;
		int axisSide = -1, axisForward = -1;
		for (int x = 0; x < NumAxes; x++) {
			if (Axes[x].GameAxis == axisY) {
				axisXInfo = &Axes[x];
				axisForward = x;
			}
			else if (Axes[x].GameAxis == axisX) {
				axisYInfo = &Axes[x];
				axisSide = x;
			}
		}

		// Make sure they are on the same physical stick, or let it be forced with joy_xinput_squaremove
		if (axisYInfo && axisXInfo && (
			cv > 1 ||
			((axisSide == SDL_CONTROLLER_AXIS_LEFTX || axisSide == SDL_CONTROLLER_AXIS_LEFTY) && (axisForward == SDL_CONTROLLER_AXIS_LEFTX || axisForward == SDL_CONTROLLER_AXIS_LEFTY)) ||
			((axisSide == SDL_CONTROLLER_AXIS_RIGHTX || axisSide == SDL_CONTROLLER_AXIS_RIGHTY) && (axisForward == SDL_CONTROLLER_AXIS_RIGHTX || axisForward == SDL_CONTROLLER_AXIS_RIGHTY))
			)) {

			// We share a physical stick, so let's un-circularize the inputs
			float x = axisYInfo->Value * 1.15f;	// Add a slightly arbitrary bonus value to make up for many sticks that don't fully commit a proper circle
			float y = axisXInfo->Value * 1.15f;
			float r = sqrt(x*x + y * y);
			float maxMove = max(abs(x), abs(y));
			if (maxMove > 0) {
				axisXInfo->Value = clamp(y * (r / maxMove), -1.0f, 1.0f);
				axisYInfo->Value = clamp(x * (r / maxMove), -1.0f, 1.0f);
			}
		}
	}

	void ProcessInput()
	{
		uint8_t buttonstate;

		/*bool con = SDL_GameControllerGetAttached(Device);
		if((!con && Connected) || !Device) {
			// We have disconnected
			Detached();
		} else if(con && !Connected) {
			Attached();
		}*/

		if(!Connected) {
			return;
		}

		UpdateFeedback();

		ProcessThumbstick(SDL_GameControllerGetAxis(Device, SDL_CONTROLLER_AXIS_LEFTX)/32767.0, &Axes[SDL_CONTROLLER_AXIS_LEFTX],
					 	  SDL_GameControllerGetAxis(Device, SDL_CONTROLLER_AXIS_LEFTY)/32767.0, &Axes[SDL_CONTROLLER_AXIS_LEFTY], KEY_PAD_LTHUMB_RIGHT);
		ProcessThumbstick(SDL_GameControllerGetAxis(Device, SDL_CONTROLLER_AXIS_RIGHTX)/32767.0, &Axes[SDL_CONTROLLER_AXIS_RIGHTX],
						  SDL_GameControllerGetAxis(Device, SDL_CONTROLLER_AXIS_RIGHTY)/32767.0, &Axes[SDL_CONTROLLER_AXIS_RIGHTY], KEY_PAD_RTHUMB_RIGHT);
		
		ProcessTrigger(SDL_GameControllerGetAxis(Device, SDL_CONTROLLER_AXIS_TRIGGERLEFT)/32767.0, &Axes[SDL_CONTROLLER_AXIS_TRIGGERLEFT], KEY_PAD_LTRIGGER);
		ProcessTrigger(SDL_GameControllerGetAxis(Device, SDL_CONTROLLER_AXIS_TRIGGERRIGHT)/32767.0, &Axes[SDL_CONTROLLER_AXIS_TRIGGERRIGHT], KEY_PAD_RTRIGGER);

		if(joy_sdl_squarelook > 0) {
			SquareStick(JOYAXIS_Yaw, JOYAXIS_Pitch, joy_sdl_squarelook);
		}

		if(joy_sdl_squaremove > 0) {
			SquareStick(JOYAXIS_Side, JOYAXIS_Forward, joy_sdl_squaremove);
		}
	}


	static void ProcessThumbstick(double value1, AxisInfo *axis1, double value2, AxisInfo *axis2, int base) {
		uint8_t buttonstate;
		double axisval1, axisval2;

		axis1->RawValue = value1;
		axis2->RawValue = value2;

		axisval1 = Joy_RemoveDeadZone(value1, axis1->DeadZone, NULL);
		axisval2 = Joy_RemoveDeadZone(value2, axis2->DeadZone, NULL);

		// Calculate acceleration
		ProcessAcceleration(axis1, axisval1);
		ProcessAcceleration(axis2, axisval2);

		// We store all four buttons in the first axis and ignore the second.
		buttonstate = Joy_XYAxesToButtons(axis1->Value, axis2->Value);
		Joy_GenerateButtonEvents(axis1->ButtonValue, buttonstate, 4, base);
		axis1->ButtonValue = buttonstate;
	}


	static void ProcessTrigger(double value, AxisInfo *axis, int base) {
		uint8_t buttonstate;
		double axisval = value;
		
		axis->RawValue = value;

		axisval = (float)Joy_RemoveDeadZone(axisval, axis->DeadZone, &buttonstate);

		ProcessAcceleration(axis, axisval);

		Joy_GenerateButtonEvents(axis->ButtonValue, buttonstate, 1, base);
		axis->ButtonValue = buttonstate;
	}


protected:
	static const DefaultAxisConfig DefaultAxes[6];

	SDL_GameController		*Device;
	FString 				Name;

	bool InternalSetVibration(float l, float r) override {
		if (Device) {
			if (!joy_feedback) {
				if (lFeed != 0.0f || rFeed != 0.0f) {
					rFeed = lFeed = 0.0f;

					SDL_GameControllerRumble(Device, 0, 0, 100);
				}
				return false;
			}

			lFeed = clamp(l, 0.0f, 1.0f);
			rFeed = clamp(r, 0.0f, 1.0f);

			return SDL_GameControllerRumble(
				Device,
				(Uint16)round(clamp(lFeed * joy_feedback_scale, 0.0f, 1.0f) * 65535.0),
				(Uint16)round(clamp(rFeed * joy_feedback_scale, 0.0f, 1.0f) * 65535.0),
				50
			) == 0;
		}
		return false;
	}
};

const SDLInputJoystickBase::DefaultAxisConfig SDLInputGamepad::DefaultAxes[6] = {
	// Dead zone, game axis, multiplier, acceleration
	{ 0.234f,		JOYAXIS_Side,		1,		0.25f },	// ThumbLX
	{ 0.234f,		JOYAXIS_Forward,	1,		0.25f },	// ThumbLY
	{ 0.265f,		JOYAXIS_Yaw,		1,		0.5f },		// ThumbRX
	{ 0.265f,		JOYAXIS_Pitch,		0.4f,	0.5f },		// ThumbRY
	{ 0.117,		JOYAXIS_None,		0,		0 },		// LeftTrigger
	{ 0.117,		JOYAXIS_None,		0,		0 }			// RightTrigger
};



SDLInputJoystickManager::SDLInputJoystickManager() {
	SDL_GameControllerEventState(SDL_ENABLE);	// Enable events from the game controller interface

	for(int i = 0;i < SDL_NumJoysticks();i++) {
		// Check each joystick to see if it's a recognized gamepad, or use a generic joystick interface instead
		if(SDL_IsGameController(i)) {
			SDLInputGamepad *device = new SDLInputGamepad(i);
			if(device->IsValid())
				Joysticks.Push(device);
			else
				delete device;
		} else {
			SDLInputJoystick *device = new SDLInputJoystick(i);
			if(device->IsValid())
				Joysticks.Push(device);
			else
				delete device;
		}
	}
}

SDLInputJoystickManager::~SDLInputJoystickManager() {
	for(uint x = 0; x < Joysticks.Size(); x++) {
		M_SaveJoystickConfig(Joysticks[x]);
	}

	Joysticks.Clear();
}

void SDLInputJoystickManager::AddAxes(float axes[NUM_JOYAXIS]) {
	for(unsigned int i = 0;i < Joysticks.Size();i++)
		Joysticks[i]->AddAxes(axes);
}

void SDLInputJoystickManager::GetDevices(TArray<IJoystickConfig *> &sticks)
{
	for(unsigned int i = 0;i < Joysticks.Size();i++)
	{
		//M_LoadJoystickConfig(Joysticks[i]);
		sticks.Push(Joysticks[i]);
	}
}

IJoystickConfig *SDLInputJoystickManager::GetDevice(int joyID) {
	for(unsigned int i = 0; i < Joysticks.Size(); i++)
	{
		if(Joysticks[i]->GetDeviceIndex() == joyID) {
			return Joysticks[i];
		}
	}

	return NULL;
}

IJoystickConfig *SDLInputJoystickManager::GetDeviceFromID(int joyDeviceID) {
	for(unsigned int i = 0; i < Joysticks.Size(); i++)
	{
		if(Joysticks[i]->GetDeviceID() == joyDeviceID) {
			return Joysticks[i];
		}
	}

	return NULL;
}

SDLInputJoystickBase *SDLInputJoystickManager::InternalGetDevice(int joyIndex) {
	for(unsigned int i = 0; i < Joysticks.Size(); i++) {
		if(Joysticks[i]->GetDeviceIndex() == joyIndex) {
			return (SDLInputJoystickBase*)Joysticks[i];
		}
	}

	return NULL;
}

void SDLInputJoystickManager::ProcessInput() const
{
	for(unsigned int i = 0;i < Joysticks.Size();++i)
		Joysticks[i]->ProcessInput();
}

void SDLInputJoystickManager::DeviceRemoved(int joyInstanceID) {
	// We need to delete the device because the next device at the same slot *COULD* 
	// end up being a gamepad where it was previously a joystick
	bool hasDeleted = false;
	for(int x = Joysticks.Size() - 1; x >= 0; x--) {
		auto joy = Joysticks[x];

		if(joy->GetDeviceID() == joyInstanceID) {
			M_SaveJoystickConfig(joy);
			joy->Detached();
			delete Joysticks[x];
			Joysticks.Delete(x);
			hasDeleted = true;
		}
	}


	if(hasDeleted) PostDeviceChangeEvent();//UpdateJoystickMenu(NULL);
}

void SDLInputJoystickManager::DeviceAdded(int joyIndex) {
	// Check existing joysticks with the same ID to see if they match
	// the same type. If not, delete it!
	bool newJoyIsGamepad = SDL_IsGameController(joyIndex);
	bool hasDeleted = false;

	for(int x = Joysticks.Size() - 1; x >= 0; x--) {
		auto joy = Joysticks[x];

		if(joy->GetDeviceIndex() == joyIndex) {
			if(newJoyIsGamepad != joy->IsGamepad()) {
				if(joy->IsConnected()) joy->Detached();
				delete Joysticks[x];
				Joysticks.Delete(x);
				Printf("Deleting mismatched joystick entry: %d\n", joyIndex);
				hasDeleted = true;
			}
		}
	}

	auto device = InternalGetDevice(joyIndex);
	if(device) {
		if(!device->IsConnected()) device->Attached();
		return;
	} else {
		if(SDL_IsGameController(joyIndex)) {
			SDLInputGamepad *device = new SDLInputGamepad(joyIndex);
			if(device->IsValid()) {
				Joysticks.Push(device);
				PostDeviceChangeEvent();
				return;
			} else {
				delete device;
			}
		} else {
			SDLInputJoystick *device = new SDLInputJoystick(joyIndex);
			if(device->IsValid()) {
				Joysticks.Push(device);
				PostDeviceChangeEvent();
				return;
			} else {
				delete device;
			}
		}
	}

	if(hasDeleted) {
		PostDeviceChangeEvent();
	}
}


SDLInputJoystickManager *JoystickManager;


void I_StartupJoysticks()
{
#ifndef NO_SDL_JOYSTICK
	if(SDL_InitSubSystem(SDL_INIT_JOYSTICK) >= 0) {
		SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
		JoystickManager = new SDLInputJoystickManager();
	}
#endif
}
void I_ShutdownInput()
{
	if(JoystickManager)
	{
		delete JoystickManager;
		SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
		SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
	}
}

void I_GetJoysticks(TArray<IJoystickConfig *> &sticks)
{
	sticks.Clear();

	if (JoystickManager)
		JoystickManager->GetDevices(sticks);
}


void I_GetAxes(float axes[NUM_JOYAXIS])
{
	for (int i = 0; i < NUM_JOYAXIS; ++i)
	{
		axes[i] = 0;
	}
	if (use_joystick && JoystickManager)
	{
		JoystickManager->AddAxes(axes);
	}
}

void I_ProcessJoysticks()
{
	if (use_joystick && JoystickManager)
		JoystickManager->ProcessInput();
}

IJoystickConfig *I_UpdateDeviceList()
{
	return NULL;
}
