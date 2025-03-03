#pragma once

#include "printf.h"
#include "s_sound.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <map>
#include "stats.h"
#include "TSQueue.h"

// This should encapsulate pre-calculated data on how the sound will be played
// After it finishes loading via the queue
struct AudioQueuePlayInfo {
	FSoundHandle handle;
	FSoundID orgSoundID;
	FVector3 pos, vel;
	int channel, type;
	float pitch, volume, attenuation, startTime;
	EChanFlags flags;
	FRolloffInfo rolloff;
	const void *source = NULL;
};


struct AudioQItem {
	FSoundID soundID;
	sfxinfo_t *sfx = nullptr;
	TArray<AudioQueuePlayInfo> playInfo;

	bool operator ==(const AudioQItem& b) {
		return sfx == b.sfx || soundID == b.soundID;
	}
};


struct AudioQInput {
	FSoundID soundID;
	sfxinfo_t *sfx = nullptr;
	//TArray<AudioQueuePlayInfo> playInfo;
	FileReader *readerCopy = nullptr;		// Requires a reader prepared on the main thread
};

struct AudioQOutput {
	FSoundID soundID;
	sfxinfo_t *sfx = nullptr;
	//TArray<AudioQueuePlayInfo> playInfo;
	char *data = nullptr;					// We only need the data if we are going to cache it but there is no reason to do that for audio since it is buffered and stored with OpenAL, so maybe remove this eventually
	SoundHandle loadedSnd;
	bool createdNewData = false;
};



class AudioLoadThread : public ResourceLoader<AudioQInput, AudioQOutput> {
public:
	std::atomic<int> currentSoundID;		// Used to externally determine if this sound is already being loaded

	// Is this soundID already loading/loaded on this thread?
	bool existsInQueue(int soundID) {
		if (currentSoundID == soundID) return true;
		
		bool found = false;

		mInputQ.foreach([&](AudioQInput &i) {
			if (i.soundID == soundID) found = true;
		});

		if (found) return true;

		mOutputQ.foreach([&](AudioQOutput &o) {
			if (o.soundID == soundID) found = true;
		});

		return found;
	}

protected:
	//bool relinkSound(AudioQueuePlayInfo &pi, int sourcetype, const void *from, const void *to, const FVector3 *optpos);
	bool loadResource(AudioQInput &input, AudioQOutput &output) override;
	void cancelLoad() override { currentSoundID.store(0); }
	void completeLoad() override { currentSoundID.store(0); }
};



class AudioLoaderQueue
{
private:
	struct QStat {
		double totalTime, threadTime, integrationTime;
	};

	//TArray<AudioQItem> mQueue;
	TArray<AudioLoadThread*> mRunning;
	TArray<QStat> mStats;
	std::unordered_map<int, TArray<AudioQueuePlayInfo>> mPlayQueue;	// Stores all of the playback details for each queued sound

	cycle_t updateCycles;
	int totalLoaded = 0, totalFailed = 0;

	bool relinkSound(AudioQueuePlayInfo &item, FSoundID sndID, int sourcetype, const void *from, const void *to, const FVector3 *optpos);
	
	AudioLoadThread *spinupThreads();	// Start as many threads as necessary or specified, return the first one

public:
	static const int MAX_THREADS = 4;	// Max number of threads that will be allowed to be running at once, regardless of CVAR value
	
	AudioLoaderQueue();
	~AudioLoaderQueue();

	
	void startup();
	void shutdown();

	// Call this during the game loop when queued sounds are allowed to be finished and potentially played
	void update();

	// Empty the queue, blocks until current load ops are complete
	void clear();

	// Queue a sound to load. The sound will be played on load if there is valid playInfo data
	void queue(sfxinfo_t *sfx, FSoundID soundID, const AudioQueuePlayInfo *playInfo = NULL);
	void relinkSound(int sourcetype, const void *from, const void *to, const FVector3 *optpos);
	void stopSound(FSoundID soundID);
	void stopSound(int channel, FSoundID soundID);
	void stopSound(int sourcetype, const void* actor, int channel, int soundID);
	void stopSound(FSoundHandle& handle);
	void stopActorSounds(int sourcetype, const void* actor, int chanmin, int chanmax);
	void stopAllSounds();
	int getSoundPlayingInfo(int sourcetype, const void *source, int sound_id, int chann);

	double updateTimeLast() { return updateCycles.TimeMS(); }
	int queueSize();
	int numActive();
	double avgLoad() { return calcLoadAvg(); }
	double maxLoad() { return calcMaxLoad(); }
	double minLoad() { return calcMinLoad(); }

	double calcLoadAvg() {
		double totalLoad = 0;
		for (QStat &s : mStats) { totalLoad += s.threadTime; }
		return mStats.Size() > 0 ? totalLoad / (double)mStats.Size()  : 0;
	}

	double calcMaxLoad() {
		double maxLoad = 0;
		for (QStat &s : mStats) { if (s.threadTime > maxLoad) maxLoad = s.threadTime; }
		return maxLoad;
	}

	double calcMinLoad() {
		double minLoad = mStats.Size() > 0 ? mStats[0].totalTime : -1;
		for (QStat &s : mStats) { if (s.threadTime < minLoad) minLoad = s.threadTime; }
		return minLoad >= 0 ? minLoad : 0;
	}

	double calcMinIntegration() {
		double mintegration = mStats.Size() > 0 ? mStats[0].integrationTime : -1;
		for (QStat &s : mStats) { if (s.integrationTime < mintegration) mintegration = s.integrationTime; }
		return mintegration >= 0 ? mintegration : 0;
	}

	double calcMaxIntegration() {
		double maxIntegration = 0;
		for (QStat &s : mStats) { if (s.integrationTime > maxIntegration) maxIntegration = s.integrationTime; }
		return maxIntegration;
	}

	double calcAvgIntegration() {
		double totalInt = 0;
		for (QStat &s : mStats) { totalInt += s.integrationTime; }
		return mStats.Size() > 0 ? totalInt / (double)mStats.Size() : 0;
	}

	int getTotalLoaded() { return totalLoaded; }
	int getTotalFailed() { return totalFailed; }

	static AudioLoaderQueue *Instance;
};