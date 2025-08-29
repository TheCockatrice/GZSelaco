#include "a_lightgroup.h"
#include "thingdef.h"
#include "c_dispatch.h"
#include "hw_renderstate.h"
#include "hw_viewpointbuffer.h"
#include "flatvertices.h"
#include "hw_bonebuffer.h"
#include "hw_lightbuffer.h"
#include "c_cvars.h"

CVAR(Bool, lm_lightprobes, true, 0);
CVAR(Bool, lm_dynamiclightmap, true, 0);
CVAR(Int, lm_maxlightgroups, -1, 0);
CVAR(Bool, lm_masterlightmaps, true, 0);

CCMD(lm_info)
{
	Printf("Lightmap Info: \n");
	Printf("Master Lightmaps: %d\n", level.MasterLightmaps.Size());
	for (unsigned int i = 0; i < level.MasterLightmaps.Size(); i++)
	{
		Printf("\t%dx%d\n", level.MasterLightmaps[i].base->GetTexelWidth(), level.MasterLightmaps[i].base->GetTexelHeight());
	}
	Printf("Lightgroup Textures: %d\n", level.Lightmaps.Size());
	for (unsigned int i = 0; i < level.Lightmaps.Size(); i++)
	{
		Printf("\t%dx%d\n", level.Lightmaps[i]->GetTexelWidth(), level.Lightmaps[i]->GetTexelHeight());
	}
	Printf("Lightgroups: %d\n", level.LightGroups.Size());
	Printf("Light Probes: %d\n", level.LightProbes.Size());
	Printf("Light Probe Cell Size: ", level.LPCellSize);
}

FVector3 LightProbe::CalculateColor(FLevelLocals* level) const
{
	FVector3 result = FVector3(Red, Green, Blue);

	for (uint8_t i = 0; i < numSubProbes; i++)
	{
		const SubProbe& subProbe = subProbes[i];
		const LightGroupData& lightGroup = level->LightGroups[subProbe.groupID];

		if (lightGroup.TickedThisFrame)
		{
			result += FVector3(subProbe.Red, subProbe.Green, subProbe.Blue) * lightGroup.Intensity;
		}
	}

	return result;
}

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
	if (lm_maxlightgroups == 0)
	{
		return;
	}

	const FLevelLocals::MasterLightmapInfo& master = level->MasterLightmaps[index];
	FRenderState* state = screen->RenderState();

	int width = master.base->GetTexelWidth();
	int height = master.base->GetTexelHeight();

	IntRect bounds = IntRect{ 0, 0, width, height };
	screen->SetViewportRects(&bounds);
	state->SetViewport(0, 0, width, height);
	screen->mViewpoints->Set2D(*state, width, height);

	if (lm_masterlightmaps)
	{
		state->SetMaterial(master.base, UF_Texture, 0, CLAMP_NONE, 0, SHADER_LightmapComposite);
		state->SetRenderStyle(STYLE_Normal);
		state->SetObjectColor(0xffffffff);

		// Draw the master lightmap
		state->SetVertexBuffer(&master.quadBuffer);
		state->DrawIndexed(DT_Triangles, 0, 6, true);
	}

	// Render the additive lightgroups
	// [NL] HACK: This goes through each of the lightgroups for *every single master lightmap*, which could probably be precalculated to save some performance

	int numRendered = 0;

	for (LightGroupData& lightGroupData : level->LightGroups)
	{
		if (lightGroupData.TickedThisFrame)
		{
			if (lm_maxlightgroups >= 0 && numRendered >= lm_maxlightgroups)
			{
				return;
			}

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
				state->SetMaterial(lightmap, UF_Texture, 0, CLAMP_NONE, 0, SHADER_LightmapComposite);
				state->SetObjectColor(color);
				state->SetRenderStyle(STYLE_Add);
				state->SetVertexBuffer(&mesh.buffers);
				state->DrawIndexed(DT_Triangles, 0, mesh.indexCount);
			}

			numRendered++;
		}
	}
}

void P_RenderLightGroups(FLevelLocals* level)
{
	if (!lm_dynamiclightmap)
	{
		return;
	}

	FRenderState& state = *screen->RenderState();
	state.EnableStencil(false);
	state.SetStencil(0, SOP_Keep, SF_AllOn);
	state.Clear(CT_Stencil);
	state.EnableDepthTest(false);
	state.EnableMultisampling(false);
	state.EnableFog(false);
	state.EnableTexture(true);
	state.SetTextureMode(TM_NORMAL);
	state.EnableBrightmap(false);
	state.EnableModelMatrix(false);
	state.EnableTextureMatrix(false);
	state.SetDynLight(0, 0, 0);
	state.SetLightProbe(0, 0, 0);

	screen->mLights->Clear();
	screen->mBones->Clear();
	screen->mViewpoints->Clear();

	for (int i = 0; i < level->MasterLightmaps.size(); i++)
	{
		screen->RenderLightmapSlice(i,
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
	state.EnableTextureMatrix(false);
	state.ResetColor();
}
