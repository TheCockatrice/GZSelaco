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

#include <zvulkan/vulkanobjects.h>

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
#include "hw_bonebuffer.h"

#include "vk_renderdevice.h"
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
#include "vulkan/textures/vk_framebuffer.h"
#include <zvulkan/vulkanswapchain.h>
#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkansurface.h>
#include <zvulkan/vulkancompatibledevice.h>
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/system/vk_buffer.h"
#include "engineerrors.h"
#include "c_dispatch.h"
#include "image.h"
#include "model.h"
#include "vm.h"


FString JitCaptureStackTrace(int framesToSkip, bool includeNativeFrames, int maxFrames = -1);

EXTERN_CVAR(Int, gl_tonemap)
EXTERN_CVAR(Int, screenblocks)
EXTERN_CVAR(Bool, cl_capfps)
EXTERN_CVAR(Int, vk_max_transfer_threads)
EXTERN_CVAR(Bool, gl_texture_thread)
EXTERN_CVAR(Int, gl_background_flush_count)
EXTERN_CVAR(Bool, gl_texture_thread_upload)

CVAR(Bool, vk_raytrace, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

// Physical device info
static std::vector<VulkanCompatibleDevice> SupportedDevices;
int vkversion;

CUSTOM_CVAR(Bool, vk_debug, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CVAR(Bool, vk_debug_callstack, true, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

CUSTOM_CVAR(Int, vk_device, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CUSTOM_CVAR(Int, vk_max_transfer_threads, 2, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0) self = 0;
	else if (self > 8) self = 8;

	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CUSTOM_CVAR(Int, vk_present_type, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self < 0) self = 0;
	else if (self > 1) self = 1;

	Printf("This won't take effect until " GAMENAME " is restarted.\n");
}

CCMD(vk_listdevices)
{
	for (size_t i = 0; i < SupportedDevices.size(); i++)
	{
		Printf("#%d - %s\n", (int)i, SupportedDevices[i].Device->Properties.Properties.deviceName);
	}
}


DEFINE_ACTION_FUNCTION(_Screen, GetDeviceList)
{
	PARAM_PROLOGUE;
	PARAM_POINTER(lst, TArray<FString>);

	lst->clear();

	for (size_t i = 0; i < SupportedDevices.size(); i++) {
		lst->Push(SupportedDevices[i].Device->Properties.Properties.deviceName);
	}

	ACTION_RETURN_INT(lst->size());
}


void VulkanError(const char* text)
{
	throw CVulkanError(text);
}

void VulkanPrintLog(const char* typestr, const std::string& msg)
{
	bool showcallstack = strstr(typestr, "error") != nullptr;

	if (showcallstack)
		Printf("\n");

	Printf(TEXTCOLOR_RED "[%s] ", typestr);
	Printf(TEXTCOLOR_WHITE "%s\n", msg.c_str());

	if (vk_debug_callstack && showcallstack)
	{
		FString callstack = JitCaptureStackTrace(0, true, 12);
		if (!callstack.IsEmpty())
			Printf("%s\n", callstack.GetChars());
	}
}




ADD_STAT(vkloader)
{
	static int maxQueue = 0, maxSecondaryQueue = 0, queue, secQueue, total, collisions, outSize, models;
	static double minLoad = 0, maxLoad = 0, avgLoad = 0;
	static double minFG = 0, maxFG = 0, avgFG = 0;

	auto sc = dynamic_cast<VulkanRenderDevice *>(screen);

	if (sc) {
		if (!sc->SupportsBackgroundCache()) {
			return FString("Vulkan Texture Thread Disabled");
		}
		sc->GetBGQueueSize(queue, secQueue, collisions, maxQueue, maxSecondaryQueue, total, outSize, models);
		sc->GetBGStats(minLoad, maxLoad, avgLoad);
		sc->GetBGStats2(minFG, maxFG, avgFG);

		static VmaBudget budgets[10] = {};
		VkDeviceSize a = 0, b = 0;

		vmaGetHeapBudgets(sc->device->allocator, budgets);
		for (auto& bud : budgets) {
			a += bud.usage;
			b += bud.budget;
		}

		FString out = "";
		out.AppendFormat(
			"VRAM: %ldMB / %ldMB\n"
			"[%d Threads] Queued: %3.3d - %3.3d Out: %3.3d  Col: %d\nMax: %3.3d Max Sec: %3.3d Tot: %d\n"
			"Models: %d\n"
			"Min: %.3fms  FG: %.3fms\n"
			"Max: %.3fms  FG: %.3fms\n"
			"Avg: %.3fms  FG: %.3fms\n",
			a / 1024 / 1024, b / 1024 / 1024, sc->GetNumThreads(), queue, secQueue, outSize, collisions, maxQueue, maxSecondaryQueue, total, models, minLoad, minFG, maxLoad, maxFG, avgLoad, avgFG
		);
		return out;
	}
	
	return "No Vulkan Device";
}

CCMD(vk_rstbgstats) {
	auto sc = dynamic_cast<VulkanRenderDevice*>(screen);
	if (sc) sc->ResetBGStats();
}


void VulkanRenderDevice::GetBGQueueSize(int& current, int& secCurrent, int& collisions, int& max, int& maxSec, int& total, int& outSize, int& models) {
	max = maxSec = total = 0;
	current = primaryTexQueue.size();
	secCurrent = secondaryTexQueue.size();
	max = statMaxQueued;
	maxSec = statMaxQueuedSecondary;
	collisions = statCollisions;
	outSize = outputTexQueue.size() + bgtUploads.size();
	models = statModelsLoaded;

	for (auto& tfr : bgTransferThreads) {
		total += tfr->statTotalLoaded();
	}
}



void VulkanRenderDevice::GetBGStats(double &min, double &max, double &avg) {
	min = 99999998;
	max = avg = 0;

	for (auto& tfr : bgTransferThreads) {
		min = std::min(tfr->statMinLoadTime(), min);
		max = std::max(tfr->statMaxLoadTime(), max);
		avg += tfr->statAvgLoadTime();
	}

	avg /= (double)bgTransferThreads.size();
}


void VulkanRenderDevice::GetBGStats2(double& min, double& max, double& avg) {
	min = 99999998;
	max = avg = 0;

	min = fgMin;
	max = fgMax;
	avg = fgTotalTime / fgTotalCount;
}



void VulkanRenderDevice::ResetBGStats() {
	statMaxQueued = statMaxQueuedSecondary = 0;
	for (auto& tfr : bgTransferThreads) tfr->resetStats();
	statCollisions = 0;
	fgTotalTime = fgTotalCount = fgMin = fgMax = fgCurTime = 0;
	statModelsLoaded = 0;
}

bool VulkanRenderDevice::CachingActive() {
	return secondaryTexQueue.size() > 0;
}

// TODO: Change this to report the actual progress once we have a way to mark the total number of objects to load
float VulkanRenderDevice::CacheProgress() {
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

// Return the number of used mip levels
static int TempUploadTexture(VkCommandBufferManager *cmd, VkHardwareTexture *tex, VkFormat fmt, int buffWidth, int buffHeight, unsigned char *pixelData, size_t pixelDataSize, size_t totalSize, bool mipmap = true, bool gpuOnly = false, bool indexed = false, bool allowQualityReduction = false, bool allowFinalize = false) {
	if (gpuOnly) {
		uint32_t numMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(buffWidth, buffHeight)))) + 1;
		uint32_t mipWidth = buffWidth, mipHeight = buffHeight;
		size_t mipSize = pixelDataSize, dataPos = 0;
		const int startMip = allowQualityReduction ? min((int)gl_texture_quality, (int)numMipLevels - 1) : 0;
		int mipCnt = 0, maxMips = mipmap ? numMipLevels - startMip : 1;
		uint32_t blockSize = fmt == VK_FORMAT_BC1_RGB_UNORM_BLOCK ? 8 : 16;
		int totalMipLevels = 0;

		assert(indexed == false);

		for (int x = 0; x < (int)numMipLevels && mipCnt < maxMips; x++) {
			if (mipSize == 0 || totalSize - dataPos < mipSize)
				break;

			if (x >= startMip) {
				mipCnt++;
				if (x == startMip) {
					// Base texture
					tex->BackgroundCreateTexture(cmd, mipWidth, mipHeight, 0, fmt, pixelData + dataPos, std::min(maxMips, (int)numMipLevels - startMip), false, mipSize);
				}
				else {
					// Mip
					tex->BackgroundCreateTextureMipMap(cmd, x - startMip, mipWidth, mipHeight, 0, fmt, pixelData + dataPos, mipSize);
				}
				totalMipLevels++;
			}
				
			dataPos += mipSize;

			mipWidth = std::max(1u, (mipWidth >> 1));
			mipHeight = std::max(1u, (mipHeight >> 1));
			mipSize = (size_t)std::max(1u, ((mipWidth + 3) / 4)) * std::max(1u, ((mipHeight + 3) / 4)) * blockSize;
		}

		// Make sure the image is in the correct layout
		if (allowFinalize) tex->CheckFinalTransition(cmd->GetTransferCommands(), true);
		return totalMipLevels;
	}
	else {
		int numMipLevels = VkHardwareTexture::GetMipLevels(buffWidth, buffHeight);
		tex->BackgroundCreateTexture(cmd, buffWidth, buffHeight, indexed ? 1 : 4, fmt, pixelData, mipmap ? -1 : 0, mipmap, (int)pixelDataSize);
		if (allowFinalize) tex->CheckFinalTransition(cmd->GetTransferCommands(), true);
		return numMipLevels;
	}
}

// @Cockatrice - TODO: Separate 
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
	output.flags = input.flags;

	auto *src = input.imgSource;
	bool gpu = src->IsGPUOnly();
	int exx = input.spi.shouldExpand && !gpu;
	int srcWidth = src->GetWidth();
	int srcHeight = src->GetHeight();
	int buffWidth = src->GetWidth() + 2 * exx;
	int buffHeight = src->GetHeight() + 2 * exx;
	bool indexed = false;	// TODO: Determine this properly
	bool allowMips = (input.flags & TEXLOAD_ALLOWMIPS);
	bool mipmap = !indexed && allowMips;
	VkFormat fmt = indexed ? VK_FORMAT_R8_UNORM : VK_FORMAT_B8G8R8A8_UNORM;
	VulkanDevice* device = cmd != nullptr ? cmd->GetRenderDevice()->device.get() : nullptr;

	unsigned char* pixelData = nullptr;
	size_t pixelDataSize = 0;

	if (exx && !gpu) {
		// Load a software texture with a border
		pixelDataSize = 4u * (size_t)buffWidth * (size_t)buffHeight;
		pixelData = (unsigned char*)malloc(pixelDataSize);
		memset(pixelData, 0, pixelDataSize);

		FBitmap pixels(pixelData, buffWidth * 4, buffWidth, buffHeight);

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

		output.totalDataSize = pixelDataSize;
	}
	else {
		if (gpu) {
			// GPU only textures cannot be trimmed or translated, so just do a straight read
			size_t totalSize;
			int numMipLevels;

			FileReader reader;

			assert(params->lump > 0);
			try { reader = fileSystem.OpenFileReader(params->lump, FileSys::EReaderType::READER_NEW, 0); }
			catch (std::exception& e) {
				output.error = VK_TEXLOAD_ERR_FILE;
				return true;
			}

			// Verify file reader
			if (!reader.isOpen() || reader.GetLength() <= 0) {
				output.error = VK_TEXLOAD_ERR_FILE;
				return true;
			}
			
			output.isTranslucent = src->ReadCompressedPixels(&reader, &pixelData, totalSize, pixelDataSize, numMipLevels);
			reader.Close();
			
			// Verify pixel data
			if (pixelData == nullptr) {
				output.error = VK_TEXLOAD_ERR_FORMAT;
				return true;
			}
			
			mipmap = false;
			fmt = (VkFormat)src->getVKFormat(); //VK_FORMAT_BC7_UNORM_BLOCK;

			output.totalDataSize = totalSize;

			uint32_t expectedMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(buffWidth, buffHeight)))) + 1;

			// Only perform upload if we have a command buffer
			if (cmd) {
				mipmap = false;	// Don't generate mipmaps past this point
				output.mipmapCount = TempUploadTexture(cmd, output.tex, fmt, buffWidth, buffHeight, pixelData, pixelDataSize, totalSize, allowMips && numMipLevels == (int)expectedMipLevels && numMipLevels > 0, true, indexed, input.flags & TEXLOAD_ALLOWQUALITY, uploadQueue.familySupportsGraphics);
			
				if(output.mipmapCount <= 0 || output.tex->mLoadedImage.get() == nullptr) {
					output.error = VK_TEXLOAD_ERR_UPLOAD;
					return true;
				}
			}
			else {
				mipmap = allowMips && numMipLevels == (int)expectedMipLevels && numMipLevels > 0;	// Upload mipmaps if the science is correct
				output.mipmapCount = -1;
			}

			if (input.spi.generateSpi) {
				// Generate sprite data without pixel data, since no trimming should occur
				FGameTexture::GenerateEmptySpriteData(output.spi.info, input.gtex ? input.gtex->GetTexelWidth() : buffWidth, input.gtex ? input.gtex->GetTexelHeight() : buffHeight);
			}
		}
		else {
			// Load a software texture without a border
			pixelDataSize = 4u * (size_t)buffWidth * (size_t)buffHeight;
			pixelData = (unsigned char*)malloc(pixelDataSize);
			memset(pixelData, 0, pixelDataSize);

			FBitmap pixels(pixelData, buffWidth * 4, buffWidth, buffHeight);

			output.isTranslucent = src->ReadPixels(params, &pixels);
			output.totalDataSize = pixelDataSize;

			if (input.spi.generateSpi) {
				FGameTexture::GenerateInitialSpriteData(output.spi.info, &pixels, input.spi.shouldExpand, input.spi.notrimming);
			}
		}
	}

	delete input.params;

	output.pixelsSize = pixelDataSize;
	output.pixelW = buffWidth;
	output.pixelH = buffHeight;

	// If there is no command buffer we have to do the upload in the main thread
	// Transfer data if necessary
	if (!cmd) {
		output.createMipmaps = mipmap;
		output.pixels = pixelData;
	}
	else {
		output.createMipmaps = mipmap && !uploadQueue.familySupportsGraphics;
		output.pixels = nullptr;

		// Upload non-gpu only textures
		if (!gpu) {
			output.mipmapCount = mipmap ? VkHardwareTexture::GetMipLevels(buffWidth, buffHeight) : 1;
			output.tex->BackgroundCreateTexture(cmd, buffWidth, buffHeight, indexed ? 1 : 4, fmt, pixelData, mipmap ? -1 : 0, mipmap, (int)pixelDataSize);

			if (!output.createMipmaps && uploadQueue.familySupportsGraphics) {
				// We should be in the final state for the texture, ensure it has the correct layout
				// This can only be done on a queue which supports graphics, since the spec seems to state that you can't transition an image to a state which is unsupported by the current queue
				output.mipmapCount = mipmap ? VkHardwareTexture::GetMipLevels(buffWidth, buffHeight) : 1;
				output.tex->CheckFinalTransition(cmd->GetTransferCommands(), true);
			}
		}

		// Wait for operations to finish, since we can't maintain a regular loop of clearing the buffer
		if (cmd && cmd->TransferDeleteList->TotalSize > 1) {
			cmd->WaitForCommands(false, true);
		}

		if (pixelData) {
			free(pixelData);
		}

		// If we created the texture on a different family than the graphics family, we need to release access 
		// to the image on this queue
		if (device && device->GraphicsFamily != uploadQueue.queueFamily) {
			auto cmds = cmd->CreateUnmanagedCommands();
			cmds->SetDebugName("BGThread::QueueMoveCMDS");
			output.releaseSemaphore = new VulkanSemaphore(device);
			output.tex->ReleaseLoadedFromQueue(cmds.get(), uploadQueue.queueFamily, device->GraphicsFamily);
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
	}

	// Always return true, because failed images need to be marked as unloadable
	// TODO: Mark failed images as unloadable so they don't keep coming back to the queue
	return true;
}

