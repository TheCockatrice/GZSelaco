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

CCMD(lm_rebuild)
{
	level.LMNeedsRebuild = true;
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

// Completely rerender *all* of the master lightmaps. Can have a high pixel cost for maps with a lot of large, overlapping lightgroups.
void RebuildMasterLightmap(FLevelLocals* level, int index)
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

// Only render the sectors/lightgroups that have changed since we last rendered. 
// Significantly decreases pixel cost by not having to rerender the entire lightmap.
void RenderMasterLightmapPartial(FLevelLocals* level, int index)
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

			LMSectorLightGroupInfo* sectorit = master.MasterRenderSectorItFirst;
			while (sectorit != nullptr)
			{
				state->DrawIndexed(DT_Triangles, sectorit->MeshIndexStart, sectorit->MeshIndexCount);
				sectorit = sectorit->RenderItNext;
			}
		}

		// We're done with rendering the master lightmap meshes, so clear the iterators ready for the next frame
		master.MasterRenderSectorItFirst = nullptr;
		master.MasterRenderSectorItLast = nullptr;
	}

	// Render the additive lightgroups
	LightgroupLightmapInfo* lightmapit = master.LightGroupLightmapItFirst;
	while (lightmapit != nullptr)
	{
		FGameTexture* lightmap = level->Lightmaps[lightmapit->LightmapIndex + level->LMTextureCount];
		state->SetMaterial(lightmap, UF_Texture, 0, CLAMP_NONE, 0, SHADER_LightmapComposite);
		state->SetRenderStyle(STYLE_Add);

		LightGroupMesh* meshit = lightmapit->MasterInfos[index].MeshItFirst;
		while (meshit != nullptr)
		{
			assert(meshit->targetLightmapIndex == index);
			assert(meshit->sourceLightmapIndex == lightmapit->LightmapIndex + level->LMTextureCount);

			state->SetVertexBuffer(&meshit->buffers);
			state->SetObjectColor(level->LightGroups[meshit->LightGroupIndex].RenderColor);

			LMSectorLightGroupInfo* sectorit = meshit->SectorItFirst;
			while (sectorit != nullptr)
			{
				state->DrawIndexed(DT_Triangles, sectorit->MeshIndexStart, sectorit->MeshIndexCount);
				sectorit = sectorit->RenderItNext;
			}

			meshit->SectorItFirst = nullptr;
			meshit = meshit->RenderItNext;
		}

		// We're done with rendering this lightmap, so clear the iterators ready for the next frame
		lightmapit->MasterInfos[index].MeshItFirst = nullptr;
		lightmapit->MasterInfos[index].MeshItLast = nullptr;
		lightmapit->MasterInfos[index].AddedToMaster = false;

		lightmapit = lightmapit->MasterInfos[index].RenderItNext;
	}

	master.LightGroupLightmapItFirst = nullptr;
	master.LightGroupLightmapItLast = nullptr;
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
		if (level->LMSectorItFirst && !level->LMNeedsRebuild)
		{
			// Go through all of the sectors that rendered last frame
			LMSectorInfo* sectorit = level->LMSectorItFirst;
			while (sectorit != nullptr)
			{
				bool shouldAdd = false;

				// Check if this sector has any lightgroups that have updated since our last render
				for (LMSectorLightGroupInfo& lgInfo : sectorit->LightGroupInfos)
				{
					LightGroupData& lightGroup = level->LightGroups[lgInfo.LightGroupIndex];

					if (lightGroup.GroupID != 0 && lightGroup.LastUpdateFrame > sectorit->LastRenderFrame)
					{
						// Found a lightgroup that changed for this sector, so we want to continue
						shouldAdd = true;
						NumLMSectorsRendered++;
						break;
					}
				}

				if (shouldAdd)
				{
					// Queue up the sector to render to the master lightmap
					// This basically just resets this sector back to the base version so we can render lightgroups on top of it
					for (LMSectorLightGroupInfo& masterSector : sectorit->MasterInfos)
					{
						MasterLightmapInfo& masterLightmap = level->MasterLightmaps[masterSector.LightmapIndex];

						if (masterLightmap.MasterRenderSectorItFirst == nullptr)
						{
							masterLightmap.MasterRenderSectorItFirst = &masterSector;
						}

						if (masterLightmap.MasterRenderSectorItLast != nullptr)
						{
							masterLightmap.MasterRenderSectorItLast->RenderItNext = &masterSector;
						}

						masterLightmap.MasterRenderSectorItLast = &masterSector;
						masterLightmap.MasterRenderSectorItLast->RenderItNext = nullptr;
					}

					// Go through all of the lightgroups for this sector
					for (LMSectorLightGroupInfo& lightgroupSector : sectorit->LightGroupInfos)
					{
						LightGroupData& lightGroup = level->LightGroups[lightgroupSector.LightGroupIndex];
						if (!lightGroup.Enabled || lightGroup.Intensity <= 0)
						{
							// If the lightgroup won't contribute, don't add it
							continue;
						}

						LightgroupLightmapInfo* lightmap = &level->LightGroupLightmapInfos[lightgroupSector.LightmapIndex - level->LMTextureCount];
						LightGroupMesh* mesh = &level->LightGroupMeshes[lightgroupSector.MeshIndex];

						LightgroupLightmapInfo::PerMasterInfo& lightmapPerMasterInfo = lightmap->MasterInfos[mesh->targetLightmapIndex];
						assert(mesh->LightGroupIndex == lightgroupSector.LightGroupIndex);

						if (mesh->SectorItFirst == nullptr)
						{
							// Tell the master lightmap that we want to render this lightmap if it hasn't been added already
							if (!lightmapPerMasterInfo.AddedToMaster)
							{
								MasterLightmapInfo& master = level->MasterLightmaps[mesh->targetLightmapIndex];
								if (master.LightGroupLightmapItFirst == nullptr)
								{
									master.LightGroupLightmapItFirst = lightmap;
								}

								if (master.LightGroupLightmapItLast != nullptr)
								{
									master.LightGroupLightmapItLast->MasterInfos[mesh->targetLightmapIndex].RenderItNext = lightmap;
								}

								master.LightGroupLightmapItLast = lightmap;
								master.LightGroupLightmapItLast->MasterInfos[mesh->targetLightmapIndex].RenderItNext = nullptr;

								lightmapPerMasterInfo.AddedToMaster = true;
							}

							// Add this mesh to the list of meshes to render for this particular master lightmap/lightgroup pair
							if (lightmapPerMasterInfo.MeshItFirst == nullptr)
							{
								lightmapPerMasterInfo.MeshItFirst = mesh;
							}

							if (lightmapPerMasterInfo.MeshItLast != nullptr)
							{
								lightmapPerMasterInfo.MeshItLast->RenderItNext = mesh;
							}

							lightmapPerMasterInfo.MeshItLast = mesh;
							lightmapPerMasterInfo.MeshItLast->RenderItNext = nullptr;

							mesh->SectorItFirst = &lightgroupSector;
						}

						// Add this sector to the list of sectors to render for this mesh
						if (mesh->SectorItLast != nullptr)
						{
							mesh->SectorItLast->RenderItNext = &lightgroupSector;
						}

						mesh->SectorItLast = &lightgroupSector;
						mesh->SectorItLast->RenderItNext = nullptr;
					}
				}

				sectorit->LastRenderFrame = level->LMRenderFrame;
				sectorit->QueuedForRender = false;
				sectorit = sectorit->NextIt;
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
							RebuildMasterLightmap(level, i);
						}
						else
						{
							RenderMasterLightmapPartial(level, i);
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

		level->LMSectorItFirst = nullptr;
		level->LMSectorItLast = nullptr;
		level->LMNeedsRebuild = false;

		// Prevent wrapping issues if the game has been sitting forever by just marking everything as needing a rerender
		if (level->LMRenderFrame == 0xffffffff)
		{
			for (LightGroupData& lightgroup : level->LightGroups)
			{
				lightgroup.LastUpdateFrame = 0;
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
					RebuildMasterLightmap(level, i);
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
