#pragma once

#include "gl_sysfb.h"
#include "engineerrors.h"
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkanobjects.h>
#include "TSQueue.h"
#include "bitmap.h"
#include "printf.h"
#include "image.h"

struct FRenderViewpoint;
class VkSamplerManager;
class VkBufferManager;
class VkTextureManager;
class VkShaderManager;
class VkCommandBufferManager;
class VkDescriptorSetManager;
class VkRenderPassManager;
class VkFramebufferManager;
class VkRaytrace;
class VkRenderState;
class VkStreamBuffer;
class VkHardwareDataBuffer;
class VkHardwareTexture;
class VkRenderBuffers;
class VkPostprocess;
class SWSceneDrawer;

struct VkTexLoadSpiFull {
	bool generateSpi, shouldExpand, notrimming;
	SpritePositioningInfo info[2];
};


struct VkTexLoadSpi {
	bool generateSpi, shouldExpand, notrimming;
};

enum VkTexLoadInFlags {
	TEXLOAD_ALLOWMIPS		= 1,
	TEXLOAD_ALLOWQUALITY	= 1 << 1
};


enum vkTexLoadError {
	VK_TEXLOAD_ERR_NONE = 0,
	VK_TEXLOAD_ERR_FILE,		// File could not be found or opened
	VK_TEXLOAD_ERR_FORMAT,		// Image data could not be understood or fully read
	VK_TEXLOAD_ERR_UPLOAD,		// Texture failed to upload in background thread when the hardware should have been capable
	VK_TEXLOAD_ERR_MEM,			// Ran out of memory when trying to load the file, unused so far
	VK_TEXLOAD_ERR_UNKNOWN		// Any other error
};

struct VkTexLoadIn {
	FImageSource* imgSource;
	FImageLoadParams* params;
	VkTexLoadSpi spi;
	VkHardwareTexture* tex;					// Texture is created in main thread
	FGameTexture* gtex;
	int8_t flags;
	//bool allowMipmaps;
};

struct VkTexLoadOut {
	VkHardwareTexture *tex;
	FGameTexture *gtex;
	VkTexLoadSpiFull spi;
	int conversion, translation;
	bool isTranslucent, createMipmaps;
	FImageSource *imgSource;
	VulkanSemaphore *releaseSemaphore;		// Only used to release the resource when we have to transfer ownership (IE: Not using a graphics queue for upload)
	unsigned char* pixels = nullptr;		// Returned when we can't upload in the backghround thread
	size_t pixelsSize = 0, totalDataSize = 0;
	int pixelW = 0, pixelH = 0;
	int8_t flags;
	int mipmapCount = 1;
	vkTexLoadError error = VK_TEXLOAD_ERR_NONE;
};

struct VkModelLoadIn {
	int lump = -1;
	FModel* model = nullptr;
};

struct VkModelLoadOut {
	int lump = -1;
	FileSys::FileData data;
	FModel* model = nullptr;
};


// @Cockatrice - Background loader thread to handle transfer of texture data
// TODO: Move the queue outside of the object and have each thread pull from a central queue
class VkTexLoadThread : public ResourceLoader2<VkTexLoadIn, VkTexLoadOut> {
public:
	VkTexLoadThread(VkCommandBufferManager* bgCmd, VulkanDevice* device, int uploadQueueIndex, TSQueue<VkTexLoadIn>* inQueue, TSQueue<VkTexLoadIn>* secondaryQueue, TSQueue<VkTexLoadOut>* outQueue) : ResourceLoader2(inQueue, secondaryQueue, outQueue) {
		cmd = bgCmd;
		submits = 0;
		if (uploadQueueIndex >= 0) uploadQueue = device->uploadQueues[uploadQueueIndex];

		if (cmd && device) {
			for (auto& fence : submitFences)
				fence.reset(new VulkanFence(device));

			for (int i = 0; i < 8; i++)
				submitWaitFences[i] = submitFences[i]->fence;
		}
		else {
			for (int i = 0; i < 8; i++)
				submitWaitFences[i] = VK_NULL_HANDLE;
		}
	}

	~VkTexLoadThread() override;

	int getCurrentImageID() { return currentImageID.load(); }
	VulkanUploadSlot& getUploadQueue() { return uploadQueue; }

protected:
	VkCommandBufferManager *cmd;
	VulkanUploadSlot uploadQueue;
	
