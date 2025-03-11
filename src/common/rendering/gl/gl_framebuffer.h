#ifndef __GL_FRAMEBUFFER
#define __GL_FRAMEBUFFER

#include "gl_sysfb.h"
#include "m_png.h"
#include "TSQueue.h"
#include "image.h"

#include <memory>



namespace OpenGLRenderer
{

class FHardwareTexture;
class FGLDebug;


/* Background loader classes: TODO: Split GPU and File loading. */
struct GlTexLoadSpiFull {
	bool generateSpi, shouldExpand, notrimming;
	SpritePositioningInfo info[2];
};


struct GlTexLoadSpi {
	bool generateSpi, shouldExpand, notrimming;
};

struct GlTexLoadIn {
	FImageSource* imgSource;
	FImageLoadParams* params;
	GlTexLoadSpi spi;
	FHardwareTexture* tex;
	FGameTexture* gtex;
	int texUnit;
	bool allowMipmaps;
};

struct GlTexLoadOut {
	FHardwareTexture* tex;
	FGameTexture* gtex;
	GlTexLoadSpiFull spi;
	int conversion, translation, texUnit;
	bool isTranslucent;
	FImageSource* imgSource;
	unsigned char* pixels = nullptr;		// Returned when we can't upload in the backghround thread
	size_t pixelsSize = 0, totalDataSize = 0;
	int pixelW = 0, pixelH = 0, mipLevels = -1;
	bool createMipmaps = false;
};

struct GLModelLoadIn {
	int lump = -1;
	FModel* model = nullptr;
};

struct GLModelLoadOut {
	int lump = -1;
	FileSys::FileData data;
	FModel* model = nullptr;
};




class OpenGLFrameBuffer;

// @Cockatrice - Background loader thread to handle transfer of texture data
class GlTexLoadThread : public ResourceLoader2<GlTexLoadIn, GlTexLoadOut> {
public:
	GlTexLoadThread(OpenGLFrameBuffer *buffer, int contextIndex, TSQueue<GlTexLoadIn> *inQueue, TSQueue<GlTexLoadIn>* secondaryQueue, TSQueue<GlTexLoadOut>* outQueue) : ResourceLoader2(inQueue, secondaryQueue, outQueue) {
		auxContext = contextIndex;
		submits = 0;
		cmd = buffer;
	}

	~GlTexLoadThread() override {};

	bool uploadPossible() const { return auxContext >= 0; }

protected:
	OpenGLFrameBuffer* cmd;

	int submits, auxContext;

	std::atomic<int> maxQueue;

	bool loadResource(GlTexLoadIn& input, GlTexLoadOut& output) override;
	void cancelLoad() override {  }		// TODO: Actually finish this
	void completeLoad() override {  }	// TODO: Same
	void prepareLoad() override;

	void bgproc() override;
};


class GLModelLoadThread : public ResourceLoader2<GLModelLoadIn, GLModelLoadOut> {
public:
	GLModelLoadThread(TSQueue<GLModelLoadIn>* inQueue, TSQueue<GLModelLoadOut>* outQueue) : ResourceLoader2(inQueue, nullptr, outQueue) {

	}

protected:
	std::atomic<int> maxQueue;

	bool loadResource(GLModelLoadIn& input, GLModelLoadOut& output) override;
};



class OpenGLFrameBuffer : public SystemGLFrameBuffer
{
	typedef SystemGLFrameBuffer Super;

	void RenderTextureView(FCanvasTexture* tex, std::function<void(IntRect &)> renderFunc) override;

public:

	OpenGLFrameBuffer(void *hMonitor, bool fullscreen) ;
	~OpenGLFrameBuffer();
	int Backend() override { return 2; }
	bool CompileNextShader() override;
	void InitializeState() override;
	void Update() override;

	void AmbientOccludeScene(float m5) override;
	void FirstEye() override;
	void NextEye(int eyecount) override;
	void SetSceneRenderTarget(bool useSSAO) override;
	void UpdateShadowMap() override;
	void WaitForCommands(bool finish) override;
	void SetSaveBuffers(bool yes) override;
	void CopyScreenToBuffer(int width, int height, uint8_t* buffer) override;
	bool FlipSavePic() const override { return true; }

