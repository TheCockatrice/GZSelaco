/*
**
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

// HEADER FILES ------------------------------------------------------------

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>
#include <limits.h>

#include "i_input.h"
#include "d_eventbase.h"

#include "gameconfigfile.h"
#include "m_argv.h"
#include "cmdlib.h"
#include "keydef.h"

#include "i_time.h"
#include "TSQueue.h"

// MACROS ------------------------------------------------------------------

// This macro is defined by newer versions of xinput.h. In case we are
// compiling with an older version, define it here.
#ifndef XUSER_MAX_COUNT
#define XUSER_MAX_COUNT                 4
#endif

// MinGW
#ifndef XINPUT_DLL
#define XINPUT_DLL_A  "xinput1_3.dll"
#define XINPUT_DLL_W L"xinput1_3.dll"
#ifdef UNICODE
    #define XINPUT_DLL XINPUT_DLL_W
#else
    #define XINPUT_DLL XINPUT_DLL_A
#endif
#endif

#define XINPUT_DLL_A1 "xinput1_4.dll"
#define XINPUT_DLL_W1 L"xinput1_4.dll"

#ifdef UNICODE
#define XINPUT_DLL1 XINPUT_DLL_W1
#else
#define XINPUT_DLL1 XINPUT_DLL_A1
#endif 


EXTERN_CVAR(Bool, joy_feedback)
EXTERN_CVAR(Float, joy_feedback_scale)

CUSTOM_CVAR(Int, joy_xinput_queuesize, 5, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
	if (self > 10) self = 10;
	if (self < 1) self = 1;
}

CUSTOM_CVAR(Int, joy_xinput_squaremove, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
	if (self > 2) self = 2;
	if (self < 0) self = 0;
}

CUSTOM_CVAR(Int, joy_xinput_squarelook, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG) {
	if (self > 2) self = 2;
	if (self < 0) self = 0;
}

// TYPES -------------------------------------------------------------------

typedef DWORD (WINAPI *XInputGetStateType)(DWORD index, XINPUT_STATE *state);
typedef DWORD (WINAPI *XInputSetStateType)(DWORD index, XINPUT_STATE *state);
typedef DWORD (WINAPI *XInputSetVibeType)(DWORD index, XINPUT_VIBRATION *vibe);
typedef DWORD (WINAPI *XInputGetCapabilitiesType)(DWORD index, DWORD flags, XINPUT_CAPABILITIES *caps);
typedef void  (WINAPI *XInputEnableType)(BOOL enable);



class FXInputController : public IJoystickConfig
{
public:
	FXInputController(int index);
	~FXInputController();

	void ProcessInput();
	void AddAxes(float axes[NUM_JOYAXIS]);
	bool IsConnected() { return Connected; }

	// IJoystickConfig interface
	FString GetName();
	float GetSensitivity();
	virtual void SetSensitivity(float scale);

	int GetNumAxes();
	float GetAxisDeadZone(int axis);
	EJoyAxis GetAxisMap(int axis);
	const char *GetAxisName(int axis);
	float GetAxisScale(int axis);
	float GetAxisAcceleration(int axis) override;
	float GetAxis(int axis) override;
	float GetRawAxis(int axis) override;

	void SetAxisDeadZone(int axis, float deadzone);
	void SetAxisMap(int axis, EJoyAxis gameaxis);
	void SetAxisScale(int axis, float scale);
	void SetAxisAcceleration(int axis, float accel) override;

	bool IsSensitivityDefault();
	bool IsAxisDeadZoneDefault(int axis);
	bool IsAxisMapDefault(int axis);
	bool IsAxisScaleDefault(int axis);
	bool IsAxisAccelerationDefault(int axis) override;

	void SetDefaultConfig();
	FString GetIdentifier();

	//bool SetVibration(float l, float r) override;
	//bool AddVibration(float l, float r) override;

protected:
	struct AxisInfo
	{
		float Value;
		float RawValue;
		float DeadZone;
		float Multiplier;
		float Acceleration;
		EJoyAxis GameAxis;
		uint8_t ButtonValue;
		RingBuffer<float, 10> Inputs;
	};
	struct DefaultAxisConfig
	{
		float DeadZone;
		EJoyAxis GameAxis;
		float Multiplier;
		float Acceleration;
	};
	enum
	{
		AXIS_ThumbLX,
		AXIS_ThumbLY,
		AXIS_ThumbRX,
		AXIS_ThumbRY,
		AXIS_LeftTrigger,
		AXIS_RightTrigger,
		NUM_AXES
	};

	bool InternalSetVibration(float l, float r) override;

	int Index;
	float Multiplier;
	AxisInfo Axes[NUM_AXES];
	static DefaultAxisConfig DefaultAxes[NUM_AXES];
	DWORD LastPacketNumber;
	int LastButtons;
	bool Connected;

	void Attached();
	void Detached();

	static void ProcessThumbstick(int value1, AxisInfo *axis1, int value2, AxisInfo *axis2, int base);
	static void ProcessTrigger(int value, AxisInfo *axis, int base);
	static void ProcessAcceleration(AxisInfo *axis, float val);
};

class FXInputManager : public FJoystickCollection
{
public:
	FXInputManager();
	~FXInputManager();

	bool GetDevice();
	void ProcessInput();
	bool WndProcHook(HWND hWnd, uint32_t message, WPARAM wParam, LPARAM lParam, LRESULT *result);
	void AddAxes(float axes[NUM_JOYAXIS]);
	void GetDevices(TArray<IJoystickConfig *> &sticks);
	IJoystickConfig *Rescan();

protected:
	HMODULE XInputDLL;
	FXInputController *Devices[XUSER_MAX_COUNT];
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

CUSTOM_CVAR(Bool, joy_xinput, true, CVAR_GLOBALCONFIG|CVAR_ARCHIVE|CVAR_NOINITCALL)
{
	I_StartupXInput();
	event_t ev = { EV_DeviceChange };
	D_PostEvent(&ev);
}

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static XInputGetStateType			InputGetState;
static XInputSetStateType			InputSetState;
static XInputGetCapabilitiesType	InputGetCapabilities;
static XInputEnableType				InputEnable;
static XInputSetVibeType			InputSetVibe;

static const char *AxisNames[] =
{
	"Left Thumb X Axis",
	"Left Thumb Y Axis",
	"Right Thumb X Axis",
	"Right Thumb Y Axis",
	"Left Trigger",
	"Right Trigger"
};

FXInputController::DefaultAxisConfig FXInputController::DefaultAxes[NUM_AXES] =
{
	// Dead zone, game axis, multiplier, acceleration
	{ XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32768.f,		JOYAXIS_Side,		1,		0.25f },	// ThumbLX
	{ XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32768.f,		JOYAXIS_Forward,	1,		0.25f },	// ThumbLY
	{ XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32768.f,	JOYAXIS_Yaw,		1,		0.5f },		// ThumbRX
	{ XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32768.f,	JOYAXIS_Pitch,		0.4f,	0.5f },		// ThumbRY
	{ XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 256.f,			JOYAXIS_None,		0,		0 },		// LeftTrigger
	{ XINPUT_GAMEPAD_TRIGGER_THRESHOLD / 256.f,			JOYAXIS_None,		0,		0 }			// RightTrigger
};

// CODE --------------------------------------------------------------------

//==========================================================================
//
// FXInputController - Constructor
//
//==========================================================================

FXInputController::FXInputController(int index)
{
	Index = index;
	Connected = false;
	M_LoadJoystickConfig(this);
}

//==========================================================================
//
// FXInputController - Destructor
//
//==========================================================================

FXInputController::~FXInputController()
{
	// Send button up events before destroying this.
	ProcessThumbstick(0, &Axes[AXIS_ThumbLX], 0, &Axes[AXIS_ThumbLY], KEY_PAD_LTHUMB_RIGHT);
	ProcessThumbstick(0, &Axes[AXIS_ThumbRX], 0, &Axes[AXIS_ThumbRY], KEY_PAD_RTHUMB_RIGHT);
	ProcessTrigger(0, &Axes[AXIS_LeftTrigger], KEY_PAD_LTRIGGER);
	ProcessTrigger(0, &Axes[AXIS_RightTrigger], KEY_PAD_RTRIGGER);
	Joy_GenerateButtonEvents(LastButtons, 0, 16, KEY_PAD_DPAD_UP);
	M_SaveJoystickConfig(this);
}

//==========================================================================
//
// FXInputController :: ProcessInput
//
//==========================================================================

void FXInputController::ProcessInput()
{
	DWORD res;
	XINPUT_STATE state;

	res = InputGetState(Index, &state);
	if (res == ERROR_DEVICE_NOT_CONNECTED)
	{
		if (Connected)
		{
			Detached();
		}
		return;
	}
	if (res != ERROR_SUCCESS)
	{
		return;
	}
	if (!Connected)
	{
		Attached();
	}

	UpdateFeedback();

	// We run these every frame now, even if they have not changed
	// This is necessary to run joytick acceleration properly

	// Convert axes to floating point and cancel out deadzones.
	// XInput's Y axes are reversed compared to DirectInput.
	ProcessThumbstick(state.Gamepad.sThumbLX, &Axes[AXIS_ThumbLX],
					 -state.Gamepad.sThumbLY, &Axes[AXIS_ThumbLY], KEY_PAD_LTHUMB_RIGHT);
	ProcessThumbstick(state.Gamepad.sThumbRX, &Axes[AXIS_ThumbRX],
					 -state.Gamepad.sThumbRY, &Axes[AXIS_ThumbRY], KEY_PAD_RTHUMB_RIGHT);
	ProcessTrigger(state.Gamepad.bLeftTrigger, &Axes[AXIS_LeftTrigger], KEY_PAD_LTRIGGER);
	ProcessTrigger(state.Gamepad.bRightTrigger, &Axes[AXIS_RightTrigger], KEY_PAD_RTRIGGER);


	// Find the two axes for directional movement
	if (joy_xinput_squaremove > 0) {
		AxisInfo *moveSide = NULL, *moveForward = NULL;
		int axisSide = -1, axisForward = -1;
		for (int x = 0; x < NUM_AXES; x++) {
			if (Axes[x].GameAxis == JOYAXIS_Forward) {
				moveForward = &Axes[x];
				axisForward = x;
			}
			else if (Axes[x].GameAxis == JOYAXIS_Side) {
				moveSide = &Axes[x];
				axisSide = x;
			}
		}

		// Make sure they are on the same physical stick, or let it be forced with joy_xinput_squaremove
		if (moveSide && moveForward && (
			joy_xinput_squaremove > 1 || 
			((axisSide == AXIS_ThumbLX || axisSide == AXIS_ThumbLY) && (axisForward == AXIS_ThumbLX || axisForward == AXIS_ThumbLY)) ||
			((axisSide == AXIS_ThumbRX || axisSide == AXIS_ThumbRY) && (axisForward == AXIS_ThumbRX || axisForward == AXIS_ThumbRY))
			)) {

			// We share a physical stick, so let's un-circularize the inputs
			float x = moveSide->Value * 1.15f;	// Add a slightly arbitrary bonus value to make up for many sticks that don't fully commit a proper circle
			float y = moveForward->Value * 1.15f;
			float r = sqrt(x*x + y*y);
			float maxMove = max(abs(x), abs(y));
			if (maxMove > 0) {
				moveForward->Value = clamp(y * (r / maxMove), -1.0f, 1.0f);
				moveSide->Value = clamp(x * (r / maxMove), -1.0f, 1.0f);
			}
		}
	}

	// Find the two axis for looking
	if (joy_xinput_squarelook > 0) {
		AxisInfo *lookYaw = NULL, *lookPitch = NULL;
		int axisPitch = -1, axisYaw = -1;
		for (int x = 0; x < NUM_AXES; x++) {
			if (Axes[x].GameAxis == JOYAXIS_Yaw) {
				lookYaw = &Axes[x];
				axisYaw = x;
			}
			else if (Axes[x].GameAxis == JOYAXIS_Pitch) {
				lookPitch = &Axes[x];
				axisPitch = x;
			}
		}

		// Make sure they are on the same physical stick, or let it be forced with joy_xinput_squarelook
		if (lookPitch && lookYaw && (
			joy_xinput_squarelook > 1 ||
			((axisPitch == AXIS_ThumbLX || axisPitch == AXIS_ThumbLY) && (axisYaw == AXIS_ThumbLX || axisYaw == AXIS_ThumbLY)) ||
			((axisPitch == AXIS_ThumbRX || axisPitch == AXIS_ThumbRY) && (axisYaw == AXIS_ThumbRX || axisYaw == AXIS_ThumbRY))
			)) {

			// We share a physical stick, so let's un-circularize the inputs
			float x = lookYaw->Value * 1.15f;	// Add a slightly arbitrary bonus value to make up for many sticks that don't fully commit a proper circle
			float y = lookPitch->Value * 1.15f;
			float r = sqrt(x*x + y * y);
			float maxMove = max(abs(x), abs(y));
			if (maxMove > 0) {
				lookPitch->Value = clamp(y * (r / maxMove), -1.0f, 1.0f);
				lookYaw->Value = clamp(x * (r / maxMove), -1.0f, 1.0f);
			}
		}
	}
	

	if (state.dwPacketNumber == LastPacketNumber)
	{ // Nothing has changed since last time.
		return;
	}

	// There is a hole in the wButtons bitmask where two buttons could fit.
	// As per the XInput documentation, "bits that are set but not defined ... are reserved,
	// and their state is undefined," so we clear them to make sure they're not set.
	// Our keymapping uses these two slots for the triggers as buttons.
	state.Gamepad.wButtons &= 0xF3FF;

	// Generate events for buttons that have changed.
	Joy_GenerateButtonEvents(LastButtons, state.Gamepad.wButtons, 16, KEY_PAD_DPAD_UP);

	LastPacketNumber = state.dwPacketNumber;
	LastButtons = state.Gamepad.wButtons;
}


//==========================================================================
//
// FXInputController :: InternalSetVibration
//
// Set left and right vibration. Converts floating point to internal values
// These settings may end up being zeroed out after a period of inactivty
// as we don't want the motors to run forever. This decay is handled in 
// the generic interface
// 
//==========================================================================

bool FXInputController::InternalSetVibration(float l, float r) {
	if (!joy_feedback) {
		if (lFeed != 0.0f || rFeed != 0.0f) {
			rFeed = rFeed = 0.0f;

			XINPUT_VIBRATION vibration;
			ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
			vibration.wLeftMotorSpeed = (WORD)0;
			vibration.wRightMotorSpeed = (WORD)0;
			InputSetVibe(Index, &vibration);
		}
		return false;
	}

	lFeed = clamp(l, 0.0f, 1.0f);
	rFeed = clamp(r, 0.0f, 1.0f);

	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = (WORD)round(clamp(lFeed * joy_feedback_scale, 0.0f, 1.0f) * 65535.0);
	vibration.wRightMotorSpeed = (WORD)round(clamp(rFeed * joy_feedback_scale, 0.0f, 1.0f) * 65535.0);

	return InputSetVibe(Index, &vibration) == ERROR_SUCCESS;
}


//==========================================================================
//
// FXInputController :: ProcessThumbstick							STATIC
//
// Converts both axes of a thumb stick to floating point, cancels out the
// deadzone, and generates button up/down events for them.
//
//==========================================================================

void FXInputController::ProcessAcceleration(AxisInfo *axis, float val) {
	// Curve value - Circular
	//float cv = (1 - sqrt(1.0f - pow(abs(val), 2.0f))) * (val > 0 ? 1.0f : -1.0f);

	// Curve value, Quint
	float cv = (val * val * val * val * val);

	// Return a lerp of the actual value and the curve value
	axis->Value = clamp(cv + ((1.0f - axis->Acceleration) * (val - cv)), -1.0f, 1.0f);
}

void FXInputController::ProcessThumbstick(int value1, AxisInfo *axis1,
	int value2, AxisInfo *axis2, int base)
{
	uint8_t buttonstate;
	double axisval1, axisval2;

	axisval1 = (value1 - SHRT_MIN) * 2.0 / 65536 - 1.0;
	axisval2 = (value2 - SHRT_MIN) * 2.0 / 65536 - 1.0;

	axis1->RawValue = (float)axisval1;
	axis2->RawValue = (float)axisval2;

	axisval1 = Joy_RemoveDeadZone(axisval1, axis1->DeadZone, NULL);
	axisval2 = Joy_RemoveDeadZone(axisval2, axis2->DeadZone, NULL);

	ProcessAcceleration(axis1, (float)axisval1);
	ProcessAcceleration(axis2, (float)axisval2);

	// We store all four buttons in the first axis and ignore the second.
	buttonstate = Joy_XYAxesToButtons(axis1->Value, axis2->Value);
	Joy_GenerateButtonEvents(axis1->ButtonValue, buttonstate, 4, base);
	axis1->ButtonValue = buttonstate;
}

//==========================================================================
//
// FXInputController :: ProcessTrigger								STATIC
//
// Much like ProcessThumbstick, except triggers only go in the positive
// direction and have less precision.
//
//==========================================================================

void FXInputController::ProcessTrigger(int value, AxisInfo *axis, int base)
{
	uint8_t buttonstate;
	float axisval = value / 256.0f;
	
	axis->RawValue = axisval;

	// Seems silly to bother with axis scaling here, but I'm going to @Cockatrice
	axisval = (float)Joy_RemoveDeadZone((double)axisval, axis->DeadZone, &buttonstate);
	ProcessAcceleration(axis, axisval);
	

	Joy_GenerateButtonEvents(axis->ButtonValue, buttonstate, 1, base);
	axis->ButtonValue = buttonstate;
	//axis->Value = float(axisval);
}

//==========================================================================
//
// FXInputController :: Attached
//
// This controller was just attached. Set all buttons and axes to 0.
//
//==========================================================================

void FXInputController::Attached()
{
	int i;

	Connected = true;
	LastPacketNumber = ~0;
	LastButtons = 0;
	for (i = 0; i < NUM_AXES; ++i)
	{
		Axes[i].Value = 0;
		Axes[i].RawValue = 0;
		Axes[i].ButtonValue = 0;
		Axes[i].Inputs.pos = -1;
	}
	
	lFeed = rFeed = 0;
	feedLastUpdate = I_msTimeF();
	UpdateJoystickMenu(this);
}

//==========================================================================
//
// FXInputController :: Detached
//
// This controller was just detached. Send button ups for buttons that
// were pressed the last time we got input from it.
//
//==========================================================================

void FXInputController::Detached()
{
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
	Joy_GenerateButtonEvents(LastButtons, 0, 16, KEY_PAD_DPAD_UP);
	LastButtons = 0;
	UpdateJoystickMenu(NULL);
}

//==========================================================================
//
// FXInputController :: AddAxes
//
// Add the values of each axis to the game axes.
//
//==========================================================================

void FXInputController::AddAxes(float axes[NUM_JOYAXIS])
{
	// Add to game axes.
	for (int i = 0; i < NUM_AXES; ++i)
	{
		// Do not scale the movement axes by the global multiplier
		if (Axes[i].GameAxis == JOYAXIS_Forward || Axes[i].GameAxis == JOYAXIS_Side) {
			axes[Axes[i].GameAxis] -= float(Axes[i].Value * Axes[i].Multiplier);
		}
		else {
			axes[Axes[i].GameAxis] -= float(Axes[i].Value * Multiplier * Axes[i].Multiplier);
		}
	}
}

//==========================================================================
//
// FXInputController :: SetDefaultConfig
//
//==========================================================================

void FXInputController::SetDefaultConfig()
{
	Multiplier = 1;
	for (int i = 0; i < NUM_AXES; ++i)
	{
		Axes[i].DeadZone = DefaultAxes[i].DeadZone;
		Axes[i].GameAxis = DefaultAxes[i].GameAxis;
		Axes[i].Multiplier = DefaultAxes[i].Multiplier;
		Axes[i].Acceleration = DefaultAxes[i].Acceleration;
		Axes[i].Inputs.pos = -1;
	}
}

//==========================================================================
//
// FXInputController :: GetIdentifier
//
//==========================================================================

FString FXInputController::GetIdentifier()
{
	return FStringf("XI:%d", Index);
}

//==========================================================================
//
// FXInputController :: GetName
//
//==========================================================================

FString FXInputController::GetName()
{
	FString res;
	res.Format("Controller #%d (XInput)", Index + 1);
	return res;
}

//==========================================================================
//
// FXInputController :: GetSensitivity
//
//==========================================================================

float FXInputController::GetSensitivity()
{
	return Multiplier;
}

//==========================================================================
//
// FXInputController :: SetSensitivity
//
//==========================================================================

void FXInputController::SetSensitivity(float scale)
{
	Multiplier = scale;
}

//==========================================================================
//
// FXInputController :: IsSensitivityDefault
//
//==========================================================================

bool FXInputController::IsSensitivityDefault()
{
	return Multiplier == 1;
}

//==========================================================================
//
// FXInputController :: GetNumAxes
//
//==========================================================================

int FXInputController::GetNumAxes()
{
	return NUM_AXES;
}

//==========================================================================
//
// FXInputController :: GetAxisDeadZone
//
//==========================================================================

float FXInputController::GetAxisDeadZone(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].DeadZone;
	}
	return 0;
}

//==========================================================================
//
// FXInputController :: GetAxisMap
//
//==========================================================================

EJoyAxis FXInputController::GetAxisMap(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].GameAxis;
	}
	return JOYAXIS_None;
}

//==========================================================================
//
// FXInputController :: GetAxisName
//
//==========================================================================

const char *FXInputController::GetAxisName(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return AxisNames[axis];
	}
	return "Invalid";
}

//==========================================================================
//
// FXInputController :: GetAxisScale
//
//==========================================================================

float FXInputController::GetAxisScale(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Multiplier;
	}
	return 0;
}

//==========================================================================
//
// FXInputController :: GetAxisAccleration
//
//==========================================================================

float FXInputController::GetAxisAcceleration(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Acceleration;
	}
	return 0;
}


float FXInputController::GetAxis(int axis) {
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Value;
	}
	return 0;
}


float FXInputController::GetRawAxis(int axis) {
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].RawValue;
	}
	return 0;
}

//==========================================================================
//
// FXInputController :: SetAxisDeadZone
//
//==========================================================================

void FXInputController::SetAxisDeadZone(int axis, float deadzone)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].DeadZone = clamp(deadzone, 0.f, 1.f);
	}
}

//==========================================================================
//
// FXInputController :: SetAxisMap
//
//==========================================================================

void FXInputController::SetAxisMap(int axis, EJoyAxis gameaxis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].GameAxis = (unsigned(gameaxis) < NUM_JOYAXIS) ? gameaxis : JOYAXIS_None;
	}
}

//==========================================================================
//
// FXInputController :: SetAxisScale
//
//==========================================================================

void FXInputController::SetAxisScale(int axis, float scale)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].Multiplier = scale;
	}
}

//==========================================================================
//
// FXInputController :: SetAxisAcceleration
//
//==========================================================================

void FXInputController::SetAxisAcceleration(int axis, float acceleration)
{
	if (unsigned(axis) < NUM_AXES)
	{
		Axes[axis].Acceleration = acceleration;
	}
}

//===========================================================================
//
// FXInputController :: IsAxisDeadZoneDefault
//
//===========================================================================

bool FXInputController::IsAxisDeadZoneDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].DeadZone == DefaultAxes[axis].DeadZone;
	}
	return true;
}

//===========================================================================
//
// FXInputController :: IsAxisScaleDefault
//
//===========================================================================

bool FXInputController::IsAxisScaleDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Multiplier == DefaultAxes[axis].Multiplier;
	}
	return true;
}

//===========================================================================
//
// FXInputController :: IsAxisScaleDefault
//
//===========================================================================

bool FXInputController::IsAxisAccelerationDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].Acceleration == DefaultAxes[axis].Acceleration;
	}
	return true;
}

//===========================================================================
//
// FXInputController :: IsAxisMapDefault
//
//===========================================================================

bool FXInputController::IsAxisMapDefault(int axis)
{
	if (unsigned(axis) < NUM_AXES)
	{
		return Axes[axis].GameAxis == DefaultAxes[axis].GameAxis;
	}
	return true;
}

//==========================================================================
//
// FXInputManager - Constructor
//
//==========================================================================

FXInputManager::FXInputManager()
{
	// Try UAP first, should have better support
	//XInputDLL = LoadLibrary(XINPUT_DLL1);

	//if (XInputDLL == NULL) {
	//	Printf("Switching to xinput9_1_0.dll...\n");
		XInputDLL = LoadLibrary(XINPUT_DLL);
	//}

	if (XInputDLL != NULL)
	{
		InputGetState = (XInputGetStateType)GetProcAddress(XInputDLL, "XInputGetState");
		InputSetState = (XInputSetStateType)GetProcAddress(XInputDLL, "XInputSetState");
		InputGetCapabilities = (XInputGetCapabilitiesType)GetProcAddress(XInputDLL, "XInputGetCapabilities");
		InputEnable = (XInputEnableType)GetProcAddress(XInputDLL, "XInputEnable");
		InputSetVibe = (XInputSetVibeType)GetProcAddress(XInputDLL, "XInputSetState");
		// Treat XInputEnable() function as optional
		// It is not available in xinput9_1_0.dll which is XINPUT_DLL in modern SDKs
		// See https://msdn.microsoft.com/en-us/library/windows/desktop/hh405051(v=vs.85).aspx
		if (InputGetState == NULL || InputSetState == NULL || InputGetCapabilities == NULL)
		{
			FreeLibrary(XInputDLL);
			XInputDLL = NULL;
			Printf("XInput Library failed to initialize");
		}
	}
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		Devices[i] = (XInputDLL != NULL) ? new FXInputController(i) : NULL;
	}
}

//==========================================================================
//
// FXInputManager - Destructor
//
//==========================================================================

FXInputManager::~FXInputManager()
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (Devices[i] != NULL)
		{
			delete Devices[i];
		}
	}
	if (XInputDLL != NULL)
	{
		FreeLibrary(XInputDLL);
	}
}

//==========================================================================
//
// FXInputManager :: GetDevice
//
//==========================================================================

bool FXInputManager::GetDevice()
{
	return (XInputDLL != NULL);
}

//==========================================================================
//
// FXInputManager :: ProcessInput
//
// Process input for every attached device.
//
//==========================================================================

void FXInputManager::ProcessInput()
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		Devices[i]->ProcessInput();
	}
}

//===========================================================================
//
// FXInputManager :: AddAxes
//
// Adds the state of all attached device axes to the passed array.
//
//===========================================================================

void FXInputManager::AddAxes(float axes[NUM_JOYAXIS])
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (Devices[i]->IsConnected())
		{
			Devices[i]->AddAxes(axes);
		}
	}
}

//===========================================================================
//
// FXInputManager :: GetJoysticks
//
// Adds the IJoystick interfaces for each device we created to the sticks
// array, if they are detected as connected.
//
//===========================================================================

void FXInputManager::GetDevices(TArray<IJoystickConfig *> &sticks)
{
	for (int i = 0; i < XUSER_MAX_COUNT; ++i)
	{
		if (Devices[i]->IsConnected())
		{
			sticks.Push(Devices[i]);
		}
	}
}

//===========================================================================
//
// FXInputManager :: WndProcHook
//
// Enable and disable XInput as our window is (de)activated.
//
//===========================================================================

bool FXInputManager::WndProcHook(HWND hWnd, uint32_t message, WPARAM wParam, LPARAM lParam, LRESULT *result)
{
	if (nullptr != InputEnable && message == WM_ACTIVATE)
	{
		if (LOWORD(wParam) == WA_INACTIVE)
		{
			InputEnable(FALSE);
		}
		else
		{
			InputEnable(TRUE);
		}
	}
	return false;
}

//===========================================================================
//
// FXInputManager :: Rescan
//
//===========================================================================

IJoystickConfig *FXInputManager::Rescan()
{
	return NULL;
}

//===========================================================================
//
// I_StartupXInput
//
//===========================================================================

void I_StartupXInput()
{
	if (!joy_xinput || !use_joystick || Args->CheckParm("-nojoy"))
	{
		if (JoyDevices[INPUT_XInput] != NULL)
		{
			delete JoyDevices[INPUT_XInput];
			JoyDevices[INPUT_XInput] = NULL;
			UpdateJoystickMenu(NULL);
		}
	}
	else
	{
		if (JoyDevices[INPUT_XInput] == NULL)
		{
			FJoystickCollection *joys = new FXInputManager;
			if (joys->GetDevice())
			{
				JoyDevices[INPUT_XInput] = joys;
				
				// See if there is at least one joystick connected
				TArray<IJoystickConfig*> joycon;
				joys->GetDevices(joycon);
				if (joycon.Size() > 0) {
					UpdateJoystickMenu(joycon[0]);
				}
			}
			else
			{
				delete joys;
			}
		}
	}
}