void VkTexLoadThread::cancelLoad() { currentImageID.store(0); }
void VkTexLoadThread::completeLoad() { currentImageID.store(0); }


bool VkModelLoadThread::loadResource(VkModelLoadIn& input, VkModelLoadOut& output) {
	FileReader reader = fileSystem.OpenFileReader(input.lump, FileSys::EReaderType::READER_NEW, 0);
	output.data = reader.Read();
	reader.Close();

	output.lump = input.lump;
	output.model = input.model;

	return true;
}


// END Background Loader Stuff =====================================================

void VulkanRenderDevice::FlushBackground() {
	int nq = primaryTexQueue.size() + secondaryTexQueue.size();
	bool active = nq;

	if(!active)
		for (auto& tfr : bgTransferThreads) active = active || tfr->isActive();

	Printf(TEXTCOLOR_GREEN"VulkanFrameBuffer[%s]: Flushing [%d + %d + %d] texture load ops\n", active ? "active" : "inactive", nq, patchQueue.size(), nq + patchQueue.size());
	Printf(TEXTCOLOR_GREEN"\tFlushing %d - %d Model Reads\n", modelInQueue.size(), modelOutQueue.size());

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
		active = active || modelThread->isActive();
	}

	// Finish anything that was loaded
	UpdateBackgroundCache(true);

	Printf(TEXTCOLOR_GREEN "\tFlushing %ld raw texture reads...\n", bgtUploads.size());

	// Lastly finish anything that needs to be uploaded in the main thread
	UploadLoadedTextures(true);

	check.Unclock();
	Printf(TEXTCOLOR_GOLD"\tVulkanFrameBuffer::FlushBackground() took %f ms\n", check.TimeMS());
}