	std::vector<std::unique_ptr<VulkanCommandBuffer>> deleteList;
	std::unique_ptr<VulkanFence> submitFences[8];
	VkFence submitWaitFences[8];

	int submits;

	std::atomic<int> currentImageID;
	std::atomic<int> maxQueue;

	bool loadResource(VkTexLoadIn &input, VkTexLoadOut &output) override;
	void cancelLoad() override;
	void completeLoad() override;
};


class VkModelLoadThread : public ResourceLoader2<VkModelLoadIn, VkModelLoadOut> {
public:
	VkModelLoadThread(TSQueue<VkModelLoadIn>* inQueue, TSQueue<VkModelLoadOut>* outQueue) : ResourceLoader2(inQueue, nullptr, outQueue) {
		
	}

protected:
	std::atomic<int> maxQueue;

	bool loadResource(VkModelLoadIn& input, VkModelLoadOut& output) override;
};



class VulkanRenderDevice : public SystemBaseFrameBuffer
{
	typedef SystemBaseFrameBuffer Super;


public:
	std::shared_ptr<VulkanDevice> device;

	VkCommandBufferManager* GetCommands() { return mCommands.get(); }
	//VkCommandBufferManager* GetBGCommands() { return mBGTransferCommands.get(); }
	VkShaderManager *GetShaderManager() { return mShaderManager.get(); }
	VkSamplerManager *GetSamplerManager() { return mSamplerManager.get(); }
	VkBufferManager* GetBufferManager() { return mBufferManager.get(); }
	VkTextureManager* GetTextureManager() { return mTextureManager.get(); }
	VkFramebufferManager* GetFramebufferManager() { return mFramebufferManager.get(); }
	VkDescriptorSetManager* GetDescriptorSetManager() { return mDescriptorSetManager.get(); }
	VkRenderPassManager *GetRenderPassManager() { return mRenderPassManager.get(); }
	VkRaytrace* GetRaytrace() { return mRaytrace.get(); }
	VkRenderState *GetRenderState() { return mRenderState.get(); }
	VkPostprocess *GetPostprocess() { return mPostprocess.get(); }
	VkRenderBuffers *GetBuffers() { return mActiveRenderBuffers; }
	FRenderState* RenderState() override;

	unsigned int GetLightBufferBlockSize() const;

	VulkanRenderDevice(void *hMonitor, bool fullscreen, std::shared_ptr<VulkanSurface> surface);
	~VulkanRenderDevice();
	bool IsVulkan() override { return true; }

	void Update() override;

	void InitializeState() override;
	bool CompileNextShader() override;
	void PrecacheMaterial(FMaterial *mat, int translation) override;
	void PrequeueMaterial(FMaterial *mat, int translation) override;
	bool BackgroundCacheMaterial(FMaterial *mat, FTranslationID translation, bool makeSPI = false, bool secondary = false) override;
	bool BackgroundCacheTextureMaterial(FGameTexture *tex, FTranslationID translation, int scaleFlags, bool makeSPI = false) override;
	bool BackgroundLoadModel(FModel* model) override;
	bool CachingActive() override;
	bool SupportsBackgroundCache() override { return bgTransferEnabled; }
	void StopBackgroundCache() override;
	void FlushBackground() override;
	float CacheProgress() override;
	void UpdateBackgroundCache(bool flush = false) override;
	void UploadLoadedTextures(bool flush = false);
	void OnApplicationActivated(bool active) override;
	bool CanDisplay(bool active) override;
	void UpdatePalette() override;
	const char* DeviceName() const override;
	int Backend() override { return 1; }
	void SetTextureFilterMode() override;
	void StartPrecaching() override;
	void BeginFrame() override;
	void InitLightmap(int LMTextureSize, int LMTextureCount, TArray<uint16_t>& LMTextureData) override;
	//void BlurScene(float amount) override;
	void BlurScene(float amount, bool force = false) override;
	void PostProcessScene(bool swscene, int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D) override;
	void AmbientOccludeScene(float m5) override;
	void SetSceneRenderTarget(bool useSSAO) override;
	void SetLevelMesh(hwrenderer::LevelMesh* mesh) override;
	void UpdateShadowMap() override;
	void SetSaveBuffers(bool yes) override;
	void ImageTransitionScene(bool unknown) override;
	void SetActiveRenderTarget() override;

