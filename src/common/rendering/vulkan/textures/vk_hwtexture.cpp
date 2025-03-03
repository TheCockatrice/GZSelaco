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


#include "c_cvars.h"
#include "hw_material.h"
#include "hw_cvars.h"
#include "hw_renderstate.h"
#include "filesystem.h"
#include "vulkan/system/vk_objects.h"
#include "vulkan/system/vk_builders.h"
#include "vulkan/system/vk_framebuffer.h"
#include "vulkan/system/vk_commandbuffer.h"
#include "vulkan/textures/vk_samplers.h"
#include "vulkan/textures/vk_renderbuffers.h"
#include "vulkan/textures/vk_texture.h"
#include "vulkan/renderer/vk_descriptorset.h"
#include "vulkan/renderer/vk_postprocess.h"
#include "vulkan/shaders/vk_shader.h"
#include "vk_hwtexture.h"
#include "g_levellocals.h"

VkHardwareTexture::VkHardwareTexture(VulkanFrameBuffer* fb, int numchannels) : fb(fb)
{
	mTexelsize = numchannels;

	mImage.reset(new VkTextureImage());
	mDepthStencil.reset(new VkTextureImage());

	fb->GetTextureManager()->AddTexture(this);
}

VkHardwareTexture::~VkHardwareTexture()
{
	if (fb)
		fb->GetTextureManager()->RemoveTexture(this);
}

void VkHardwareTexture::Reset()
{
	if (fb)
	{
		if (mappedSWFB)
		{
			mImage->Image->Unmap();
			mappedSWFB = nullptr;
		}

		mImage->Reset(fb);
		mDepthStencil->Reset(fb);

		if (mLoadedImage) mLoadedImage->Reset(fb);

		hwState = NONE;
	}
}

// Swap the loaded image with the main image
// This is likely because the image was loaded in the background but hasn't been used yet
void VkHardwareTexture::SwapToLoadedImage() {
	if (!mLoadedImage) return;

	if (mappedSWFB)
	{
		mImage->Image->Unmap();
		mappedSWFB = nullptr;
	}

	assert(mLoadedImage != mImage);
	
	mImage->Reset(fb);
	mDepthStencil->Reset(fb);

	mImage.reset(mLoadedImage.release());
}

VkTextureImage *VkHardwareTexture::GetImage(FTexture *tex, int translation, int flags)
{
	if (!mImage->Image)
	{
		if (mLoadedImage && mLoadedImage->Image) {
			SwapToLoadedImage();
		}
		else {
			CreateImage(tex, translation, flags);
		}
	}
	return mImage.get();
}

