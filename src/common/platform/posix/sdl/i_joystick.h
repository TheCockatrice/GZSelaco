#ifndef __POSIX_SDL_I_JOYSTICK__
#define __POSIX_SDL_I_JOYSTICK__

#include "m_joy.h"
#include "SDL_joystick.h"
#include "TSQueue.h"

// Very small deadzone so that floating point magic doesn't happen
#define MIN_DEADZONE 0.000001f

EXTERN_CVAR(Int, joy_sdl_queuesize)

class SDLInputJoystickBase: public IJoystickConfig {
public:

	SDLInputJoystickBase() { }
	virtual void SetDefaultConfig() = 0;

    virtual int GetDeviceIndex() {
        return DeviceIndex;
    }

    virtual SDL_JoystickID GetDeviceID() {
        return DeviceID;
    }

	virtual FString GetIdentifier() {
		return "JS:" + DeviceIndex;
	}

	virtual void AddAxes(float axes[NUM_JOYAXIS]) = 0;
	virtual void ProcessInput() = 0;
	bool IsConnected() { return Connected; }

	// Getters
	float GetSensitivity() {
		return Multiplier;
	}

	int GetNumAxes() {
		return NumAxes;
	}

	float GetAxisDeadZone(int axis) {
		return Axes[axis].DeadZone;
	}

	EJoyAxis GetAxisMap(int axis) {
		return Axes[axis].GameAxis;
	}
	const char *GetAxisName(int axis) {
		return Axes[axis].Name.GetChars();
	}

	float GetAxisScale(int axis) {
		return Axes[axis].Multiplier;
	}

	float GetAxisAcceleration(int axis) override {
		return Axes[axis].Acceleration;
	}

	float GetAxis(int axis) override {
		return Axes[axis].Value;
	}

	float GetRawAxis(int axis) override {
		return Axes[axis].RawValue;
	}
	

	// Setters
	void SetSensitivity(float scale) {
		Multiplier = scale;
	}

	void SetAxisDeadZone(int axis, float zone) {
		Axes[axis].DeadZone = clamp(zone, MIN_DEADZONE, 1.f);
	}

	void SetAxisMap(int axis, EJoyAxis gameaxis) {
		Axes[axis].GameAxis = gameaxis;
	}

	void SetAxisScale(int axis, float scale) {
		Axes[axis].Multiplier = scale;
	}

	void SetAxisAcceleration(int axis, float acceleration) override {
		Axes[axis].Acceleration = acceleration;
	}

    virtual void Attached() {
        Connected = true;
        for (int i = 0; i < GetNumAxes(); ++i)
        {
            Axes[i].Value = 0;
			Axes[i].RawValue = 0;
            Axes[i].ButtonValue = 0;
            Axes[i].Inputs.pos = -1;
        }
        
        lFeed = rFeed = 0;
        feedLastUpdate = I_msTimeF();
    }

    virtual void Detached() {
        Connected = false;
    }
	

protected:
	struct AxisInfo
	{
		FString Name;
		float DeadZone;
		float Multiplier;
		float Acceleration;
		EJoyAxis GameAxis;
		double Value;
		double RawValue;
		uint8_t ButtonValue;
        RingBuffer<double, 10> Inputs;
	};

	struct DefaultAxisConfig
	{
		float DeadZone;
		EJoyAxis GameAxis;
		float Multiplier;
		float Acceleration;
	};

	int					DeviceIndex;
    SDL_JoystickID      DeviceID;
	float				Multiplier;
	TArray<AxisInfo>	Axes;
	int					NumAxes;
	bool				Connected;


    static inline void ProcessAcceleration(AxisInfo *axis, double val) {
		// Curve value - Circular
		//float cv = (1 - sqrt(1.0f - pow(abs(val), 2.0f))) * (val > 0 ? 1.0f : -1.0f);

		// Curve value, Quint
		double cv = (val * val * val * val * val);

		// Return a lerp of the actual value and the curve value
		axis->Value = clamp(cv + ((1.0 - (double)axis->Acceleration) * (val - cv)), -1.0, 1.0);
	}
};

class SDLInputJoystickManager {
public:
	SDLInputJoystickManager();
	~SDLInputJoystickManager();

	void AddAxes(float axes[NUM_JOYAXIS]);
	void GetDevices(TArray<IJoystickConfig *> &sticks);
    IJoystickConfig* GetDevice(int joyID);
    IJoystickConfig* GetDeviceFromID(int joyDeviceID);

    void DeviceRemoved(int joyID);
    void DeviceAdded(int joyID);

	void ProcessInput() const;

protected:
	TDeletingArray<SDLInputJoystickBase *> Joysticks;

    SDLInputJoystickBase *InternalGetDevice(int joyIndex);
};

extern SDLInputJoystickManager *JoystickManager;

#endif
