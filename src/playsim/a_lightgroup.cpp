#include "a_lightgroup.h"
#include "thingdef.h"
#include "c_dispatch.h"
#include "hw_renderstate.h"
#include "hw_viewpointbuffer.h"
#include "flatvertices.h"
#include "hw_bonebuffer.h"
#include "hw_lightbuffer.h"
#include "c_cvars.h"
#include "v_draw.h"
#include "v_font.h"
#include "i_time.h"
#include <utility>

CVAR(Bool, lm_lightprobes, true, 0);
CVAR(Bool, lm_dynamiclightmap, true, 0);
CVAR(Int, lm_maxlightgroups, -1, 0);
CVAR(Bool, lm_masterlightmaps, true, 0);

CCMD(lm_info)
{
	Printf("Version: %d\n", level.LMVersion);
	Printf("Master Textures: %d (Actual size: %dx%d)\n", level.MasterLightmaps.Size(), level.LMMasterTextureSize.X, level.LMMasterTextureSize.Y);
	for (unsigned int i = 0; i < level.MasterLightmaps.Size(); i++)
	{
		Printf("\t%dx%d\n", level.LMMasterTextureActualSizes[i].X, level.LMMasterTextureActualSizes[i].Y);
	}
	Printf("Lightgroups: %d\n", level.LightGroups.Size());
	Printf("Lightgroup Textures: %d\n", level.Lightmaps.Size() - level.MasterLightmaps.Size());
	for (unsigned int i = level.MasterLightmaps.Size(); i < level.Lightmaps.Size(); i++)
	{
		Printf("\t%dx%d\n", level.Lightmaps[i]->GetTexelWidth(), level.Lightmaps[i]->GetTexelHeight());
	}
	Printf("Light Probes: %d\n", level.LightProbes.Size());
	Printf("Light Probe Cell Size: %d\n", level.LPCellSize);
}

FVector3 LightProbe::CalculateColor(FLevelLocals* level) const
{
	FVector3 result = FVector3(Red, Green, Blue);

	for (uint8_t i = 0; i < numSubProbes; i++)
	{
		const SubProbe& subProbe = subProbes[i];
		const LightGroupData& lightGroup = level->LightGroups[subProbe.groupID];

		if (lightGroup.Enabled)
		{
			result += FVector3(subProbe.Red, subProbe.Green, subProbe.Blue) * lightGroup.Intensity;
		}
	}

	return result;
}

//==========================================================================
//
//
//
//==========================================================================

void UpdateTintColor(AActor* self)
{
	LightGroupData& lightgroup = self->Level->LightGroups[self->Score];
	float r = self->args[0] / 255.0f;
	float g = self->args[1] / 255.0f;
	float b = self->args[2] / 255.0f;

	if (r != lightgroup.TintColor.r || g != lightgroup.TintColor.g || b != lightgroup.TintColor.b)
	{
		lightgroup.TintColor.r = r;
		lightgroup.TintColor.g = g;
		lightgroup.TintColor.b = b;
		uint8_t ri = (uint8_t)(r * lightgroup.Intensity * 255);
		uint8_t gi = (uint8_t)(g * lightgroup.Intensity * 255);
		uint8_t bi = (uint8_t)(b * lightgroup.Intensity * 255);
		lightgroup.RenderColor = ri | (gi << 8) | (bi << 16) | 0xff000000;

		lightgroup.LastUpdateFrame = self->Level->LMRenderFrame;
	}
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
	LightGroupData& lightgroup = self->Level->LightGroups[self->Score];
	float newIntensity = (float)intensity;
	if (lightgroup.Intensity != newIntensity)
	{
		lightgroup.Intensity = newIntensity;
		uint8_t ri = (uint8_t)(lightgroup.TintColor.r * newIntensity * 255);
		uint8_t gi = (uint8_t)(lightgroup.TintColor.g * newIntensity * 255);
		uint8_t bi = (uint8_t)(lightgroup.TintColor.b * newIntensity * 255);
		lightgroup.RenderColor = ri | (gi << 8) | (bi << 16) | 0xff000000;
		lightgroup.LastUpdateFrame = self->Level->LMRenderFrame;
	}
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

	if (self->Level->LightGroups.size() > 0)
	{
		LightGroupData& lightGroup = self->Level->LightGroups[self->Score];
		
		bool wasEnabled = lightGroup.Enabled;
		lightGroup.Enabled = true;

		if (!wasEnabled)
		{
			lightGroup.LastUpdateFrame = self->Level->LMRenderFrame;
		}
	}
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

	if (self->Level->LightGroups.size() > 0)
	{
		LightGroupData& lightGroup = self->Level->LightGroups[self->Score];
		
		bool wasEnabled = lightGroup.Enabled;
		lightGroup.Enabled = false;

		if (wasEnabled)
		{
			lightGroup.LastUpdateFrame = self->Level->LMRenderFrame;
		}
	}
}

