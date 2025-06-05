#include "a_lightgroup.h"
#include "thingdef.h"
#include "c_dispatch.h"
#include "hw_renderstate.h"
#include "hw_viewpointbuffer.h"
#include "flatvertices.h"
#include "hw_bonebuffer.h"
#include "hw_lightbuffer.h"

void MarkForRender(AActor* self)
{
	self->Level->LightGroups[self->Score].TickedThisFrame = true;
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, MarkForRender, MarkForRender)
{
	PARAM_SELF_PROLOGUE(AActor);
	MarkForRender(self);
	return 0;
}

//==========================================================================
//
//
//
//==========================================================================

void UpdateTintColor(AActor* self)
{
	LightGroupData& lightGroupData = self->Level->LightGroups[self->Score];
	lightGroupData.TintColor.r = self->args[0] / 255.0f;
	lightGroupData.TintColor.g = self->args[1] / 255.0f;
	lightGroupData.TintColor.b = self->args[2] / 255.0f;
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, UpdateTintColor, UpdateTintColor)
{
	PARAM_SELF_PROLOGUE(AActor);
	UpdateTintColor(self);
	return 0;
}

//==========================================================================
//
//
//
//==========================================================================

void SetIntensity(AActor* self, double intensity)
{
	self->Level->LightGroups[self->Score].Intensity = (float)intensity;
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, SetIntensity, SetIntensity)
{
	PARAM_SELF_PROLOGUE(AActor);
	PARAM_FLOAT(intensity);
	SetIntensity(self, intensity);
	return 0;
}

//==========================================================================
//
//
//
//==========================================================================

double GetIntensity(AActor* self)
{
	return self->Level->LightGroups[self->Score].Intensity;
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, GetIntensity, GetIntensity)
{
	PARAM_SELF_PROLOGUE(AActor);
	double intensity = GetIntensity(self);
	ACTION_RETURN_FLOAT(intensity);
}

//==========================================================================
//
//
//
//==========================================================================

void ActivateLightGroup(AActor* self)
{
	self->flags2 &= ~MF2_DORMANT;
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, ActivateLightGroup, ActivateLightGroup)
{
	PARAM_SELF_PROLOGUE(AActor);
	ActivateLightGroup(self);
	return 0;
}


//==========================================================================
//
//
//
//==========================================================================

void DeactivateLightGroup(AActor* self)
{
	self->flags2 |= MF2_DORMANT;
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, DeactivateLightGroup, DeactivateLightGroup)
{
	PARAM_SELF_PROLOGUE(AActor);
	DeactivateLightGroup(self);
	return 0;
}


void P_ClearLightgroupTicks(FLevelLocals* level)
{
	for (LightGroupData& lightGroupData : level->LightGroups)
	{
		lightGroupData.TickedThisFrame = false;
	}
}

void RenderMasterLightmap(FLevelLocals* level, int index)
{
	const FLevelLocals::MasterLightmapInfo& master = level->MasterLightmaps[index];
	FRenderState* state = screen->RenderState();

	int width = master.canvas->GetWidth();
	int height = master.canvas->GetHeight();

	IntRect bounds = IntRect{ 0, 0, width, height };
	screen->SetViewportRects(&bounds);
	state->SetViewport(0, 0, width, height);
	screen->mViewpoints->Set2D(*state, width, height);

	state->SetMaterial(master.base, UF_Texture, 0, CLAMP_NONE, 0, -1);
	state->SetRenderStyle(STYLE_Normal);

	// Draw the master lightmap
	state->SetVertexBuffer(&master.quadBuffer);
	state->DrawIndexed(DT_Triangles, 0, 6, true);

	// Render the additive lightgroups
	// [NL] HACK: This goes through each of the lightgroups for *every single master lightmap*, which could probably be precalculated to save some performance
	for (LightGroupData& lightGroupData : level->LightGroups)
	{
		if (lightGroupData.TickedThisFrame)
		{
			for (const LightGroupData::Mesh& mesh : lightGroupData.Meshes)
			{
				if (mesh.targetLightmapIndex != index)
				{
					continue;
				}

				uint8_t r = (uint8_t)(lightGroupData.TintColor.r * lightGroupData.Intensity * 255);
				uint8_t g = (uint8_t)(lightGroupData.TintColor.g * lightGroupData.Intensity * 255);
				uint8_t b = (uint8_t)(lightGroupData.TintColor.b * lightGroupData.Intensity * 255);
				int color = r | (g << 8) | (b << 16) | 0xff000000;

				FGameTexture* lightmap = level->Lightmaps[mesh.sourceLightmapIndex];
				state->SetMaterial(lightmap, UF_Texture, 0, CLAMP_NONE, 0, -1);
				state->SetObjectColor(color);
				state->SetRenderStyle(STYLE_Add);
				state->SetVertexBuffer(&mesh.buffers);
				state->DrawIndexed(DT_Triangles, 0, mesh.indexCount);
			}
		}
	}

	// Copy the rendered lightmap to the one used in the level
	// TODO: This shouldn't be required; we should just be using the master textures directly in the level
	screen->BlitLightmap(master.canvas->GetHardwareTexture(0, 0), index);
}

void P_RenderLightGroups(FLevelLocals* level)
{
	FRenderState& state = *screen->RenderState();
	state.EnableStencil(false);
	state.SetStencil(0, SOP_Keep, SF_AllOn);
	state.Clear(CT_Stencil);
	state.EnableDepthTest(false);
	state.EnableMultisampling(false);
	state.EnableFog(false);
	state.EnableTexture(true);
	state.EnableBrightmap(false);
	state.EnableModelMatrix(false);
	state.EnableTextureMatrix(false);

	screen->mLights->Clear();
	screen->mBones->Clear();
	screen->mViewpoints->Clear();

	for (int i = 0; i < level->MasterLightmaps.size(); i++)
	{
		screen->RenderTextureView(level->MasterLightmaps[i].canvas,
			[level, i](IntRect& bounds)
			{
				RenderMasterLightmap(level, i);
			});
	}

	state.SetRenderStyle(STYLE_Translucent);
	state.SetVertexBuffer(screen->mVertexData);
	state.EnableBrightmap(true);
	state.SetTextureMode(TM_NORMAL);
	state.EnableFog(false);
	state.SetScreenFade(1);
	state.SetSoftLightLevel(255);
	state.EnableModelMatrix(true);
	state.EnableTextureMatrix(false);
	state.ResetColor();
}
