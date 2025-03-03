/*
**  Vulkan backend
**  Copyright (c) 2016-2020 Magnus Norddahl
**
**  This software is provided 'as-is', without any express or implied
**  warranty.  In no event will the authors be held liable for any damages
**  arising from the use of this software.
**
**  Permission is granted to anyone to use this software for any purpose,
**  including commercial applications, and to alter it and redistribute it
**  freely, subject to the following restrictions:
**
**  1. The origin of this software must not be misrepresented; you must not
**     claim that you wrote the original software. If you use this software
**     in a product, an acknowledgment in the product documentation would be
**     appreciated but is not required.
**  2. Altered source versions must be plainly marked as such, and must not be
**     misrepresented as being the original software.
**  3. This notice may not be removed or altered from any source distribution.
**
*/

#include "volk/volk.h"

#include <inttypes.h>

#include "v_video.h"
#include "m_png.h"

#include "r_videoscale.h"
#include "i_time.h"
#include "v_text.h"
#include "version.h"
#include "v_draw.h"

#include "hw_clock.h"
#include "hw_vrmodes.h"
#include "hw_cvars.h"
#include "hw_skydome.h"
#include "hwrenderer/data/hw_viewpointbuffer.h"
#include "flatvertices.h"
#include "hwrenderer/data/shaderuniforms.h"
#include "hw_lightbuffer.h"

#include "vk_framebuffer.h"
#include "vk_hwbuffer.h"
#include "vulkan/renderer/vk_renderstate.h"
#include "vulkan/renderer/vk_renderpass.h"
#include "vulkan/renderer/vk_descriptorset.h"
#include "vulkan/renderer/vk_streambuffer.h"
#include "vulkan/renderer/vk_postprocess.h"
#include "vulkan/renderer/vk_raytrace.h"
#include "vulkan/shaders/vk_shader.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/textures/vk_samplers.h"
#include "vulkan/textures/vk_hwtexture.h"
#include "vulkan/textures/vk_texture.h"
#include "vulkan/system/vk_builders.h"
#include "vulkan/system/vk_swapchain.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/system/vk_buffer.h"
#include "engineerrors.h"
#include "c_dispatch.h"

#include "image.h"

EXTERN_CVAR(Bool, r_drawvoxels)
EXTERN_CVAR(Int, gl_tonemap)
EXTERN_CVAR(Int, screenblocks)
EXTERN_CVAR(Bool, cl_capfps)
EXTERN_CVAR(Int, vk_max_transfer_threads)
EXTERN_CVAR(Bool, gl_texture_thread)
EXTERN_CVAR(Int, gl_background_flush_count)

CCMD(vk_memstats)
{
	if (screen->IsVulkan())
	{
		VmaStats stats = {};
		vmaCalculateStats(static_cast<VulkanFrameBuffer*>(screen)->device->allocator, &stats);
		Printf("Allocated objects: %d, used bytes: %d MB\n", (int)stats.total.allocationCount, (int)stats.total.usedBytes / (1024 * 1024));
		Printf("Unused range count: %d, unused bytes: %d MB\n", (int)stats.total.unusedRangeCount, (int)stats.total.unusedBytes / (1024 * 1024));
	}
	else
	{
		Printf("Vulkan is not the current render device\n");
	}
}

CVAR(Bool, vk_raytrace, false, 0/*CVAR_ARCHIVE | CVAR_GLOBALCONFIG*/)	// @Cockatrice - UGH



ADD_STAT(vkloader)
{
	static int maxQueue = 0, maxSecondaryQueue = 0, queue, secQueue, total, collisions, outSize;
	static double minLoad = 0, maxLoad = 0, avgLoad = 0;

	auto sc = dynamic_cast<VulkanFrameBuffer *>(screen);

	if (sc) {
		if (!sc->SupportsBackgroundCache()) {
			return FString("Vulkan Texture Thread Disabled");
		}
		sc->GetBGQueueSize(queue, secQueue, collisions, maxQueue, maxSecondaryQueue, total, outSize);
		sc->GetBGStats(minLoad, maxLoad, avgLoad);

		FString out;
		out.AppendFormat(
			"[%d Threads] Queued: %3.3d - %3.3d Out: %3.3d  Col: %d\nMax: %3.3d Max Sec: %3.3d Tot: %d\n"
			"Min: %.3fms\n"
			"Max: %.3fms\n"
			"Avg: %.3fms\n",
			sc->GetNumThreads(), queue, secQueue, outSize, collisions, maxQueue, maxSecondaryQueue, total, minLoad, maxLoad, avgLoad
		);
		return out;
	}
	
	return "No Vulkan Device";
}

CCMD(vk_rstbgstats) {
	auto sc = dynamic_cast<VulkanFrameBuffer *>(screen);
	if (sc) sc->ResetBGStats();
}


void VulkanFrameBuffer::GetBGQueueSize(int& current, int& secCurrent, int& collisions, int& max, int& maxSec, int& total, int& outSize) {
	max = maxSec = total = 0;
	current = primaryTexQueue.size();
	secCurrent = secondaryTexQueue.size();
	max = statMaxQueued;
	maxSec = statMaxQueuedSecondary;
	collisions = statCollisions;
	outSize = outputTexQueue.size();

	for (auto& tfr : bgTransferThreads) {
		total += tfr->statTotalLoaded();
	}
}



