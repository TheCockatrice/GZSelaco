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
	void setMainContext();
	void setAuxContext(int index);
	int numAuxContexts();

protected:
	HGLRC m_hRC;

	HWND InitDummy();
	void ShutdownDummy(HWND dummy);
	bool SetPixelFormat();
	bool SetupPixelFormat(int multisample);
};