	IHardwareTexture *CreateHardwareTexture(int numchannels) override;
	FMaterial* CreateMaterial(FGameTexture* tex, int scaleflags) override;
	IVertexBuffer *CreateVertexBuffer() override;
	IIndexBuffer *CreateIndexBuffer() override;
	IDataBuffer *CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize) override;

	FTexture *WipeStartScreen() override;
	FTexture *WipeEndScreen() override;

	TArray<uint8_t> GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma) override;

	bool GetVSync() { return mVSync; }
	void SetVSync(bool vsync) override;

	void Draw2D() override;

	void WaitForCommands(bool finish) override;

	bool RaytracingEnabled();

	// Cache stats helpers
	void GetBGQueueSize(int& current, int& currentSec, int& collisions, int& max, int& maxSec, int& total, int& outSize, int &models);
	void GetBGStats(double& min, double& max, double& avg);
	void GetBGStats2(double& min, double& max, double& avg);
	void ResetBGStats();
	int GetNumThreads() { return (int)bgTransferThreads.size(); }

private:
	void RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc) override;
	void PrintStartupLog();
	void CopyScreenToBuffer(int w, int h, uint8_t *data) override;

	struct QueuedPatch {
		FGameTexture *tex;
		int translation, scaleFlags;
		bool generateSPI;
	};

	//inline int findLeastFullSecondaryQueue();
	//inline int findLeastFullPrimaryQueue();

	// BG Thread management
	// TODO: Move these into their own manager object
	int statMaxQueued = 0, statMaxQueuedSecondary = 0, statCollisions = 0, statModelsLoaded = 0;
	TSQueue<VkTexLoadIn> primaryTexQueue, secondaryTexQueue;
	TSQueue<VkTexLoadOut> outputTexQueue;
	TSQueue<QueuedPatch> patchQueue;									// @Cockatrice - Queue of textures to create materials for and submit to the bg thread
	TSQueue<VkModelLoadIn> modelInQueue;
	TSQueue<VkModelLoadOut> modelOutQueue;
	std::unique_ptr<VkModelLoadThread> modelThread;						// Loads models, always 1 thread
	std::vector<std::unique_ptr<VkTexLoadThread>> bgTransferThreads;	// @Cockatrice - Threads that handle the background transfers
	std::unique_ptr<VulkanFence> bgtFence;								// @Cockatrice - Used to block for tranferring resources between queues
	std::vector<std::unique_ptr<VulkanSemaphore>> bgtSm4List;			// Semaphores to release after queue resource transfers
	std::unique_ptr<VulkanCommandBuffer> bgtCmds;
	std::vector<VkTexLoadOut> bgtUploads;								// Main-thread uploads need to be moved here (for gpus that can't transfer in another queue)
	bool bgtHasFence = false;
	bool bgTransferEnabled = true;
	bool bgUploadEnabled = true;
	double fgTotalTime = 0, fgCurTime = 0, fgTotalCount = 0, fgMin = 0, fgMax = 0;		// Foreground integration time stats

	std::unique_ptr<VkCommandBufferManager> mCommands;
	std::vector<std::unique_ptr<VkCommandBufferManager>> mBGTransferCommands;		// @Cockatrice - Command pool for submitting background transfers
	std::unique_ptr<VkBufferManager> mBufferManager;
	std::unique_ptr<VkSamplerManager> mSamplerManager;
	std::unique_ptr<VkTextureManager> mTextureManager;
	std::unique_ptr<VkFramebufferManager> mFramebufferManager;
	std::unique_ptr<VkShaderManager> mShaderManager;
	std::unique_ptr<VkRenderBuffers> mScreenBuffers;
	std::unique_ptr<VkRenderBuffers> mSaveBuffers;
	std::unique_ptr<VkPostprocess> mPostprocess;
	std::unique_ptr<VkDescriptorSetManager> mDescriptorSetManager;
	std::unique_ptr<VkRenderPassManager> mRenderPassManager;
	std::unique_ptr<VkRaytrace> mRaytrace;
	std::unique_ptr<VkRenderState> mRenderState;

	VkRenderBuffers *mActiveRenderBuffers = nullptr;

	bool mVSync = false;
};

class CVulkanError : public CEngineError
{
public:
	CVulkanError() : CEngineError() {}
	CVulkanError(const char* message) : CEngineError(message) {}
};