void VulkanFrameBuffer::GetBGStats(double &min, double &max, double &avg) {
	min = 99999998;
	max = avg = 0;

	for (auto& tfr : bgTransferThreads) {
		min = std::min(tfr->statMinLoadTime(), min);
		max = std::max(tfr->statMaxLoadTime(), max);
		avg += tfr->statAvgLoadTime();
	}

	avg /= (double)bgTransferThreads.size();
}


void VulkanFrameBuffer::ResetBGStats() {
	statMaxQueued = statMaxQueuedSecondary = 0;
	for (auto& tfr : bgTransferThreads) tfr->resetStats();
	statCollisions = 0;
}

bool VulkanFrameBuffer::CachingActive() {
	return secondaryTexQueue.size() > 0;
}

// TODO: Change this to report the actual progress once we have a way to mark the total number of objects to load
float VulkanFrameBuffer::CacheProgress() {
	float total = 0;

	return (float)secondaryTexQueue.size();
}


// @Cockatrice - Background Loader Stuff ===========================================
// =================================================================================
VkTexLoadThread::~VkTexLoadThread() {
	/*VulkanDevice* device = cmd->GetFrameBuffer()->device;

	// Finish up anything that was running so we can destroy the resources
	if(submits > 0) {
		vkWaitForFences(device->device, submits, submitWaitFences, VK_TRUE, std::numeric_limits<uint64_t>::max());
		vkResetFences(device->device, submits, submitWaitFences);
		deleteList.clear();
	}*/
}


bool VkTexLoadThread::loadResource(VkTexLoadIn &input, VkTexLoadOut &output) {
	currentImageID.store(input.imgSource->GetId());

	FImageLoadParams *params = input.params;

	output.conversion = params->conversion;
	output.imgSource = input.imgSource;
	output.translation = params->translation;
	output.tex = input.tex;
	output.spi.generateSpi = input.spi.generateSpi;
	output.spi.notrimming = input.spi.notrimming;
	output.spi.shouldExpand = input.spi.shouldExpand;
	output.gtex = input.gtex;
	output.releaseSemaphore = nullptr;

	// Load pixels directly with the reader we copied on the main thread
	auto *src = input.imgSource;
	FBitmap pixels;

	bool gpu = src->IsGPUOnly();
	int exx = input.spi.shouldExpand && !gpu;
	int srcWidth = src->GetWidth();
	int srcHeight = src->GetHeight();
	int buffWidth = src->GetWidth() + 2 * exx;
	int buffHeight = src->GetHeight() + 2 * exx;
	bool indexed = false;	// TODO: Determine this properly
	bool mipmap = !indexed && input.allowMipmaps;
	VkFormat fmt = indexed ? VK_FORMAT_R8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
	VulkanDevice* device = cmd->GetFrameBuffer()->device;

	unsigned char* pixelData = nullptr;
	size_t pixelDataSize = 0;
	bool freePixels = false;


	if (exx && !gpu) {
		pixels.Create(buffWidth, buffHeight); // TODO: Error checking

		// This is incredibly wasteful, but necessary for now since we can't read the bitmap with an offset into a larger buffer
		// Read into a buffer and blit 
		FBitmap srcBitmap;
		srcBitmap.Create(srcWidth, srcHeight);
		output.isTranslucent = src->ReadPixels(params, &srcBitmap);
		pixels.Blit(exx, exx, srcBitmap);

		// If we need sprite positioning info, generate it here and assign it in the main thread later
		if (input.spi.generateSpi) {
			FGameTexture::GenerateInitialSpriteData(output.spi.info, &srcBitmap, input.spi.shouldExpand, input.spi.notrimming);
		}

		pixelData = pixels.GetPixels();
		pixelDataSize = pixels.GetBufferSize();
	}
	else {
		if (gpu) {
			// GPU only textures cannot be trimmed or translated, so just do a straight read
			size_t totalSize;
			int numMipLevels;
			output.isTranslucent = src->ReadCompressedPixels(params->reader, &pixelData, totalSize, pixelDataSize, numMipLevels);
			freePixels = true;
			mipmap = false;
			fmt = VK_FORMAT_BC7_UNORM_BLOCK;

			uint32_t expectedMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(buffWidth, buffHeight)))) + 1;
			if (numMipLevels != (int)expectedMipLevels || numMipLevels == 0) {
				// Abort mips
				output.tex->BackgroundCreateTexture(cmd, buffWidth, buffHeight, 4, fmt, pixelData, false, false, (int)pixelDataSize);
			}
			else {
				// Base texture
				output.tex->BackgroundCreateTexture(cmd, buffWidth, buffHeight, indexed ? 1 : 4, fmt, pixelData, true, false, (int)pixelDataSize);

				// Mips
				uint32_t mipWidth = buffWidth, mipHeight = buffHeight;
				uint32_t mipSize = (uint32_t)pixelDataSize, dataPos = (uint32_t)pixelDataSize;

				for (int x = 1; x < numMipLevels; x++) {
					mipWidth	= std::max(1u, (mipWidth >> 1));
					mipHeight	= std::max(1u, (mipHeight >> 1));
					mipSize		= std::max(1u, ((mipWidth + 3) / 4)) * std::max(1u, ((mipHeight + 3) / 4)) * 16;
					if (mipSize == 0 || totalSize - dataPos < mipSize) 
						break;
					output.tex->BackgroundCreateTextureMipMap(cmd, x, mipWidth, mipHeight, 4, fmt, pixelData + dataPos, mipSize);
					dataPos += mipSize;
				}
			}

			if (input.spi.generateSpi) {
				// Generate sprite data without pixel data, since no trimming should occur
				FGameTexture::GenerateEmptySpriteData(output.spi.info, buffWidth, buffHeight);
			}
		}
		else {
			pixels.Create(buffWidth, buffHeight); // TODO: Error checking
			output.isTranslucent = src->ReadPixels(params, &pixels);
			pixelData = pixels.GetPixels();
			pixelDataSize = pixels.GetBufferSize();

			if (input.spi.generateSpi) {
				FGameTexture::GenerateInitialSpriteData(output.spi.info, &pixels, input.spi.shouldExpand, input.spi.notrimming);
			}
		}
	}
	
	/*if (input.translationRemap) {

	} else if (IsLuminosityTranslation(input.translation)) {
		V_ApplyLuminosityTranslation(input.translation, pixels.GetPixels(), src->GetWidth() * src->GetHeight());
	}*/

	delete input.params;


	if (!gpu) {
		output.tex->BackgroundCreateTexture(cmd, buffWidth, buffHeight, indexed ? 1 : 4, fmt, pixelData, mipmap, mipmap, (int)pixelDataSize);
	}

	// Wait for operations to finish, since we can't maintain a regular loop of clearing the buffer
	if (cmd->TransferDeleteList->TotalSize > 1) {
		cmd->WaitForCommands(false, true);
	}

	output.createMipmaps = mipmap && !uploadQueue.familySupportsGraphics;

	if (freePixels) {
		free(pixelData);
	}

	// If we created the texture on a different family than the graphics family, we need to release access 
	// to the image on this queue
	if(device->graphicsFamily != uploadQueue.queueFamily) {
		auto cmds = cmd->CreateUnmanagedCommands();
		cmds->SetDebugName("BGThread::QueueMoveCMDS");
		output.releaseSemaphore = new VulkanSemaphore(device);
		output.tex->ReleaseLoadedFromQueue(cmds.get(), uploadQueue.queueFamily, device->graphicsFamily);
		cmds->end();

		QueueSubmit submit;
		submit.AddCommandBuffer(cmds.get());
		submit.AddSignal(output.releaseSemaphore);
		
		deleteList.push_back(std::move(cmds));

		//if(++submits == 8) {
			// TODO: We have to wait for each submit right now, because for some reason we can't rely on sempaphores
			// being used by the time we get back to the main thread and move resources to the main graphics queue.
			// I believe this is incorrect, we should be able to move on here without having to wait.
			submits = 1;
			submit.Execute(device, uploadQueue.queue, submitFences[submits - 1].get());
			vkWaitForFences(device->device, submits, submitWaitFences, VK_TRUE, std::numeric_limits<uint64_t>::max());
			vkResetFences(device->device, submits, submitWaitFences);
			deleteList.clear();
			submits = 0;
		//} else {
		//	submit.Execute(device, device->uploadQueue, submitFences[submits - 1].get());
		//}
	}

	// Always return true, because failed images need to be marked as unloadable
	// TODO: Mark failed images as unloadable so they don't keep coming back to the queue
	return true;
}