void VulkanRenderDevice::UpdateBackgroundCache(bool flush) {
	// Check for completed cache items and link textures to the data
	VkTexLoadOut loaded;
	bool processed = false;
	cycle_t timer = cycle_t();
	timer.Clock();
	
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
		bool transferOwnership = false;
		bool uploadOnMainThread = false;
		for (int bgIndex = (int)bgTransferThreads.size() - 1; bgIndex >= 0; bgIndex--) {
			if (bgTransferThreads[bgIndex]->getUploadQueue().queueFamily != device->GraphicsFamily && bgTransferThreads[bgIndex]->getUploadQueue().queueIndex >= 0) {
				transferOwnership = true;
			}

			if (bgTransferThreads[bgIndex]->getUploadQueue().queueIndex < 0) {
				uploadOnMainThread = true;
			}
		}

		if (transferOwnership && uploadOnMainThread) {
			VulkanError("Ambiguous situation detected: Ownership and Transfer cannot happen at the same time!\nPlease report to Cockatrice.\n");
		}

		if (!uploadOnMainThread) processed = true;	// For stats

		// TODO: Limit the total amount of commands we are going to send per-frame, or at least per-submit
		std::unique_ptr<VulkanCommandBuffer> cmds = transferOwnership ? mCommands->CreateUnmanagedCommands() : nullptr;
		if (cmds.get()) cmds->SetDebugName("MainThread::QueueMoveCMDS");

		unsigned int sm4StartIndex = (unsigned int)bgtSm4List.size() - 1;

		int dequeueCount = 0;
		size_t uploadSize = 0;
		while ((flush || (dequeueCount < gl_background_flush_count && uploadSize < 20971520L)) && outputTexQueue.dequeue(loaded)) {
			dequeueCount++;

			// Abort and log errors
			if (loaded.error != VK_TEXLOAD_ERR_NONE) {
				const char* name = loaded.gtex ? loaded.gtex->GetName().GetChars() : "[unknown]";
				const char* err;
				switch (loaded.error) {
				case VK_TEXLOAD_ERR_FILE:
					err = "File access Error";
					break;
				case VK_TEXLOAD_ERR_FORMAT:
					err = "Texture format or load error";
					break;
				case VK_TEXLOAD_ERR_UPLOAD:
					err = "Error uploading texture";
					break;
				case VK_TEXLOAD_ERR_MEM:
					err = "Out of memory";
					break;
				default:
					err = "Unknown Error";
					break;
				}

				Printf(TEXTCOLOR_RED "vkRenderDevice: Error loading texture %s (%s)\n", name, err);
				if (loaded.imgSource) {
					Printf(TEXTCOLOR_RED "\t%s\n", fileSystem.GetFileFullPath(loaded.imgSource->LumpNum()).c_str());
				}
			}


			if (loaded.tex->hwState == IHardwareTexture::HardwareState::READY) {
				statCollisions++;

				// Set the sprite positioning if we loaded it and it hasn't already been applied
				if (loaded.spi.generateSpi && loaded.gtex && !loaded.gtex->HasSpritePositioning()) {
					//Printf("Setting SPI even though we rejected the texture.\n");
					SpritePositioningInfo* spi = (SpritePositioningInfo*)ImageArena.Alloc(2 * sizeof(SpritePositioningInfo));
					memcpy(spi, loaded.spi.info, 2 * sizeof(SpritePositioningInfo));
					loaded.gtex->SetSpriteRect(spi);
				}

				if (loaded.pixels) {
					free(loaded.pixels);
				}
				continue;
			}

			// If this image was created in a different queue family, it now needs to be moved over to
			// the graphics queue faimly
			if (transferOwnership && loaded.tex->mLoadedImage) {
				assert(cmds.get());

				loaded.tex->AcquireLoadedFromQueue(cmds.get(), device->UploadFamily, device->GraphicsFamily);

				// If we cannot create mipmaps in the background, tell the GPU to create them now
				if (loaded.createMipmaps) {
					assert(!loaded.imgSource->IsGPUOnly());
					loaded.tex->mLoadedImage.get()->GenerateMipmaps(cmds.get());
				}

				if (loaded.releaseSemaphore) {
					loaded.releaseSemaphore->SetDebugName("BGT::RlsA");
					bgtSm4List.push_back(std::unique_ptr<VulkanSemaphore>(loaded.releaseSemaphore));
				}

				assert(loaded.mipmapCount > 0);
				loaded.tex->CheckFinalTransition(cmds.get(), true);
			}
			else if (uploadOnMainThread && loaded.pixels) {
				bgtUploads.push_back(std::move(loaded));
				continue;
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

			if (!bgtFence.get()) bgtFence.reset(new VulkanFence(device.get()));

			submit.Execute(device.get(), device->GraphicsQueue, bgtFence.get());

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
			BackgroundCacheMaterial(gltex, FTranslationID::fromInt(qp.translation), qp.generateSPI);
		}
	}

	// Process any loaded models
	VkModelLoadOut modelOut;
	while (modelOutQueue.dequeue(modelOut)) {
		assert(modelOut.model);

		if (modelOut.model->GetLoadState() != FModel::LOADING) {
			statCollisions++;
			modelOut.data.clear();
			continue;
		}

		modelOut.model->LoadGeometry(&modelOut.data);
		modelOut.model->SetLoadState(FModel::READY);
		modelOut.data.clear();
		statModelsLoaded++;
	}


	if (!flush && processed) {
		timer.Unclock();
		fgCurTime = timer.TimeMS();

		if (bgtUploads.size() == 0) {
			fgTotalTime += fgCurTime;
			fgTotalCount += 1;
			fgMin = std::min(fgMin, fgCurTime);
			fgMax = std::max(fgMax, fgCurTime);
		}
	}
	else {
		fgCurTime = 0;
	}
}