VkTextureImage *VkHardwareTexture::GetDepthStencil(FTexture *tex)
{
	if (!mDepthStencil || !mDepthStencil->View)
	{
		VkFormat format = fb->GetBuffers()->SceneDepthStencilFormat;
		int w = tex->GetWidth();
		int h = tex->GetHeight();

		mDepthStencil->Image = ImageBuilder()
			.Size(w, h)
			.Samples(VK_SAMPLE_COUNT_1_BIT)
			.Format(format)
			.Usage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			.DebugName("VkHardwareTexture.DepthStencil")
			.Create(fb->device);

		mDepthStencil->AspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

		mDepthStencil->View = ImageViewBuilder()
			.Image(mDepthStencil->Image.get(), format, VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
			.DebugName("VkHardwareTexture.DepthStencilView")
			.Create(fb->device);

		VkImageTransition()
			.AddImage(mDepthStencil.get(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, true)
			.Execute(fb->GetCommands()->GetTransferCommands());
	}
	return mDepthStencil.get();
}

void VkHardwareTexture::CreateImage(FTexture *tex, int translation, int flags)
{
	
	if (!tex->isHardwareCanvas())
	{
#ifndef NDEBUG
		// Output a texture load on the main thread for debugging. 
		if (tex->GetSourceLump() > 0) {
			auto* rLump = fileSystem.GetFileAt(tex->GetSourceLump());
			Printf("Making texture: %s\n", !!rLump ? rLump->getName() : "No Lump Found!");
		}
		else if (tex->GetImage() && tex->GetImage()->LumpNum() > 0) {
			auto* rLump = fileSystem.GetFileAt(tex->GetImage()->LumpNum());
			Printf("Making texture2: %s\n", !!rLump ? rLump->getName() : "No Lump Found!");
		}
#endif

		// @Cockatrice - Special case for GPU only textures
		// These texture cannot be manipulated, so just straight up load them into the GPU now
		// We are completely ignoring any translations, upscaling, trimming or effects here
		FImageSource* src = tex->GetImage();
		if (src && src->IsGPUOnly()) {
			unsigned char* pixelData = nullptr;
			size_t pixelDataSize = 0, totalDataSize = 0;
			int mipLevels = 0;
			uint32_t srcWidth = src->GetWidth(), srcHeight = src->GetHeight();

			// Create a reader
			auto *rLump = fileSystem.GetFileAt(src->LumpNum());
			if (!rLump) 
				return;

			FileReader *reader = rLump->Owner->GetReader();
			reader = reader ? reader->CopyNew() : rLump->NewReader().CopyNew();
			if (!reader) return;
			reader->Seek(rLump->GetFileOffset(), FileReader::SeekSet);

			// Read pixels
			src->ReadCompressedPixels(reader, &pixelData, totalDataSize, pixelDataSize, mipLevels);

			// Create texture
			// Mipmaps must be read from the source image, they cannot be generated
			// TODO: Find some way to prevent UI textures from loading mipmaps. After all they are never used when rendering and just straight up eating VRAM for no reason
			uint32_t expectedMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(srcWidth, srcHeight)))) + 1;
			if (mipLevels > 0 && mipLevels == (int)expectedMipLevels) {
				CreateTexture(fb->GetCommands(), mImage.get(), src->GetWidth(), src->GetHeight(), 4, VK_FORMAT_BC7_UNORM_BLOCK, pixelData, true, false, (int)pixelDataSize);

				uint32_t mipWidth = srcWidth, mipHeight = srcHeight;
				uint32_t mipSize = (uint32_t)pixelDataSize, dataPos = (uint32_t)pixelDataSize;

				for (int x = 1; x < mipLevels; x++) {
					mipWidth = std::max(1u, (mipWidth >> 1));
					mipHeight = std::max(1u, (mipHeight >> 1));
					mipSize = std::max(1u, ((mipWidth + 3) / 4)) * std::max(1u, ((mipHeight + 3) / 4)) * 16;
					if (mipSize == 0 || totalDataSize - dataPos < mipSize)
						break;
					CreateTextureMipMap(fb->GetCommands(), mImage.get(), x, mipWidth, mipHeight, 4, VK_FORMAT_BC7_UNORM_BLOCK, pixelData + dataPos, mipSize);
					dataPos += mipSize;
				}
			}
			else {
				CreateTexture(fb->GetCommands(), mImage.get(), src->GetWidth(), src->GetHeight(), 4, VK_FORMAT_BC7_UNORM_BLOCK, pixelData, false, false, (int)pixelDataSize);
			}
			
			free(pixelData);
			free(reader);
			hwState = READY;
		}
		else {
			FTextureBuffer texbuffer = tex->CreateTexBuffer(translation, flags | CTF_ProcessData);
			bool indexed = flags & CTF_Indexed;
			CreateTexture(texbuffer.mWidth, texbuffer.mHeight, indexed ? 1 : 4, indexed ? VK_FORMAT_R8_UNORM : VK_FORMAT_B8G8R8A8_UNORM, texbuffer.mBuffer, !indexed);
		}
	}
	else
	{
#ifndef NDEBUG
		// Output a texture load on the main thread for debugging. 
		if (tex->GetSourceLump() > 0) {
			auto* rLump = fileSystem.GetFileAt(tex->GetSourceLump());
			Printf("Making texture in the weird way: %s\n", rLump ? rLump->getName() : "None");
		}
#endif

		VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
		int w = tex->GetWidth();
		int h = tex->GetHeight();

		mImage->Image = ImageBuilder()
			.Format(format)
			.Size(w, h)
			.Usage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
			.DebugName("VkHardwareTexture.mImage")
			.Create(fb->device);

		mImage->View = ImageViewBuilder()
			.Image(mImage->Image.get(), format)
			.DebugName("VkHardwareTexture.mImageView")
			.Create(fb->device);

		VkImageTransition()
			.AddImage(mImage.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true)
			.Execute(fb->GetCommands()->GetTransferCommands());

		hwState = READY;
	}
}

void VkHardwareTexture::CreateTexture(int w, int h, int pixelsize, VkFormat format, const void *pixels, bool mipmap) {
	CreateTexture(fb->GetCommands(), mImage.get(), w, h, pixelsize, format, pixels, mipmap);
	hwState = READY;
}