	FRenderState* RenderState() override;
	void UpdatePalette() override;
	const char* DeviceName() const override;
	void SetTextureFilterMode() override;
	IHardwareTexture *CreateHardwareTexture(int numchannels) override;
	void PrecacheMaterial(FMaterial *mat, int translation) override;
	void PrequeueMaterial(FMaterial* mat, int translation) override;
	bool BackgroundLoadModel(FModel* model) override;
	bool BackgroundCacheMaterial(FMaterial* mat, FTranslationID translation, bool makeSPI = false, bool secondary = false) override;
	bool BackgroundCacheTextureMaterial(FGameTexture* tex, FTranslationID translation, int scaleFlags, bool makeSPI = false) override;
	bool CachingActive() override { return secondaryTexQueue.size() > 0; }
	bool SupportsBackgroundCache() override { return bgTransferThreads.size() > 0; }
	void StopBackgroundCache() override;
	void FlushBackground() override;
	float CacheProgress() override { return 0.5; }	// TODO: Report actual progress, there is no way to measure this yet and this function is not used yet
	void UpdateBackgroundCache(bool flush = false) override;

	void BeginFrame() override;
	void SetViewportRects(IntRect *bounds) override;
	void BlurScene(float amount, bool force = false) override;
	IVertexBuffer *CreateVertexBuffer() override;
	IIndexBuffer *CreateIndexBuffer() override;
	IDataBuffer *CreateDataBuffer(int bindingpoint, bool ssbo, bool needsresize) override;

	void InitLightmap(int LMTextureSize, int LMTextureCount, TArray<uint16_t>& LMTextureData) override;

	// Retrieves a buffer containing image data for a screenshot.
	// Hint: Pitch can be negative for upside-down images, in which case buffer
	// points to the last row in the buffer, which will be the first row output.
	virtual TArray<uint8_t> GetScreenshotBuffer(int &pitch, ESSType &color_type, float &gamma) override;

	void Swap();
	bool IsHWGammaActive() const { return HWGammaActive; }

	void SetVSync(bool vsync) override;

	void Draw2D() override;
	void PostProcessScene(bool swscene, int fixedcm, float flash, const std::function<void()> &afterBloomDrawEndScene2D) override;

	bool HWGammaActive = false;			// Are we using hardware or software gamma?
	std::unique_ptr<FGLDebug> mDebug;	// Debug API

    FTexture *WipeStartScreen() override;
    FTexture *WipeEndScreen() override;

	// Cache stats helpers
	
	void GetBGQueueSize(int& current, int& currentSec, int& collisions, int& max, int& maxSec, int& total, int &outSize, int &models);
	void GetBGStats(double& min, double& max, double& avg);
	void GetBGStats2(double& min, double& max, double& avg);
	int GetNumThreads() { return (int)bgTransferThreads.size(); }
	void ResetBGStats();

	int camtexcount = 0;

private:
	struct QueuedPatch {
		FGameTexture* tex;
		FTranslationID translation;
		int scaleFlags;
		bool generateSPI;
	};

	int statMaxQueued = 0, statMaxQueuedSecondary = 0, statCollisions = 0, statModelsLoaded = 0;
	TSQueue<GlTexLoadIn> primaryTexQueue, secondaryTexQueue;
	TSQueue<GlTexLoadOut> outputTexQueue;
	TSQueue<GLModelLoadIn> modelInQueue;
	TSQueue<GLModelLoadOut> modelOutQueue;
	TSQueue<QueuedPatch> patchQueue;									// @Cockatrice - Thread safe queue of textures to create materials for and submit to the bg thread
	std::vector<std::unique_ptr<GlTexLoadThread>> bgTransferThreads;	// @Cockatrice - Threads that handle the background transfers
	std::unique_ptr<GLModelLoadThread> modelThread;						// Loads models, always 1 thread

	double fgTotalTime = 0, fgTotalCount = 0, fgMin = 0, fgMax = 0;		// Foreground integration time stats
};

}

#endif //__GL_FRAMEBUFFER