void VulkanRenderDevice::UploadLoadedTextures(bool flush) {
	if (bgtUploads.size() == 0) return;

	cycle_t timer = cycle_t();
	timer.Clock();
	size_t bytesUploaded = 0;
	int numLoaded = 0;

	for (auto& loaded : bgtUploads) {
		if (!flush && bytesUploaded > 20971520) break;	// Limit to ~20mb per call unless flushing

		bool gpuOnly = loaded.imgSource->IsGPUOnly();
		VkFormat fmt = gpuOnly ? (VkFormat)loaded.imgSource->getVKFormat() : VK_FORMAT_B8G8R8A8_UNORM;

		assert(loaded.pixels);

		loaded.mipmapCount = TempUploadTexture(mCommands.get(), loaded.tex, fmt, loaded.pixelW, loaded.pixelH, loaded.pixels, loaded.pixelsSize, loaded.totalDataSize, loaded.createMipmaps, gpuOnly, false, loaded.flags & TEXLOAD_ALLOWQUALITY, false);
		free(loaded.pixels);
		loaded.pixels = 0;

		// Upload would skip the mipmap generation if UploadFamilySupportsGraphics is unset (which it almost always should be)
		// so create manually now
		if (!gpuOnly && loaded.createMipmaps && !device.get()->UploadFamilySupportsGraphics) {
			loaded.tex->mLoadedImage.get()->GenerateMipmaps(mCommands->GetTransferCommands());
			loaded.mipmapCount = VkHardwareTexture::GetMipLevels(loaded.tex->mLoadedImage->Image->width, loaded.tex->mLoadedImage->Image->height);
		}

		loaded.tex->CheckFinalTransition(mCommands->GetTransferCommands(), true);
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

		numLoaded++;
		bytesUploaded += loaded.totalDataSize;
	}

	if (numLoaded > 0) {
		if (numLoaded == (int)bgtUploads.size())
			bgtUploads.clear();
		else if (numLoaded == 1)
			bgtUploads.erase(bgtUploads.begin());
		else
			bgtUploads.erase(bgtUploads.begin(), std::next(bgtUploads.begin(), numLoaded));
	}

	if (!flush) {
		timer.Unclock();
		fgCurTime += timer.TimeMS();
		fgTotalTime += fgCurTime;
		fgTotalCount += 1;
		fgMin = std::min(fgMin, fgCurTime);
		fgMax = std::max(fgMax, fgCurTime);
	}
}



