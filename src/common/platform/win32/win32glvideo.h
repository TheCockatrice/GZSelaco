#pragma once

#include "win32basevideo.h"

//==========================================================================
//
// 
//
//==========================================================================

class Win32GLVideo : public Win32BaseVideo
{
public:
	Win32GLVideo();

	DFrameBuffer *CreateFrameBuffer() override;
	bool InitHardware(HWND Window, int multisample);
	void Shutdown();

	void setNULLContext();
	bool setMainContext();
	bool setAuxContext(int index);
	int createAuxContext();
	int numAuxContexts();

protected:
	HGLRC m_hRC;
	int glVersion = 0;
	int glProfile = 0;
	

	HWND InitDummy();
	void ShutdownDummy(HWND dummy);
	bool SetPixelFormat();
	bool SetupPixelFormat(int multisample);
};