void VkHardwareTexture::BackgroundCreateTexture(VkCommandBufferManager* bufManager, int w, int h, int pixelsize, VkFormat format, const void *pixels, bool mipmap, bool createMips, int totalSize) {
	if (!mLoadedImage) mLoadedImage.reset(new VkTextureImage());
	else {
		//mLoadedImage->Reset(fb);
		assert(mLoadedImage != nullptr);
		return; // We cannot reset the loaded image on a different thread
	}

	CreateTexture(bufManager, mLoadedImage.get(), w, h, pixelsize, format, pixels, mipmap, createMips && fb->device->uploadFamilySupportsGraphics, totalSize);

	// Flush commands as they come in, since we don't have a steady frame loop in the background thread
	/*if (bufManager->TransferDeleteList->TotalSize > 1) {
		bufManager->WaitForCommands(false, true);
	}*/
}


void VkHardwareTexture::BackgroundCreateTextureMipMap(VkCommandBufferManager* bufManager, int mipLevel, int w, int h, int pixelsize, VkFormat format, const void* pixels, int totalSize) {
	if (!mLoadedImage) {
		return;
	}

	CreateTextureMipMap(bufManager, mLoadedImage.get(), mipLevel, w, h, pixelsize, format, pixels, totalSize);

	// Flush commands as they come in, since we don't have a steady frame loop in the background thread
	// TODO: Make this a manual flush, since we are going to call a couple of these mipmap creations in a row
	/*if (bufManager->TransferDeleteList->TotalSize > 1) {
		bufManager->WaitForCommands(false, true);
	}*/
}