VulkanRenderDevice::VulkanRenderDevice(void *hMonitor, bool fullscreen, std::shared_ptr<VulkanSurface> surface) :
	Super(hMonitor, fullscreen) 
{
	VulkanDeviceBuilder builder;
	builder.OptionalRayQuery();
	builder.Surface(surface);
	builder.SelectDevice(vk_device);
	SupportedDevices = builder.FindDevices(surface->Instance);

	int flags = 0;
	if (vk_present_type == 0) flags |= VK_DEVICE_FLAG_FORCE_EXCLUSIVE_PRESENT;
	device = builder.Create(surface->Instance, gl_texture_thread ? vk_max_transfer_threads : 0, flags);
}

VulkanRenderDevice::~VulkanRenderDevice()
{
	StopBackgroundCache();
	vkDeviceWaitIdle(device->device); // make sure the GPU is no longer using any objects before RAII tears them down

	delete mVertexData;
	delete mSkyData;
	delete mViewpoints;
	delete mLights;
	delete mBones;
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


void VulkanRenderDevice::StopBackgroundCache() {
	FlushBackground();
	primaryTexQueue.clear();
	secondaryTexQueue.clear();
	modelInQueue.clear();

	for (auto& tfr : bgTransferThreads) {
		tfr->stop();
	}

	modelThread->stop();

	modelOutQueue.clear();
	outputTexQueue.clear();
}

void VulkanRenderDevice::InitializeState()
{
	static bool first = true;
	if (first)
	{
		PrintStartupLog();
		first = false;
	}

	// Use the same names here as OpenGL returns.
	switch (device->PhysicalDevice.Properties.Properties.vendorID)
	{
	case 0x1002: vendorstring = "ATI Technologies Inc.";     break;
	case 0x10DE: vendorstring = "NVIDIA Corporation";  break;
	case 0x8086: vendorstring = "Intel";   break;
	default:     vendorstring = "Unknown"; break;
	}

	hwcaps = RFL_SHADER_STORAGE_BUFFER | RFL_BUFFER_STORAGE;
	glslversion = 4.50f;
	uniformblockalignment = (unsigned int)device->PhysicalDevice.Properties.Properties.limits.minUniformBufferOffsetAlignment;
	maxuniformblock = device->PhysicalDevice.Properties.Properties.limits.maxUniformBufferRange;

	mCommands.reset(new VkCommandBufferManager(this, &device->GraphicsQueue, device->GraphicsFamily));

	mSamplerManager.reset(new VkSamplerManager(this));
	mTextureManager.reset(new VkTextureManager(this));
	mFramebufferManager.reset(new VkFramebufferManager(this));
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
	mBones = new BoneBuffer();

	mShaderManager.reset(new VkShaderManager(this));
	mDescriptorSetManager->Init();
#ifdef __APPLE__
	mRenderState.reset(new VkRenderStateMolten(this));
#else
	mRenderState.reset(new VkRenderState(this));
#endif

	// @Cockatrice - Init the background loader
	bgTransferThreads.clear();
	modelThread.reset(new VkModelLoadThread(&modelInQueue, &modelOutQueue));
	
	if (gl_texture_thread && vk_max_transfer_threads >= 0) {
		int numThreads = 1;

		bgTransferEnabled = true;

		if (device->uploadQueues.size() > 0 && gl_texture_thread_upload) {
			// Init upload queues with GPU upload enabled in the thread
			bgUploadEnabled = true;
			numThreads = std::min((int)vk_max_transfer_threads, (int)device->uploadQueues.size());

			for (int q = 0; q < numThreads; q++) {
				std::unique_ptr<VkCommandBufferManager> cmds(new VkCommandBufferManager(this, &device->uploadQueues[q].queue, device->uploadQueues[q].queueFamily, true));
				std::unique_ptr<VkTexLoadThread> ptr(new VkTexLoadThread(cmds.get(), device.get(), q, &primaryTexQueue, &secondaryTexQueue, &outputTexQueue));
				ptr->start();
				mBGTransferCommands.push_back(std::move(cmds));
				bgTransferThreads.push_back(std::move(ptr));
			}
		}
		else {
			// Init queues but only load from disk, upload will have to happen on the main thread
			bgUploadEnabled = false;

			for (int x = 0; x < numThreads; x++) {
				std::unique_ptr<VkTexLoadThread> ptr(new VkTexLoadThread(nullptr, device.get(), -1, &primaryTexQueue, &secondaryTexQueue, &outputTexQueue));
				ptr->start();
				bgTransferThreads.push_back(std::move(ptr));
			}
		}

		modelThread->start();
	}
	else {
		bgUploadEnabled = false;
		bgTransferEnabled = false;
	}
}

void VulkanRenderDevice::Update()
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

void VulkanRenderDevice::OnApplicationActivated(bool active) {
	// Intel ARC drivers do not work properly, we need to signal the swapchain that it has been invalidated
	// @Cockatrice - This fix can be removed, drivers have no problems regarding this any longer
	/*if (device && device->isARC && mFramebufferManager && mFramebufferManager->SwapChain) {
		vkDeviceWaitIdle(device->device);
		mFramebufferManager->SwapChain->SetLost();

		if (active) {
			// Add an artificial delay to allow drivers to synchronize
			// Not ideal, but sometimes quite necessary or we lose the vulkan device
			std::this_thread::sleep_for(std::chrono::milliseconds(300));
		}
	}*/
}

bool VulkanRenderDevice::CanDisplay(bool active) {
	// @Cockatrice - Do not display in the background when fullscreen on ARC. 
	// This almost always results in losing the device due to driver issues
	// @Cockatrice - Disabling this now, as the ARC driver updates seem to have fixed this problem
	return true;// !device->isARC || (active || !IsFullscreen());
}

bool VulkanRenderDevice::CompileNextShader()
{
	return mShaderManager->CompileNextShader();
}

void VulkanRenderDevice::RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc)
{
	auto BaseLayer = static_cast<VkHardwareTexture*>(tex->GetHardwareTexture(0, 0));

	VkTextureImage *image = BaseLayer->GetImage(tex, 0, 0);
	VkTextureImage *depthStencil = BaseLayer->GetDepthStencil(tex);

	mRenderState->EndRenderPass();

	VkImageTransition()
		.AddImage(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, false)
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

void VulkanRenderDevice::PostProcessScene(bool swscene, int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D)
{
	if (!swscene) mPostprocess->BlitSceneToPostprocess(); // Copy the resulting scene to the current post process texture
	mPostprocess->PostProcessScene(fixedcm, flash, afterBloomDrawEndScene2D);
}

const char* VulkanRenderDevice::DeviceName() const
{
	return device->PhysicalDevice.Properties.Properties.deviceName;
}

void VulkanRenderDevice::SetVSync(bool vsync)
{
	Printf("Vsync changed to: %d\n", vsync);
	mVSync = vsync;
}

void VulkanRenderDevice::PrecacheMaterial(FMaterial *mat, int translation)
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

void VulkanRenderDevice::PrequeueMaterial(FMaterial *mat, int translation)
{
	BackgroundCacheMaterial(mat, FTranslationID::fromInt(translation), true, true);
}


bool VulkanRenderDevice::BackgroundLoadModel(FModel* model) {
	if (!model || model->GetLoadState() == FModel::READY || model->GetLumpNum() <= 0)
		return false;
	if (model->GetLoadState() == FModel::LOADING)
		return true;

	model->SetLoadState(FModel::LOADING);

	VkModelLoadIn modelLoad;
	modelLoad.model = model;
	modelLoad.lump = model->GetLumpNum();
	modelInQueue.queue(modelLoad);

	return true;
}


// @Cockatrice - Cache a texture material, intended for use outside of the main thread
bool VulkanRenderDevice::BackgroundCacheTextureMaterial(FGameTexture *tex, FTranslationID translation, int scaleFlags, bool makeSPI) {
	if (!tex || !tex->isValid() || tex->GetID().GetIndex() == 0) {

		return false;
	}

	QueuedPatch qp = {
		tex, translation.index(), scaleFlags, makeSPI
	};

	patchQueue.queue(qp);

	return true;
}


// @Cockatrice - Submit each texture in the material to the background loader
// Call from main thread only
bool VulkanRenderDevice::BackgroundCacheMaterial(FMaterial *mat, FTranslationID translation, bool makeSPI, bool secondary) {
	const auto useType = mat->Source()->GetUseType();

	if (useType == ETextureType::SWCanvas) {
		return false;
	}

	MaterialLayerInfo* layer;

	auto systex = static_cast<VkHardwareTexture*>(mat->GetLayer(0, translation.index(), &layer));
	auto remap = !translation.isvalid() || IsLuminosityTranslation(translation) ? nullptr : GPalette.TranslationToTable(translation);
	if (remap && remap->Inactive) remap = nullptr;

	// Submit each layer to the background loader
	int lump = layer->layerTexture->GetSourceLump();
	bool lumpExists = fileSystem.FileLength(lump) >= 0;
	FImageLoadParams *params;
	VkTexLoadSpi spi = {};

	bool shouldExpand = mat->sourcetex->ShouldExpandSprite() && (layer->scaleFlags & CTF_Expand);
	int8_t flags = 0;

	if (!mat->sourcetex->GetNoMipmaps()) flags |= TEXLOAD_ALLOWMIPS;
	if (layer->scaleFlags & CTF_ReduceQuality) 
		flags |= TEXLOAD_ALLOWQUALITY;

	// If the texture is already submitted to the cache, find it and move it to the normal queue to reprioritize it
	if (lumpExists && !secondary && systex->GetState() == IHardwareTexture::HardwareState::CACHING) {
		// Move from secondary queue to primary
		VkTexLoadIn in;
		if (secondaryTexQueue.dequeueSearch(in, systex,
			[](void* a, VkTexLoadIn& b)
		{ return (VkHardwareTexture*)a == b.tex; })) {
			systex->SetHardwareState(IHardwareTexture::HardwareState::LOADING, 0);
			primaryTexQueue.queue(in);
			return true;
		}
	} else if (lumpExists && systex->GetState() == IHardwareTexture::HardwareState::NONE) {
		systex->SetHardwareState(secondary ? IHardwareTexture::HardwareState::CACHING : IHardwareTexture::HardwareState::LOADING);

		FImageTexture *fLayerTexture = dynamic_cast<FImageTexture*>(layer->layerTexture);
		params = layer->layerTexture->GetImage()->NewLoaderParams(
			fLayerTexture ? (fLayerTexture->GetNoRemap0() ? FImageSource::noremap0 : FImageSource::normal) : FImageSource::normal,
			translation.index(),
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
				flags
			};

			if (secondary) secondaryTexQueue.queue(in);
			else primaryTexQueue.queue(in);
		}
		else {
			systex->SetHardwareState(IHardwareTexture::HardwareState::READY); // TODO: Set state to a special "unloadable" state
			Printf(TEXTCOLOR_RED"Error submitting texture [%s] to background loader.", fileSystem.GetFileFullName(lump, false));
			return false;
		}
	}
	

	const int numLayers = mat->NumLayers();
	for (int i = 1; i < numLayers; i++)
	{
		auto syslayer = static_cast<VkHardwareTexture*>(mat->GetLayer(i, 0, &layer));
		lump = layer->layerTexture->GetSourceLump();
		bool lumpExists = fileSystem.FileLength(lump) >= 0;

		if (lump == 0) 
			continue;
		if (lumpExists && syslayer->GetState() == IHardwareTexture::HardwareState::CACHING) {
			// Move from secondary queue to primary
			VkTexLoadIn in;
			if (secondaryTexQueue.dequeueSearch(in, syslayer,
				[](void* a, VkTexLoadIn& b)
			{ return (VkHardwareTexture*)a == b.tex; })) {
				syslayer->SetHardwareState(IHardwareTexture::HardwareState::LOADING, 0);
				primaryTexQueue.queue(in);
				return true;
			}
		} else if (lumpExists && syslayer->GetState() == IHardwareTexture::HardwareState::NONE) {
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
					flags
				};

				if (secondary) secondaryTexQueue.queue(in);
				else primaryTexQueue.queue(in);
			}
			else {
				syslayer->SetHardwareState(IHardwareTexture::HardwareState::READY); // TODO: Set state to a special "unloadable" state
				Printf(TEXTCOLOR_RED"Error submitting texture [%s] to background loader.", fileSystem.GetFileFullName(lump, false));
			}
		}
	}

	statMaxQueued = max(statMaxQueued, primaryTexQueue.size());
	statMaxQueuedSecondary = max(statMaxQueuedSecondary, secondaryTexQueue.size());

	return true;
}

