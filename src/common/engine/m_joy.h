#ifndef M_JOY_H
#define M_JOY_H

#include "basics.h"
#include "tarray.h"
#include "c_cvars.h"
#include "i_time.h"
#include "r_utility.h"
#include "printf.h"
#include "m_random.h"

enum EJoyAxis
{
	JOYAXIS_None = -1,
	JOYAXIS_Yaw,
	JOYAXIS_Pitch,
	JOYAXIS_Forward,
	JOYAXIS_Side,
	JOYAXIS_Up,
//	JOYAXIS_Roll,		// Ha ha. No roll for you.
	NUM_JOYAXIS,
};


// Generic configuration interface for a controller.
struct IJoystickConfig
{
	virtual ~IJoystickConfig() = 0;

	virtual FString GetName() = 0;
	virtual float GetSensitivity() = 0;
	virtual void SetSensitivity(float scale) = 0;

	virtual int GetNumAxes() = 0;
	virtual float GetAxisDeadZone(int axis) = 0;
	virtual EJoyAxis GetAxisMap(int axis) = 0;
	virtual const char *GetAxisName(int axis) = 0;
	virtual float GetAxisScale(int axis) = 0;
	virtual float GetAxisAcceleration(int axis) { return 0; }
	virtual float GetAxis(int axis) { return 0; }
	virtual float GetRawAxis(int axis) { return 0; }

	virtual void SetAxisDeadZone(int axis, float zone) = 0;
	virtual void SetAxisMap(int axis, EJoyAxis gameaxis) = 0;
	virtual void SetAxisScale(int axis, float scale) = 0;
	virtual void SetAxisAcceleration(int axis, float accel) {};

	virtual bool GetEnabled() = 0;
	virtual void SetEnabled(bool enabled) = 0;

	virtual bool AllowsEnabledInBackground() = 0;
	virtual bool GetEnabledInBackground() = 0;
	virtual void SetEnabledInBackground(bool enabled) = 0;

	// Used by the saver to not save properties that are at their defaults.
	virtual bool IsSensitivityDefault() = 0;
	virtual bool IsAxisDeadZoneDefault(int axis) = 0;
	virtual bool IsAxisMapDefault(int axis) = 0;
	virtual bool IsAxisScaleDefault(int axis) = 0;
	virtual bool IsAxisAccelerationDefault(int axis) { return true; }

	virtual void SetDefaultConfig() = 0;
	virtual FString GetIdentifier() = 0;
	virtual bool IsGamepad() { return false; }

	// @Cockatrice - Can't find a terribly appropriate place to add this functionality, so it goes here
	virtual bool AddVibration(float l, float r) { return SetVibration(lFeed + l, rFeed + r); }
	virtual bool SetVibration(float l, float r) {
		feedLastUpdate = I_msTimeF() / 1000.0;
		return InternalSetVibration(l, r);
	}

protected:
	const float FeedbackDecay = 0.5;	// Time in seconds to full decay of vibration when no value is set
	double feedLastUpdate = 0;
	float lFeed = 0, rFeed = 0;

	virtual bool InternalSetVibration(float l, float r) { return false; }	// Must implement per-api
	

	virtual bool UpdateFeedback() {
		double tm = I_msTimeF() / 1000.0;
		float te = clamp((float)(tm - (feedLastUpdate + 0.04)), 0.0f, 10.0f);

		lFeed = clamp(lFeed - (te * (1.0f / FeedbackDecay)), 0.0f, 1.0f);
		rFeed = clamp(rFeed - (te * (1.0f / FeedbackDecay)), 0.0f, 1.0f);

		return InternalSetVibration(lFeed, rFeed);
	}
};

EXTERN_CVAR(Bool, use_joystick);

bool M_LoadJoystickConfig(IJoystickConfig *joy);
void M_SaveJoystickConfig(IJoystickConfig *joy);

void Joy_GenerateButtonEvents(int oldbuttons, int newbuttons, int numbuttons, int base);
void Joy_GenerateButtonEvents(int oldbuttons, int newbuttons, int numbuttons, const int *keys);
int Joy_XYAxesToButtons(double x, double y);
double Joy_RemoveDeadZone(double axisval, double deadzone, uint8_t *buttons);

// These ought to be provided by a system-specific i_input.cpp.
void I_GetAxes(float axes[NUM_JOYAXIS]);
void I_GetJoysticks(TArray<IJoystickConfig *> &sticks);
IJoystickConfig *I_UpdateDeviceList();
extern void UpdateJoystickMenu(IJoystickConfig *);


#endif
