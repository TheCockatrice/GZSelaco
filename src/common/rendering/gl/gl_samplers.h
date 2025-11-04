#ifndef __GL_SAMPLERS_H
#define __GL_SAMPLERS_H

#include "gl_hwtexture.h"
#include "textures.h"

namespace OpenGLRenderer
{


class FSamplerManager
{
	unsigned int mSamplers[NUMSAMPLERS];

	void UnbindAll();

public:

	FSamplerManager(bool isArc = false);
	~FSamplerManager();

	uint8_t Bind(int texunit, int num, int lastval);
	void SetTextureFilterMode();

	bool isArc = false;		// @Cocaktrice - On IntelARC we cannot turn on anisotropy when using GL_NEAREST
};

}
#endif