IHardwareTexture *VulkanRenderDevice::CreateHardwareTexture(int numchannels)
{
	return new VkHardwareTexture(this, numchannels);
}

FMaterial* VulkanRenderDevice::CreateMaterial(FGameTexture* tex, int scaleflags)
{
	return new VkMaterial(this, tex, scaleflags);
}

IVertexBuffer *VulkanRenderDevice::CreateVertexBuffer()
{
	return GetBufferManager()->CreateVertexBuffer();
}

IIndexBuffer *VulkanRenderDevice::CreateIndexBuffer()
{
	return GetBufferManager()->CreateIndexBuffer();
}

IDataBuffer *VulkanRenderDevice::CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize)
{
	return GetBufferManager()->CreateDataBuffer(bindingpoint, ssbo, needsresize);
}

void VulkanRenderDevice::SetTextureFilterMode()
{
	if (mSamplerManager)
	{
		mDescriptorSetManager->ResetHWTextureSets();
		mSamplerManager->ResetHWSamplers();
	}
}

void VulkanRenderDevice::StartPrecaching()
{
	// Destroy the texture descriptors to avoid problems with potentially stale textures.
	mDescriptorSetManager->ResetHWTextureSets();
}

void VulkanRenderDevice::BlurScene(float amount, bool force)
{
	if (mPostprocess)
		mPostprocess->BlurScene(amount, force);
}