void VkTexLoadThread::cancelLoad() { currentImageID.store(0); }
void VkTexLoadThread::completeLoad() { currentImageID.store(0); }

// END Background Loader Stuff =====================================================

void VulkanFrameBuffer::FlushBackground() {
	int nq = primaryTexQueue.size() + secondaryTexQueue.size();
	bool active = nq;

	if(!active)
		for (auto& tfr : bgTransferThreads) active = active || tfr->isActive();

	Printf(TEXTCOLOR_GREEN"VulkanFrameBuffer[%s]: Flushing [%d + %d + %d] texture load ops\n", active ? "active" : "inactive", nq, patchQueue.size(), nq + patchQueue.size());

	// Make sure active is marked if we have patches waiting
	active = active || patchQueue.size() > 0;

	// Finish anything queued, and send anything that needs to be loaded from the patch queue
	UpdateBackgroundCache(true);
		
	// Wait for everything to load, kinda cheating here but this shouldn't be called in the game loop only at teardown
	cycle_t check = cycle_t();
	check.Clock();
	
	while (active) {
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		UpdateBackgroundCache(true);

		active = false;
		for (auto& tfr : bgTransferThreads) 
			active = active || tfr->isActive();
	}

	// Finish anything that was loaded
	UpdateBackgroundCache(true);

	check.Unclock();
	Printf(TEXTCOLOR_GOLD"VulkanFrameBuffer::FlushBackground() took %f ms\n", check.TimeMS());
}