DEFINE_ACTION_FUNCTION_NATIVE(ALightGroup, DeactivateLightGroup, DeactivateLightGroup)
{
	PARAM_SELF_PROLOGUE(AActor);
	DeactivateLightGroup(self);
	return 0;
}


void RenderMasterLightmapV2(FLevelLocals* level, int index)
{
	if (lm_maxlightgroups == 0)
	{
		return;
	}

	const MasterLightmapInfo& master = level->MasterLightmaps[index];
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

		if (level->LMNeedsRebuild || master.indexCount < 0)
		{
			// Redraw the entire master lightmap
			state->SetVertexBuffer(&master.quadBuffer);
			state->DrawIndexed(DT_Triangles, 0, 6, true);
		}
		else if (master.indexCount > 0)
		{
			// Only redraw non-static areas of the lightmap
			state->SetVertexBuffer(&master.meshBuffers);
			state->DrawIndexed(DT_Triangles, 0, master.indexCount, true);
		}
	}

	// Render the additive lightgroups
	// [NL] HACK: This goes through each of the lightgroups for *every single master lightmap*, which could probably be precalculated to save some performance

	int numRendered = 0;

	for (LightGroupData& lightGroupData : level->LightGroups)
	{
		if (lightGroupData.Enabled)
		{
			if (lm_maxlightgroups >= 0 && numRendered >= lm_maxlightgroups)
			{
				return;
			}

			state->SetObjectColor(lightGroupData.RenderColor);
			state->SetRenderStyle(STYLE_Add);

			for (int meshIndex : lightGroupData.MeshIDs)
			{
				const LightGroupMesh& mesh = level->LightGroupMeshes[meshIndex];

				if (mesh.targetLightmapIndex != index)
				{
					continue;
				}

				FGameTexture* lightmap = level->Lightmaps[mesh.sourceLightmapIndex];
				state->SetMaterial(lightmap, UF_Texture, 0, CLAMP_NONE, 0, SHADER_LightmapComposite);
				state->SetVertexBuffer(&mesh.buffers);
				state->DrawIndexed(DT_Triangles, 0, mesh.indexCount);
			}

			numRendered++;
		}
	}
}