void VulkanRenderDevice::UpdatePalette()
{
	if (mPostprocess)
		mPostprocess->ClearTonemapPalette();
}

FTexture *VulkanRenderDevice::WipeStartScreen()
{
	SetViewportRects(nullptr);

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<VkHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeStartScreen");

	return tex;
}

FTexture *VulkanRenderDevice::WipeEndScreen()
{
	GetPostprocess()->SetActiveRenderTarget();
	Draw2D();
	twod->Clear();

	auto tex = new FWrapperTexture(mScreenViewport.width, mScreenViewport.height, 1);
	auto systex = static_cast<VkHardwareTexture*>(tex->GetSystemTexture());

	systex->CreateWipeTexture(mScreenViewport.width, mScreenViewport.height, "WipeEndScreen");

	return tex;
}

void VulkanRenderDevice::CopyScreenToBuffer(int w, int h, uint8_t *data)
{
	VkTextureImage image;

	// Convert from rgba16f to rgba8 using the GPU:
	image.Image = ImageBuilder()
		.Format(VK_FORMAT_R8G8B8A8_UNORM)
		.Usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		.Size(w, h)
		.DebugName("CopyScreenToBuffer")
		.Create(device.get());

	GetPostprocess()->BlitCurrentToImage(&image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Staging buffer for download
	auto staging = BufferBuilder()
		.Size(w * h * 4)
		.Usage(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_TO_CPU)
		.DebugName("CopyScreenToBuffer")
		.Create(device.get());

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

void VulkanRenderDevice::SetActiveRenderTarget()
{
	mPostprocess->SetActiveRenderTarget();
}

TArray<uint8_t> VulkanRenderDevice::GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma)
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

void VulkanRenderDevice::BeginFrame()
{
	SetViewportRects(nullptr);
	mViewpoints->Clear();

	UpdateBackgroundCache();

	mCommands->BeginFrame();
	mTextureManager->BeginFrame();
	mScreenBuffers->BeginFrame(screen->mScreenViewport.width, screen->mScreenViewport.height, screen->mSceneViewport.width, screen->mSceneViewport.height);
	mSaveBuffers->BeginFrame(SAVEPICWIDTH, SAVEPICHEIGHT, SAVEPICWIDTH, SAVEPICHEIGHT);
	mRenderState->BeginFrame();
	mDescriptorSetManager->BeginFrame();

	// Upload textures loaded externally but cannot be uploaded in a thread
	UploadLoadedTextures();
}

void VulkanRenderDevice::InitLightmap(int LMTextureSize, int LMTextureCount, TArray<uint16_t>& LMTextureData)
{
	if (LMTextureData.Size() > 0)
	{
		GetTextureManager()->SetLightmap(LMTextureSize, LMTextureCount, LMTextureData);
		LMTextureData.Reset(); // We no longer need this, release the memory
	}
}

void VulkanRenderDevice::Draw2D()
{
	::Draw2D(twod, *mRenderState);
}

void VulkanRenderDevice::WaitForCommands(bool finish)
{
	mCommands->WaitForCommands(finish);
}

unsigned int VulkanRenderDevice::GetLightBufferBlockSize() const
{
	return mLights->GetBlockSize();
}

void VulkanRenderDevice::PrintStartupLog()
{
	const auto &props = device->PhysicalDevice.Properties.Properties;

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
	vkversion = VK_API_VERSION_MAJOR(props.apiVersion) * 100 + VK_API_VERSION_MINOR(props.apiVersion);

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

	Printf("Graphics Queue Family: #%d\nPresent Queue Family:  #%d\nUpload Queue Family:   #%d\nUpload Queue Supports Graphics: %s\n", device->GraphicsFamily, device->PresentFamily, device->UploadFamily, device->UploadFamilySupportsGraphics ? "Yes" : "No");
}

void VulkanRenderDevice::SetLevelMesh(hwrenderer::LevelMesh* mesh)
{
	mRaytrace->SetLevelMesh(mesh);
}

void VulkanRenderDevice::UpdateShadowMap()
{
	mPostprocess->UpdateShadowMap();
}

void VulkanRenderDevice::SetSaveBuffers(bool yes)
{
	if (yes) mActiveRenderBuffers = mSaveBuffers.get();
	else mActiveRenderBuffers = mScreenBuffers.get();
}

void VulkanRenderDevice::ImageTransitionScene(bool unknown)
{
	mPostprocess->ImageTransitionScene(unknown);
}

FRenderState* VulkanRenderDevice::RenderState()
{
	return mRenderState.get();
}

void VulkanRenderDevice::AmbientOccludeScene(float m5)
{
	mPostprocess->AmbientOccludeScene(m5);
}

void VulkanRenderDevice::SetSceneRenderTarget(bool useSSAO)
{
	mRenderState->SetRenderTarget(&GetBuffers()->SceneColor, GetBuffers()->SceneDepthStencil.View.get(), GetBuffers()->GetWidth(), GetBuffers()->GetHeight(), VK_FORMAT_R16G16B16A16_SFLOAT, GetBuffers()->GetSceneSamples());
}

bool VulkanRenderDevice::RaytracingEnabled()
{
	return vk_raytrace && device->SupportsExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME);
}