void VulkanFrameBuffer::UpdateBackgroundCache(bool flush) {
	// Check for completed cache items and link textures to the data
	VkTexLoadOut loaded;
	
	// If we have previously made a submit, make sure it has finished and release resources
	if(bgtFence.get() && bgtHasFence) {
		bgtHasFence = false;
		vkWaitForFences(device->device, 1, &bgtFence->fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
		vkResetFences(device->device, 1, &bgtFence->fence);
		bgtCmds.release();
		bgtSm4List.clear();
	}

	// Start processing finished resource loads
	if(outputTexQueue.size()) {
		// Do transfers need to be made?
		bool familyPicnic = false;
		for (int bgIndex = (int)bgTransferThreads.size() - 1; bgIndex >= 0; bgIndex--) {
			if (bgTransferThreads[bgIndex]->getUploadQueue().queueFamily != device->graphicsFamily) {
				familyPicnic = true;
				break;
			}
		}

		// TODO: Limit the total amount of commands we are going to send per-frame, or at least per-submit
		std::unique_ptr<VulkanCommandBuffer> cmds = familyPicnic ? mCommands->CreateUnmanagedCommands() : nullptr;
		if (cmds.get()) cmds->SetDebugName("MainThread::QueueMoveCMDS");

		unsigned int sm4StartIndex = (unsigned int)bgtSm4List.size() - 1;

		int dequeueCount = 0;
		while ((flush || dequeueCount < gl_background_flush_count) && outputTexQueue.dequeue(loaded)) {
			dequeueCount++;

			if (loaded.tex->hwState == IHardwareTexture::HardwareState::READY) {
				statCollisions++;
				//Printf("Background proc Loaded an already-loaded image: %s What a farting mistake!\n", loaded.gtex ? loaded.gtex->GetName().GetChars() : "UNKNOWN");

				// Set the sprite positioning if we loaded it and it hasn't already been applied
				if (loaded.spi.generateSpi && loaded.gtex && !loaded.gtex->HasSpritePositioning()) {
					//Printf("Setting SPI even though we rejected the texture.\n");
					SpritePositioningInfo* spi = (SpritePositioningInfo*)ImageArena.Alloc(2 * sizeof(SpritePositioningInfo));
					memcpy(spi, loaded.spi.info, 2 * sizeof(SpritePositioningInfo));
					loaded.gtex->SetSpriteRect(spi);
				}

				// TODO: Release resources now, instead of automatically doing it later
				continue;
			}

			// If this image was created in a different queue family, it now needs to be moved over to
			// the graphics queue faimly
			if (device->uploadFamily != device->graphicsFamily && loaded.tex->mLoadedImage) {
				loaded.tex->AcquireLoadedFromQueue(cmds.get(), device->uploadFamily, device->graphicsFamily);

				// If we cannot create mipmaps in the background, tell the GPU to create them now
				if (loaded.createMipmaps) {
					loaded.tex->mLoadedImage.get()->GenerateMipmaps(cmds.get());
				}

				if (loaded.releaseSemaphore) {
					loaded.releaseSemaphore->SetDebugName("BGT::RlsA");
					bgtSm4List.push_back(std::unique_ptr<VulkanSemaphore>(loaded.releaseSemaphore));
				}
			}

			loaded.tex->SwapToLoadedImage();
			loaded.tex->SetHardwareState(IHardwareTexture::HardwareState::READY);
			if (loaded.gtex) loaded.gtex->SetTranslucent(loaded.isTranslucent);

			// Set the sprite positioning info if generated
			if (loaded.spi.generateSpi && loaded.gtex) {
				if (!loaded.gtex->HasSpritePositioning()) {
					SpritePositioningInfo* spi = (SpritePositioningInfo*)ImageArena.Alloc(2 * sizeof(SpritePositioningInfo));
					memcpy(spi, loaded.spi.info, 2 * sizeof(SpritePositioningInfo));
					loaded.gtex->SetSpriteRect(spi);
				}
#ifdef DEBUG
				else {
					Printf(TEXTCOLOR_RED"%s ALREADY HAS SPRITE POSITIONING!!  %p\n", loaded.gtex->GetName(), this);
				}
#endif
			}
		}

		// We only need to run commands on the GPU if transfer/mipmaps were needed
		if (cmds != nullptr) {
			QueueSubmit submit;
			// Just in case no mipmaps were created, but we transferred ownership
			// Make sure fragment shaders don't process before our transfers are done
			PipelineBarrier().AddMemory(VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT).Execute(
				cmds.get(),
				VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
			);

			cmds->end();

			submit.AddCommandBuffer(cmds.get());

			for (unsigned int x = sm4StartIndex; x < bgtSm4List.size(); x++) {
				submit.AddWait(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, bgtSm4List[x].get());
			}

			if (!bgtFence.get()) bgtFence.reset(new VulkanFence(device));

			submit.Execute(device, device->graphicsQueue, bgtFence.get());

			if (flush) {
				// Wait if flushing
				bgtHasFence = false;
				vkWaitForFences(device->device, 1, &bgtFence->fence, VK_TRUE, std::numeric_limits<uint64_t>::max());
				vkResetFences(device->device, 1, &bgtFence->fence);
				bgtSm4List.clear();
			}
			else {
				bgtHasFence = true;
				bgtCmds = std::move(cmds);
			}
		}
	}


	// Submit all of the patches that need to be loaded
	QueuedPatch qp;
	while (patchQueue.dequeue(qp)) {
		FMaterial * gltex = FMaterial::ValidateTexture(qp.tex, qp.scaleFlags, true);
		if (gltex && !gltex->IsHardwareCached(qp.translation)) {
			BackgroundCacheMaterial(gltex, qp.translation, qp.generateSPI);
		}
	}
}



VulkanFrameBuffer::VulkanFrameBuffer(void *hMonitor, bool fullscreen, VulkanDevice *dev) : 
	Super(hMonitor, fullscreen) 
{
	device = dev;
}

VulkanFrameBuffer::~VulkanFrameBuffer()
{
	StopBackgroundCache();
	vkDeviceWaitIdle(device->device); // make sure the GPU is no longer using any objects before RAII tears them down

	delete mVertexData;
	delete mSkyData;
	delete mViewpoints;
	delete mLights;
	mShadowMap.Reset();

	if (mDescriptorSetManager)
		mDescriptorSetManager->Deinit();
	if (mTextureManager)
		mTextureManager->Deinit();
	if (mBufferManager)
		mBufferManager->Deinit();
	if (mShaderManager)
		mShaderManager->Deinit();

	mCommands->DeleteFrameObjects();
	for(auto &cmds : mBGTransferCommands) cmds->DeleteFrameObjects();
}

void VulkanFrameBuffer::StopBackgroundCache() {
	primaryTexQueue.clear();
	secondaryTexQueue.clear();
	outputTexQueue.clear();

	for (auto& tfr : bgTransferThreads) {
		//tfr->clearInputQueue();
		//tfr->clearSecondaryInputQueue();
		tfr->stop();
	}
}

void VulkanFrameBuffer::InitializeState()
{
	static bool first = true;
	if (first)
	{
		PrintStartupLog();
		first = false;
	}

	// Use the same names here as OpenGL returns.
	switch (device->PhysicalDevice.Properties.vendorID)
	{
	case 0x1002: vendorstring = "ATI Technologies Inc.";     break;
	case 0x10DE: vendorstring = "NVIDIA Corporation";  break;
	case 0x8086: vendorstring = "Intel";   break;
	default:     vendorstring = "Unknown"; break;
	}

	hwcaps = RFL_SHADER_STORAGE_BUFFER | RFL_BUFFER_STORAGE;
	glslversion = 4.50f;
	uniformblockalignment = (unsigned int)device->PhysicalDevice.Properties.limits.minUniformBufferOffsetAlignment;
	maxuniformblock = device->PhysicalDevice.Properties.limits.maxUniformBufferRange;

	mCommands.reset(new VkCommandBufferManager(this, &device->graphicsQueue, device->graphicsFamily));

	mSamplerManager.reset(new VkSamplerManager(this));
	mTextureManager.reset(new VkTextureManager(this));
	mBufferManager.reset(new VkBufferManager(this));
	mBufferManager->Init();

	mScreenBuffers.reset(new VkRenderBuffers(this));
	mSaveBuffers.reset(new VkRenderBuffers(this));
	mActiveRenderBuffers = mScreenBuffers.get();

	mPostprocess.reset(new VkPostprocess(this));
	mDescriptorSetManager.reset(new VkDescriptorSetManager(this));
	mRenderPassManager.reset(new VkRenderPassManager(this));
	mRaytrace.reset(new VkRaytrace(this));

	mVertexData = new FFlatVertexBuffer(GetWidth(), GetHeight());
	mSkyData = new FSkyVertexBuffer;
	mViewpoints = new HWViewpointBuffer;
	mLights = new FLightBuffer();

	mShaderManager.reset(new VkShaderManager(this));
	mDescriptorSetManager->Init();
#ifdef __APPLE__
	mRenderState.reset(new VkRenderStateMolten(this));
#else
	mRenderState.reset(new VkRenderState(this));
#endif

	// @Cockatrice - Init the background loader
	bgTransferThreads.clear();
	if (gl_texture_thread && vk_max_transfer_threads >= 0 && device->uploadQueues.size() > 0) {
		bgTransferEnabled = true;

		for (int q = 0; q < (int)device->uploadQueues.size(); q++) {
			if (q > 0 && q > vk_max_transfer_threads) break;	// Cap the number of threads used based on user preference
			std::unique_ptr<VkCommandBufferManager> cmds(new VkCommandBufferManager(this, &device->uploadQueues[q].queue, device->uploadQueues[q].queueFamily, true));
			std::unique_ptr<VkTexLoadThread> ptr(new VkTexLoadThread(cmds.get(), device, q, &primaryTexQueue, &secondaryTexQueue, &outputTexQueue));
			ptr->start();
			mBGTransferCommands.push_back(std::move(cmds));
			bgTransferThreads.push_back(std::move(ptr));
		}
	}
	else {
		bgTransferEnabled = false;
	}
}

void VulkanFrameBuffer::Update()
{
	twoD.Reset();
	Flush3D.Reset();

	Flush3D.Clock();

	GetPostprocess()->SetActiveRenderTarget();

	Draw2D();
	twod->Clear();

	mRenderState->EndRenderPass();
	mRenderState->EndFrame();

	Flush3D.Unclock();

	mCommands->WaitForCommands(true);
	mCommands->UpdateGpuStats();

	Super::Update();
}

bool VulkanFrameBuffer::CompileNextShader()
{
	return mShaderManager->CompileNextShader();
}

void VulkanFrameBuffer::RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc)
{
	auto BaseLayer = static_cast<VkHardwareTexture*>(tex->GetHardwareTexture(0, 0));

	VkTextureImage *image = BaseLayer->GetImage(tex, 0, 0);
	VkTextureImage *depthStencil = BaseLayer->GetDepthStencil(tex);

	mRenderState->EndRenderPass();

	VkImageTransition()
		.AddImage(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, true)
		.Execute(mCommands->GetDrawCommands());

	mRenderState->SetRenderTarget(image, depthStencil->View.get(), image->Image->width, image->Image->height, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT);

	IntRect bounds;
	bounds.left = bounds.top = 0;
	bounds.width = min(tex->GetWidth(), image->Image->width);
	bounds.height = min(tex->GetHeight(), image->Image->height);

	renderFunc(bounds);

	mRenderState->EndRenderPass();

	VkImageTransition()
		.AddImage(image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false)
		.Execute(mCommands->GetDrawCommands());

	mRenderState->SetRenderTarget(&GetBuffers()->SceneColor, GetBuffers()->SceneDepthStencil.View.get(), GetBuffers()->GetWidth(), GetBuffers()->GetHeight(), VK_FORMAT_R16G16B16A16_SFLOAT, GetBuffers()->GetSceneSamples());

	tex->SetUpdated(true);
}