void RenderMasterLightmapV3(FLevelLocals* level, int index)
{
	if (lm_maxlightgroups == 0)
	{
		return;
	}

	MasterLightmapInfo& master = level->MasterLightmaps[index];
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

		if (level->LMNeedsRebuild || master.indexCount < 0)
		{
			// Redraw the entire master lightmap
			state->SetVertexBuffer(&master.quadBuffer);
			state->DrawIndexed(DT_Triangles, 0, 6, true);
		}
		else if (master.indexCount > 0)
		{
			state->SetVertexBuffer(&master.meshBuffers);

			LMSectorLightGroupInfo* sectorit = master.MasterRenderSectorFirst;
			while (sectorit != nullptr)
			{
				state->DrawIndexed(DT_Triangles, sectorit->MeshIndexStart, sectorit->MeshIndexCount);
				sectorit = sectorit->RenderNext;
			}
		}

		master.MasterRenderSectorFirst = nullptr;
		master.MasterRenderSectorLast = nullptr;
	}

	// Render the additive lightgroups

	LightgroupLightmapInfo* lightmapit = master.LightGroupLightmapFirst;
	while (lightmapit != nullptr)
	{
		FGameTexture* lightmap = level->Lightmaps[lightmapit->LightmapIndex + level->LMTextureCount];
		state->SetMaterial(lightmap, UF_Texture, 0, CLAMP_NONE, 0, SHADER_LightmapComposite);
		state->SetRenderStyle(STYLE_Add);

		LightGroupMesh* meshit = lightmapit->MeshFirst;
		while (meshit != nullptr)
		{
			state->SetVertexBuffer(&meshit->buffers);
			state->SetObjectColor(level->LightGroups[meshit->LightGroupIndex].RenderColor);

			LMSectorLightGroupInfo* sectorit = meshit->SectorFirst;
			while (sectorit != nullptr)
			{
				state->DrawIndexed(DT_Triangles, sectorit->MeshIndexStart, sectorit->MeshIndexCount);
				sectorit = sectorit->RenderNext;
			}

			meshit->SectorFirst = nullptr;
			meshit = meshit->RenderNext;
		}

		lightmapit->MeshFirst = nullptr;
		lightmapit->MeshLast = nullptr;
		lightmapit->AddedToMaster = false;

		lightmapit = lightmapit->RenderNext;
	}

	master.LightGroupLightmapFirst = nullptr;
	master.LightGroupLightmapLast = nullptr;
}

int NumLMSectorsRendered = 0;