void VkHardwareTexture::CreateTextureMipMap(VkCommandBufferManager* bufManager, VkTextureImage *img, int mipLevel, int w, int h, int pixelsize, VkFormat format, const void* pixels, int totalSize) {
	if (w <= 0 || h <= 0)
		throw CVulkanError("Trying to create zero size mipmap!");

	auto stagingBuffer = BufferBuilder()
		.Size(totalSize)
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("VkHardwareTexture.mStagingBuffer")
		.Create(fb->device);

	uint8_t* data = (uint8_t*)stagingBuffer->Map(0, totalSize);
	memcpy(data, pixels, totalSize);
	stagingBuffer->Unmap();

	auto cmdBuffer = bufManager->GetTransferCommands();

	// Assumes image is still in transfer layout!
	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageSubresource.mipLevel = mipLevel;
	region.imageExtent.depth = 1;
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	cmdBuffer->copyBufferToImage(stagingBuffer->buffer, img->Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	// If we queued more than 64 MB of data already: wait until the uploads finish before continuing
	bufManager->TransferDeleteList->Add(std::move(stagingBuffer));
	if (bufManager->TransferDeleteList->TotalSize > (size_t)64 * 1024 * 1024) {
		bufManager->WaitForCommands(false, true);
	}
}

void VkHardwareTexture::CreateTexture(VkCommandBufferManager *bufManager, VkTextureImage *img, int w, int h, int pixelsize, VkFormat format, const void *pixels, bool mipmap, bool generateMipmaps, int totalSize)
{
	if (w <= 0 || h <= 0)
		throw CVulkanError("Trying to create zero size texture");

	if(totalSize < 0) totalSize = w * h * pixelsize;
	int mipLevels = !mipmap ? 1 : GetMipLevels(w, h);

	auto stagingBuffer = BufferBuilder()
		.Size(totalSize)
		.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
		.DebugName("VkHardwareTexture.mStagingBuffer")
		.Create(fb->device);

	uint8_t *data = (uint8_t*)stagingBuffer->Map(0, totalSize);
	memcpy(data, pixels, totalSize);
	stagingBuffer->Unmap();

	img->Image = ImageBuilder()
		.Format(format)
		.Size(w, h, mipLevels)
		.Usage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
		.DebugName("VkHardwareTexture.mImage")
		.Create(fb->device);

	img->View = ImageViewBuilder()
		.Image(img->Image.get(), format)
		.DebugName("VkHardwareTexture.mImageView")
		.Create(fb->device);

	auto cmdBuffer = bufManager->GetTransferCommands();

	VkImageTransition()
		.AddImage(img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true, 0, mipLevels)
		.Execute(cmdBuffer);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent.depth = 1;
	region.imageExtent.width = w;
	region.imageExtent.height = h;
	cmdBuffer->copyBufferToImage(stagingBuffer->buffer, img->Image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	if (generateMipmaps && mipmap) img->GenerateMipmaps(cmdBuffer);

	// If we queued more than 64 MB of data already: wait until the uploads finish before continuing
	bufManager->TransferDeleteList->Add(std::move(stagingBuffer));
	if (bufManager->TransferDeleteList->TotalSize > (size_t)64 * 1024 * 1024) {
		bufManager->WaitForCommands(false, true);
		//hwState = READY;
	}
}

void VkHardwareTexture::ReleaseLoadedFromQueue(VulkanCommandBuffer *cmd, int fromQueueFamily, int toQueueFamily) {
	assert(mLoadedImage.get());

	PipelineBarrier().AddQueueTransfer(
		fb->device->uploadFamily,
		fb->device->graphicsFamily,
		mLoadedImage->Image.get(),
		mLoadedImage->Layout,
		VK_IMAGE_ASPECT_COLOR_BIT, 
		0,
		mLoadedImage->Image->mipLevels
	).Execute(
		cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT	// TODO: Could we be more loose here? 
	);
}

void VkHardwareTexture::AcquireLoadedFromQueue(VulkanCommandBuffer *cmd, int fromQueueFamily, int toQueueFamily) {
	assert(mLoadedImage.get());

	PipelineBarrier().AddQueueTransfer(
		fb->device->uploadFamily,
		fb->device->graphicsFamily,
		mLoadedImage->Image.get(),
		mLoadedImage->Layout,
		VK_IMAGE_ASPECT_COLOR_BIT, 
		0,
		mLoadedImage->Image->mipLevels
	).Execute(
		cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_TRANSFER_BIT
	);
}

int VkHardwareTexture::GetMipLevels(int w, int h)
{
	int levels = 1;
	while (w > 1 || h > 1)
	{
		w = max(w >> 1, 1);
		h = max(h >> 1, 1);
		levels++;
	}
	return levels;
}

void VkHardwareTexture::AllocateBuffer(int w, int h, int texelsize)
{
	if (mImage->Image && (mImage->Image->width != w || mImage->Image->height != h || mTexelsize != texelsize))
	{
		Reset();
	}

	if (!mImage->Image)
	{
		VkFormat format = texelsize == 4 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8_UNORM;

		VkDeviceSize allocatedBytes = 0;
		mImage->Image = ImageBuilder()
			.Format(format)
			.Size(w, h)
			.LinearTiling()
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_UNKNOWN, VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT)
			.MemoryType(
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
			.DebugName("VkHardwareTexture.mImage")
			.Create(fb->device, &allocatedBytes);

		mTexelsize = texelsize;

		mImage->View = ImageViewBuilder()
			.Image(mImage->Image.get(), format)
			.DebugName("VkHardwareTexture.mImageView")
			.Create(fb->device);

		VkImageTransition()
			.AddImage(mImage.get(), VK_IMAGE_LAYOUT_GENERAL, true)
			.Execute(fb->GetCommands()->GetTransferCommands());

		bufferpitch = int(allocatedBytes / h / texelsize);
	}
}

uint8_t *VkHardwareTexture::MapBuffer()
{
	if (!mappedSWFB)
		mappedSWFB = (uint8_t*)mImage->Image->Map(0, mImage->Image->width * mImage->Image->height * mTexelsize);
	return mappedSWFB;
}

unsigned int VkHardwareTexture::CreateTexture(unsigned char * buffer, int w, int h, int texunit, bool mipmap, const char *name)
{
	// CreateTexture is used by the software renderer to create a screen output but without any screen data.
	if (buffer)
		CreateTexture(w, h, mTexelsize, mTexelsize == 4 ? VK_FORMAT_B8G8R8A8_UNORM : VK_FORMAT_R8_UNORM, buffer, mipmap);
	return 0;
}

void VkHardwareTexture::CreateWipeTexture(int w, int h, const char *name)
{
	VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;

	mImage->Image = ImageBuilder()
		.Format(format)
		.Size(w, h)
		.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY)
		.DebugName(name)
		.Create(fb->device);

	mTexelsize = 4;

	mImage->View = ImageViewBuilder()
		.Image(mImage->Image.get(), format)
		.DebugName(name)
		.Create(fb->device);

	if (fb->GetBuffers()->GetWidth() > 0 && fb->GetBuffers()->GetHeight() > 0)
	{
		fb->GetPostprocess()->BlitCurrentToImage(mImage.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else
	{
		// hwrenderer asked image data from a frame buffer that was never written into. Let's give it that..
		// (ideally the hwrenderer wouldn't do this, but the calling code is too complex for me to fix)

		VkImageTransition()
			.AddImage(mImage.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, true)
			.Execute(fb->GetCommands()->GetTransferCommands());

		VkImageSubresourceRange range = {};
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.layerCount = 1;
		range.levelCount = 1;

		VkClearColorValue value = {};
		value.float32[0] = 0.0f;
		value.float32[1] = 0.0f;
		value.float32[2] = 0.0f;
		value.float32[3] = 1.0f;
		fb->GetCommands()->GetTransferCommands()->clearColorImage(mImage->Image->image, mImage->Layout, &value, 1, &range);

		VkImageTransition()
			.AddImage(mImage.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false)
			.Execute(fb->GetCommands()->GetTransferCommands());
	}
}

/////////////////////////////////////////////////////////////////////////////

VkMaterial::VkMaterial(VulkanFrameBuffer* fb, FGameTexture* tex, int scaleflags) : FMaterial(tex, scaleflags), fb(fb)
{
	fb->GetDescriptorSetManager()->AddMaterial(this);
}

VkMaterial::~VkMaterial()
{
	if (fb)
		fb->GetDescriptorSetManager()->RemoveMaterial(this);
}

void VkMaterial::DeleteDescriptors()
{
	if (fb)
	{
		auto deleteList = fb->GetCommands()->DrawDeleteList.get();
		for (auto& it : mDescriptorSets)
		{
			deleteList->Add(std::move(it.descriptor));
		}
		mDescriptorSets.clear();
	}
}

VulkanDescriptorSet* VkMaterial::GetDescriptorSet(const FMaterialState& state)
{
	auto base = Source();
	int clampmode = state.mClampMode;
	int translation = state.mTranslation;
	auto translationp = IsLuminosityTranslation(translation)? translation : intptr_t(GPalette.GetTranslation(GetTranslationType(translation), GetTranslationIndex(translation)));

	clampmode = base->GetClampMode(clampmode);

	for (auto& set : mDescriptorSets)
	{
		if (set.descriptor && set.clampmode == clampmode && set.remap == translationp) return set.descriptor.get();
	}

	int numLayers = NumLayers();

	auto descriptor = fb->GetDescriptorSetManager()->AllocateTextureDescriptorSet(max(numLayers, SHADER_MIN_REQUIRED_TEXTURE_LAYERS));

	descriptor->SetDebugName("VkHardwareTexture.mDescriptorSets");

	VulkanSampler* sampler = fb->GetSamplerManager()->Get(clampmode);

	WriteDescriptors update;
	MaterialLayerInfo *layer;
	auto systex = static_cast<VkHardwareTexture*>(GetLayer(0, state.mTranslation, &layer));
	auto systeximage = systex->GetImage(layer->layerTexture, state.mTranslation, layer->scaleFlags);
	update.AddCombinedImageSampler(descriptor.get(), 0, systeximage->View.get(), sampler, systeximage->Layout);

	if (!(layer->scaleFlags & CTF_Indexed))
	{
		for (int i = 1; i < numLayers; i++)
		{
			auto syslayer = static_cast<VkHardwareTexture*>(GetLayer(i, 0, &layer));
			auto syslayerimage = syslayer->GetImage(layer->layerTexture, 0, layer->scaleFlags);
			update.AddCombinedImageSampler(descriptor.get(), i, syslayerimage->View.get(), sampler, syslayerimage->Layout);
		}
	}
	else
	{
		for (int i = 1; i < 3; i++)
		{
			auto syslayer = static_cast<VkHardwareTexture*>(GetLayer(i, translation, &layer));
			auto syslayerimage = syslayer->GetImage(layer->layerTexture, 0, layer->scaleFlags);
			update.AddCombinedImageSampler(descriptor.get(), i, syslayerimage->View.get(), sampler, syslayerimage->Layout);
		}
		numLayers = 3;
	}

	auto dummyImage = fb->GetTextureManager()->GetNullTextureView();
	for (int i = numLayers; i < SHADER_MIN_REQUIRED_TEXTURE_LAYERS; i++)
	{
		update.AddCombinedImageSampler(descriptor.get(), i, dummyImage, sampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	update.Execute(fb->device);
	mDescriptorSets.emplace_back(clampmode, translationp, std::move(descriptor));
	return mDescriptorSets.back().descriptor.get();
}