void VulkanFrameBuffer::PostProcessScene(bool swscene, int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D)
{
	if (!swscene) mPostprocess->BlitSceneToPostprocess(); // Copy the resulting scene to the current post process texture
	mPostprocess->PostProcessScene(fixedcm, flash, afterBloomDrawEndScene2D);
}

const char* VulkanFrameBuffer::DeviceName() const
{
	const auto &props = device->PhysicalDevice.Properties;
	return props.deviceName;
}

void VulkanFrameBuffer::SetVSync(bool vsync)
{
	Printf("Vsync changed to: %d\n", vsync);
	mVSync = vsync;
}


void VulkanFrameBuffer::PrecacheMaterial(FMaterial *mat, int translation)
{
	if (mat->Source()->GetUseType() == ETextureType::SWCanvas) return;

	MaterialLayerInfo* layer;

	auto systex = static_cast<VkHardwareTexture*>(mat->GetLayer(0, translation, &layer));
	systex->GetImage(layer->layerTexture, translation, layer->scaleFlags);

	int numLayers = mat->NumLayers();
	for (int i = 1; i < numLayers; i++)
	{
		auto syslayer = static_cast<VkHardwareTexture*>(mat->GetLayer(i, 0, &layer));
		syslayer->GetImage(layer->layerTexture, 0, layer->scaleFlags);
	}
}