void P_RenderLightGroups(FLevelLocals* level)
{
	NumLMSectorsRendered = 0;

	if (!lm_dynamiclightmap)
	{
		return;
	}

	if (level->LMVersion >= 3)
	{
		if (level->LMSectorFirst && !level->LMNeedsRebuild)
		{
			LMSectorInfo* sectorit = level->LMSectorFirst;
			while (sectorit != nullptr)
			{
				bool shouldAdd = false;

				for (LMSectorLightGroupInfo& lgInfo : sectorit->LightGroupInfos)
				{
					LightGroupData& lightGroup = level->LightGroups[lgInfo.LightGroupIndex];

					if (lightGroup.GroupID != 0 && lightGroup.LastUpdateFrame > sectorit->LastRenderFrame)
					{
						shouldAdd = true;
						NumLMSectorsRendered++;
						break;
					}
				}

				if (shouldAdd)
				{
					for (LMSectorLightGroupInfo& masterSector : sectorit->MasterInfos)
					{
						MasterLightmapInfo& masterLightmap = level->MasterLightmaps[masterSector.LightmapIndex];

						if (masterLightmap.MasterRenderSectorFirst == nullptr)
						{
							masterLightmap.MasterRenderSectorFirst = &masterSector;
						}

						if (masterLightmap.MasterRenderSectorLast != nullptr)
						{
							masterLightmap.MasterRenderSectorLast->RenderNext = &masterSector;
						}

						masterLightmap.MasterRenderSectorLast = &masterSector;
						masterLightmap.MasterRenderSectorLast->RenderNext = nullptr;
					}

					for (LMSectorLightGroupInfo& lightgroupSector : sectorit->LightGroupInfos)
					{
						LightGroupData& lightGroup = level->LightGroups[lightgroupSector.LightGroupIndex];
						if (!lightGroup.Enabled || lightGroup.Intensity <= 0)
						{
							continue;
						}

						LightgroupLightmapInfo* lightmap = &level->LightGroupLightmapInfos[lightgroupSector.LightmapIndex - level->LMTextureCount];
						LightGroupMesh* mesh = &level->LightGroupMeshes[lightgroupSector.MeshIndex];

						assert(mesh->LightGroupIndex == lightgroupSector.LightGroupIndex);

						if (mesh->SectorFirst == nullptr)
						{
							if (!lightmap->AddedToMaster)
							{
								MasterLightmapInfo& master = level->MasterLightmaps[mesh->targetLightmapIndex];
								if (master.LightGroupLightmapFirst == nullptr)
								{
									master.LightGroupLightmapFirst = lightmap;
								}

								if (master.LightGroupLightmapLast != nullptr)
								{
									master.LightGroupLightmapLast = lightmap;
								}

								master.LightGroupLightmapLast = lightmap;
								master.LightGroupLightmapLast->RenderNext = nullptr;

								lightmap->AddedToMaster = true;
							}

							// Hasn't been added to the mesh list yet, so add it
							if (lightmap->MeshFirst == nullptr)
							{
								lightmap->MeshFirst = mesh;
							}

							if (lightmap->MeshLast != nullptr)
							{
								lightmap->MeshLast->RenderNext = mesh;
							}

							lightmap->MeshLast = mesh;
							lightmap->MeshLast->RenderNext = nullptr;

							mesh->SectorFirst = &lightgroupSector;
						}

						if (mesh->SectorLast != nullptr)
						{
							mesh->SectorLast->RenderNext = &lightgroupSector;
						}

						mesh->SectorLast = &lightgroupSector;
						mesh->SectorLast->RenderNext = nullptr;
					}
				}

				sectorit->LastRenderFrame = level->LMRenderFrame;
				sectorit->QueuedForRender = false;
				sectorit = sectorit->Next;
			}
		}

		if (NumLMSectorsRendered > 0 || level->LMNeedsRebuild)
		{
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
						if (level->LMNeedsRebuild)
						{
							RenderMasterLightmapV2(level, i);
						}
						else
						{
							RenderMasterLightmapV3(level, i);
						}
					});
			}

			screen->SetViewportRects(nullptr);

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

		level->LMSectorFirst = nullptr;
		level->LMSectorLast = nullptr;
		level->LMNeedsRebuild = false;

		// Prevent wrapping issues if the game has been sitting forever by just marking everything as needing a rerender
		if (level->LMRenderFrame == 0xffffffff)
		{
			for (LightGroupData& lightgroup : level->LightGroups)
			{
				lightgroup.LastUpdateFrame = 1;
			}

			for (LMSectorInfo& sectorInfo : level->LMSectorInfos)
			{
				sectorInfo.LastRenderFrame = 0;
			}

			level->LMRenderFrame = 0;
			level->LMNeedsRebuild = true;
		}

		level->LMRenderFrame++;
	}
	else
	{
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
					RenderMasterLightmapV2(level, i);
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

		level->LMNeedsRebuild = false;
	}
}

ADD_STAT(lightmaps)
{
	static int AllSectorsRendered = 0;
	static int MinSectorsRendered = INT_MAX;
	static int MaxSectorsRendered = -1;

	AllSectorsRendered += NumLMSectorsRendered;
	MinSectorsRendered = std::min(MinSectorsRendered, NumLMSectorsRendered);
	MaxSectorsRendered = std::max(MaxSectorsRendered, NumLMSectorsRendered);

	static FString buff;
	static int64_t lasttime = 0;
	int64_t t = I_msTime();
	if (t - lasttime > 500)
	{
		buff.Truncate(0);
		buff.AppendFormat(
			"Lightmap Sectors Rendered=%d (Min: %d, Max: %d)", AllSectorsRendered, MinSectorsRendered, MaxSectorsRendered);
		lasttime = t;

		AllSectorsRendered = 0;
		MinSectorsRendered = INT_MAX;
		MaxSectorsRendered = -1;
	}

	return buff;
}