void VulkanFrameBuffer::PrequeueMaterial(FMaterial *mat, int translation) 
{
	BackgroundCacheMaterial(mat, translation, true, true);	
}


// @Cockatrice - Cache a texture material, intended for use outside of the main thread
bool VulkanFrameBuffer::BackgroundCacheTextureMaterial(FGameTexture *tex, int translation, int scaleFlags, bool makeSPI) {
	if (!tex || !tex->isValid() || tex->GetID().GetIndex() == 0) {

		return false;
	}

	QueuedPatch qp = {
		tex, translation, scaleFlags, makeSPI
	};

	patchQueue.queue(qp);

	return true;
}


// @Cockatrice - Submit each texture in the material to the background loader
// Call from main thread only
bool VulkanFrameBuffer::BackgroundCacheMaterial(FMaterial *mat, int translation, bool makeSPI, bool secondary) {
	if (mat->Source()->GetUseType() == ETextureType::SWCanvas) {
		return false;
	}

	MaterialLayerInfo* layer;

	auto systex = static_cast<VkHardwareTexture*>(mat->GetLayer(0, translation, &layer));
	auto remap = translation <= 0 || IsLuminosityTranslation(translation) ? nullptr : GPalette.TranslationToTable(translation);
	if (remap && remap->Inactive) remap = nullptr;

	// Submit each layer to the background loader
	int lump = layer->layerTexture->GetSourceLump();
	FResourceLump *rLump = lump >= 0 ? fileSystem.GetFileAt(lump) : nullptr;
	FImageLoadParams *params;
	VkTexLoadSpi spi = {};

	bool shouldExpand = mat->sourcetex->ShouldExpandSprite() && (layer->scaleFlags & CTF_Expand);
	bool allowMips = !mat->sourcetex->GetNoMipmaps();

	// If the texture is already submitted to the cache, find it and move it to the normal queue to reprioritize it
	if (rLump && !secondary && systex->GetState() == IHardwareTexture::HardwareState::CACHING) {
		// Move from secondary queue to primary
		VkTexLoadIn in;
		if (secondaryTexQueue.dequeueSearch(in, systex,
			[](void* a, VkTexLoadIn& b)
		{ return (VkHardwareTexture*)a == b.tex; })) {
			systex->SetHardwareState(IHardwareTexture::HardwareState::LOADING, 0);
			primaryTexQueue.queue(in);
			return true;
		}
	} else if (rLump && systex->GetState() == IHardwareTexture::HardwareState::NONE) {
		systex->SetHardwareState(secondary ? IHardwareTexture::HardwareState::CACHING : IHardwareTexture::HardwareState::LOADING);

		FImageTexture *fLayerTexture = dynamic_cast<FImageTexture*>(layer->layerTexture);
		params = layer->layerTexture->GetImage()->NewLoaderParams(
			fLayerTexture ? (fLayerTexture->GetNoRemap0() ? FImageSource::noremap0 : FImageSource::normal) : FImageSource::normal,
			translation,
			remap
		);

		if(params) {
			// Only generate SPI if it's not already there, but we need the other flags for sizing
			spi.generateSpi = makeSPI && !mat->sourcetex->HasSpritePositioning();
			spi.notrimming = mat->sourcetex->GetNoTrimming();
			spi.shouldExpand = shouldExpand;

			VkTexLoadIn in = {
				layer->layerTexture->GetImage(),
				params,
				spi,
				systex,
				mat->sourcetex,
				allowMips
			};

			if (secondary) secondaryTexQueue.queue(in);
			else primaryTexQueue.queue(in);
		}
		else {
			systex->SetHardwareState(IHardwareTexture::HardwareState::READY); // TODO: Set state to a special "unloadable" state
			Printf(TEXTCOLOR_RED"Error submitting texture [%s] to background loader.", rLump->getName());
			return false;
		}
	}
	

	const int numLayers = mat->NumLayers();
	for (int i = 1; i < numLayers; i++)
	{
		auto syslayer = static_cast<VkHardwareTexture*>(mat->GetLayer(i, 0, &layer));
		lump = layer->layerTexture->GetSourceLump();
		rLump = lump >= 0 ? fileSystem.GetFileAt(lump) : nullptr;

		if (rLump && syslayer->GetState() == IHardwareTexture::HardwareState::CACHING) {
			// Move from secondary queue to primary
			VkTexLoadIn in;
			if (secondaryTexQueue.dequeueSearch(in, syslayer,
				[](void* a, VkTexLoadIn& b)
			{ return (VkHardwareTexture*)a == b.tex; })) {
				syslayer->SetHardwareState(IHardwareTexture::HardwareState::LOADING, 0);
				primaryTexQueue.queue(in);
				return true;
			}
		} else if (rLump && syslayer->GetState() == IHardwareTexture::HardwareState::NONE) {
			syslayer->SetHardwareState(secondary ? IHardwareTexture::HardwareState::CACHING : IHardwareTexture::HardwareState::LOADING);
			
			FImageTexture *fLayerTexture = dynamic_cast<FImageTexture*>(layer->layerTexture);
			params = layer->layerTexture->GetImage()->NewLoaderParams(
				fLayerTexture ? (fLayerTexture->GetNoRemap0() ? FImageSource::noremap0 : FImageSource::normal) : FImageSource::normal,
				0, // translation
				nullptr// remap
			);
			
			if (params) {
				VkTexLoadIn in = {
					layer->layerTexture->GetImage(),
					params,
					{
						false, shouldExpand, true
					},
					syslayer,
					nullptr,
					allowMips
				};

				if (secondary) secondaryTexQueue.queue(in);
				else primaryTexQueue.queue(in);
			}
			else {
				syslayer->SetHardwareState(IHardwareTexture::HardwareState::READY); // TODO: Set state to a special "unloadable" state
				Printf(TEXTCOLOR_RED"Error submitting texture [%s] to background loader.", rLump->getName());
			}
		}
	}

	statMaxQueued = max(statMaxQueued, primaryTexQueue.size());
	statMaxQueuedSecondary = max(statMaxQueuedSecondary, secondaryTexQueue.size());

	return true;
}

IHardwareTexture *VulkanFrameBuffer::CreateHardwareTexture(int numchannels)
{
	return new VkHardwareTexture(this, numchannels);
}

FMaterial* VulkanFrameBuffer::CreateMaterial(FGameTexture* tex, int scaleflags)
{
	return new VkMaterial(this, tex, scaleflags);
}

IVertexBuffer *VulkanFrameBuffer::CreateVertexBuffer()
{
	return GetBufferManager()->CreateVertexBuffer();
}

IIndexBuffer *VulkanFrameBuffer::CreateIndexBuffer()
{
	return GetBufferManager()->CreateIndexBuffer();
}

IDataBuffer *VulkanFrameBuffer::CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize)
{
	return GetBufferManager()->CreateDataBuffer(bindingpoint, ssbo, needsresize);
}

void VulkanFrameBuffer::SetTextureFilterMode()
{
	if (mSamplerManager)
	{
		mDescriptorSetManager->ResetHWTextureSets();
		mSamplerManager->ResetHWSamplers();
	}
}

void VulkanFrameBuffer::StartPrecaching()
{
	// Destroy the texture descriptors to avoid problems with potentially stale textures.
	mDescriptorSetManager->ResetHWTextureSets();
}

void VulkanFrameBuffer::BlurScene(float amount, bool force)
{
	if (mPostprocess)
		mPostprocess->BlurScene(amount, force);
}

void VulkanFrameBuffer::UpdatePalette()
{
	if (mPostprocess)
		mPostprocess->ClearTonemapPalette();
}

FTexture *VulkanFrameBuffer::WipeStartScreen()
{
	SetViewportRects(nullptr);

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<VkHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeStartScreen");

	return tex;
}

FTexture *VulkanFrameBuffer::WipeEndScreen()
{
	GetPostprocess()->SetActiveRenderTarget();
	Draw2D();
	twod->Clear();

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<VkHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeEndScreen");

	return tex;
}

void VulkanFrameBuffer::CopyScreenToBuffer(int w, int h, uint8_t *data)
{
	VkTextureImage image;

	// Convert from rgba16f to rgba8 using the GPU:
	image.Image = ImageBuilder()
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.Size(w, h)
		.DebugName("CopyScreenToBuffer")
		.Create(device);

	GetPostprocess()->BlitCurrentToImage(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Staging buffer for download
	auto staging = BufferBuilder()
		.Size(w * h * 4)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.DebugName("CopyScreenToBuffer")
		.Create(device);

	// Copy from image to buffer
	VkBufferImageCopy region = {};
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	region.imageExtent.depth = 1;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	mCommands->GetDrawCommands()->copyImageToBuffer(image.Image->image, image.Layout, staging->buffer, 1, &region);

	// Submit command buffers and wait for device to finish the work
	mCommands->WaitForCommands(false);

	// Map and convert from rgba8 to rgb8
	uint8_t *dest = (uint8_t*)data;
	uint8_t *pixels = (uint8_t*)staging->Map(0, w * h * 4);
	int dindex = 0;
	for (int y = 0; y < h; y++)
	{
		int sindex = (h - y - 1) * w * 4;
		for (int x = 0; x < w; x++)
		{
			dest[dindex] = pixels[sindex];
			dest[dindex + 1] = pixels[sindex + 1];
			dest[dindex + 2] = pixels[sindex + 2];
			dindex += 3;
			sindex += 4;
		}
	}
	staging->Unmap();
}

void VulkanFrameBuffer::SetActiveRenderTarget()
{
	mPostprocess->SetActiveRenderTarget();
}

TArray<uint8_t> VulkanFrameBuffer::GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma)
{
	int w = SCREENWIDTH;
	int h = SCREENHEIGHT;

	IntRect box;
	box.left = 0;
	box.top = 0;
	box.width = w;
	box.height = h;
	mPostprocess->DrawPresentTexture(box, true, true);

	TArray<uint8_t> ScreenshotBuffer(w * h * 3, true);
	CopyScreenToBuffer(w, h, ScreenshotBuffer.Data());

	pitch = w * 3;
	color_type = SS_RGB;
	gamma = 1.0f;
	return ScreenshotBuffer;
}

void VulkanFrameBuffer::BeginFrame()
{
	SetViewportRects(nullptr);
	
	UpdateBackgroundCache();

	mCommands->BeginFrame();
	mTextureManager->BeginFrame();
	mScreenBuffers->BeginFrame(screen->mScreenViewport.width, screen->mScreenViewport.height, screen->mSceneViewport.width, screen->mSceneViewport.height);
	mSaveBuffers->BeginFrame(SAVEPICWIDTH, SAVEPICHEIGHT, SAVEPICWIDTH, SAVEPICHEIGHT);
	mRenderState->BeginFrame();
	mDescriptorSetManager->BeginFrame();
}

void VulkanFrameBuffer::InitLightmap(int LMTextureSize, int LMTextureCount, TArray<uint16_t>& LMTextureData)
{
	if (LMTextureData.Size() > 0)
	{
		GetTextureManager()->SetLightmap(LMTextureSize, LMTextureCount, LMTextureData);
		LMTextureData.Reset(); // We no longer need this, release the memory
	}
}

void VulkanFrameBuffer::Draw2D()
{
	::Draw2D(twod, *mRenderState);
}

void VulkanFrameBuffer::WaitForCommands(bool finish)
{
	mCommands->WaitForCommands(finish);
}

unsigned int VulkanFrameBuffer::GetLightBufferBlockSize() const
{
	return mLights->GetBlockSize();
}

void VulkanFrameBuffer::PrintStartupLog()
{
	const auto &props = device->PhysicalDevice.Properties;

	FString deviceType;
	switch (props.deviceType)
	{
	case VK_PHYSICAL_DEVICE_TYPE_OTHER: deviceType = "other"; break;
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: deviceType = "integrated gpu"; break;
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: deviceType = "discrete gpu"; break;
	case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: deviceType = "virtual gpu"; break;
	case VK_PHYSICAL_DEVICE_TYPE_CPU: deviceType = "cpu"; break;
	default: deviceType.Format("%d", (int)props.deviceType); break;
	}

	FString apiVersion, driverVersion;
	apiVersion.Format("%d.%d.%d", VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
	driverVersion.Format("%d.%d.%d", VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion));

	Printf("Vulkan device: " TEXTCOLOR_ORANGE "%s\n", props.deviceName);
	Printf("Vulkan device type: %s\n", deviceType.GetChars());
	Printf("Vulkan version: %s (api) %s (driver)\n", apiVersion.GetChars(), driverVersion.GetChars());

	Printf(PRINT_LOG, "Vulkan extensions:");
	for (const VkExtensionProperties &p : device->PhysicalDevice.Extensions)
	{
		Printf(PRINT_LOG, " %s", p.extensionName);
	}
	Printf(PRINT_LOG, "\n");

	const auto &limits = props.limits;
	Printf("Max. texture size: %d\n", limits.maxImageDimension2D);
	Printf("Max. uniform buffer range: %d\n", limits.maxUniformBufferRange);
	Printf("Min. uniform buffer offset alignment: %" PRIu64 "\n", limits.minUniformBufferOffsetAlignment);
	Printf("Graphics Queue Family: #%d\nPresent Queue Family:  #%d\nUpload Queue Family:   #%d\nUpload Queue Supports Graphics: %s\n", device->graphicsFamily, device->presentFamily, device->uploadFamily, device->uploadFamilySupportsGraphics ? "Yes" : "No");
}

void VulkanFrameBuffer::SetLevelMesh(hwrenderer::LevelMesh* mesh)
{
	mRaytrace->SetLevelMesh(mesh);
}

void VulkanFrameBuffer::UpdateShadowMap()
{
	mPostprocess->UpdateShadowMap();
}

void VulkanFrameBuffer::SetSaveBuffers(bool yes)
{
	if (yes) mActiveRenderBuffers = mSaveBuffers.get();
	else mActiveRenderBuffers = mScreenBuffers.get();
}

void VulkanFrameBuffer::ImageTransitionScene(bool unknown)
{
	mPostprocess->ImageTransitionScene(unknown);
}

FRenderState* VulkanFrameBuffer::RenderState()
{
	return mRenderState.get();
}

void VulkanFrameBuffer::AmbientOccludeScene(float m5)
{
	mPostprocess->AmbientOccludeScene(m5);
}

void VulkanFrameBuffer::SetSceneRenderTarget(bool useSSAO)
{
	mRenderState->SetRenderTarget(&GetBuffers()->SceneColor, GetBuffers()->SceneDepthStencil.View.get(), GetBuffers()->GetWidth(), GetBuffers()->GetHeight(), VK_FORMAT_R16G16B16A16_SFLOAT, GetBuffers()->GetSceneSamples());
}

bool VulkanFrameBuffer::RaytracingEnabled()
{
	return vk_raytrace && device->SupportsDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
}
