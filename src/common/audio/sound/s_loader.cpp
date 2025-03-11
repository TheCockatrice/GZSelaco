#include "s_loader.h"
#include "s_soundinternal.h"
#include "filesystem.h"
#include "s_sound.h"
#include "actor.h"
#include "stats.h"
#include "i_time.h"
#include "fs_swap.h"

AudioLoaderQueue *AudioLoaderQueue::Instance = new AudioLoaderQueue();
const int AudioLoaderQueue::MAX_THREADS;

CVAR(Int, audio_loader_threads, 1, CVAR_ARCHIVE | CVAR_GLOBALCONFIG);

static void AppendAudioThreadStats(int q, int l, double tt, FString &out)
{
	out.AppendFormat(
		"Queued: %d\n"
		"Loading: %d  Total: %d  Failed: %d\n"
		"Avg Load Time: %2.3f +(%2.3f)\n"
		"Min Load Time: %2.3f +(%2.3f)\n"
		"Max Load Time: %2.3f +(%2.3f)\n"
		"Update Time: %2.3f\n",
		q, 
		l, AudioLoaderQueue::Instance->getTotalLoaded(), AudioLoaderQueue::Instance->getTotalFailed(),
		AudioLoaderQueue::Instance->calcLoadAvg(), AudioLoaderQueue::Instance->calcAvgIntegration(),
		AudioLoaderQueue::Instance->calcMinLoad(), AudioLoaderQueue::Instance->calcMinIntegration(),
		AudioLoaderQueue::Instance->calcMaxLoad(), AudioLoaderQueue::Instance->calcMaxIntegration(),
		tt
	);
}

ADD_STAT(audiothread)
{
	static int64_t lasttime = 0, lastUpdateTime = 0;
	static int maxLoading = 0, maxQueue = 0;
	static double maxUpdate = 0;

	if (maxQueue < AudioLoaderQueue::Instance->queueSize()) { maxQueue = AudioLoaderQueue::Instance->queueSize(); }
	if (maxLoading < AudioLoaderQueue::Instance->numActive()) { maxLoading = AudioLoaderQueue::Instance->numActive(); }
	if (maxUpdate < AudioLoaderQueue::Instance->updateTimeLast()) { maxUpdate = AudioLoaderQueue::Instance->updateTimeLast(); }

	auto t = I_msTime();
	if (t - lasttime > 400)
	{
		maxQueue = AudioLoaderQueue::Instance->queueSize();
		maxLoading = AudioLoaderQueue::Instance->numActive();
		lasttime = t;
	}

	if (t - lastUpdateTime > 2000) {
		maxUpdate = AudioLoaderQueue::Instance->updateTimeLast();
		lastUpdateTime = t;
	}

	FString out;
	AppendAudioThreadStats(maxQueue, maxLoading, maxUpdate, out);
	return out;
}



// TODO: Store the sound length in the output to be added to the resource
bool AudioLoadThread::loadResource(AudioQInput &input, AudioQOutput &output) {
	currentSoundID.store(input.soundID.index());
	
	//auto rl = fileSystem.GetFileAt(input.sfx->lumpnum);		// These values do not change at runtime until after teardown
	int size = fileSystem.FileLength(input.lump);

	assert(size >= 0);

	char *data;

	output.createdNewData = true;
	data = new char[size];

	try {
		FileReader reader = fileSystem.OpenFileReader(input.lump, FileSys::EReaderType::READER_NEW, 0);
		reader.Read(data, size);
		reader.Close();
		output.data = data;
	}
	catch (std::exception& e) {
		output.data = nullptr;
	}

	
	output.sfx = input.sfx;
	output.soundID = input.soundID;

	// Try to interpret the data
	if (size > 8 && output.data != nullptr)
	{
		int32_t dmxlen = FileSys::byteswap::LittleLong(((int32_t *)data)[1]);

		// If the sound is voc, use the custom loader.
		if (strncmp(data, "Creative Voice File", 19) == 0)
		{
			output.loadedSnd = GSnd->LoadSoundVoc((uint8_t *)data, size);
		}
		// If the sound is raw, just load it as such.
		else if (input.sfx->bLoadRAW)
		{
			output.loadedSnd = GSnd->LoadSoundRaw((uint8_t *)data, size, input.sfx->RawRate, 1, 8, input.sfx->LoopStart);
		}
		// Otherwise, try the sound as DMX format.
		else if (((uint8_t *)data)[0] == 3 && ((uint8_t *)data)[1] == 0 && dmxlen <= size - 8)
		{
			int frequency = FileSys::byteswap::LittleShort(((uint16_t *)data)[1]);
			if (frequency == 0) frequency = 11025;
			output.loadedSnd = GSnd->LoadSoundRaw((uint8_t *)data + 8, dmxlen, frequency, 1, 8, input.sfx->LoopStart);
		}
		// If that fails, let the sound system try and figure it out.
		else
		{
			output.loadedSnd = GSnd->LoadSound((uint8_t *)data, size, input.sfx->LoopStart, input.sfx->LoopEnd);
		}

		if (output.loadedSnd.isValid()) {
			return true;
		}
	}

	// If we created any data, free it now
	if (output.createdNewData) {
		output.createdNewData = false;
		delete [] data;
	}

	// Always return true, because failed sounds need to be marked as unloadable
	return true;
}


AudioLoaderQueue::AudioLoaderQueue() {
	updateCycles.Reset();
}


AudioLoaderQueue::~AudioLoaderQueue() {
	clear();
}

AudioLoadThread *AudioLoaderQueue::spinupThreads() {
	if ((int)mRunning.Size() >= audio_loader_threads) return nullptr;

	int createThreads = clamp((int)(audio_loader_threads - mRunning.Size()), 0, MAX_THREADS);
	AudioLoadThread *first = nullptr;

	for (int x = 0; x < createThreads; x++) {
		AudioLoadThread *t = new AudioLoadThread();
		t->start();

		mRunning.Push(t);

		if (!first) first = t;
	}

	return first;
}

void AudioLoaderQueue::queue(sfxinfo_t *sfx, FSoundID soundID, const AudioQueuePlayInfo *playInfo) {
	if (sfx->lumpnum == sfx_empty) {
		return;
	}

	bool alreadyLoading = false;

	// Attempt to fold this play instance into an already queued or loading sound
	// This is to avoid loading the sound twice just because it's already queued
	if (playInfo != NULL) {
		// Find or create the correct entry to add playInfo to
		// If we find an entry for this soundID, we assume that it's already loading and skip the load part
		AudioQueuePlayInfo pli = *playInfo;
		
		auto search = mPlayQueue.find(soundID.index());
		if (search != mPlayQueue.end()) {
			search->second.Push(std::move(pli));
			alreadyLoading = true;
		} else {
			TArray<AudioQueuePlayInfo> pl;
			pl.Push(std::move(pli));
			mPlayQueue[soundID.index()] = std::move(pl);
		}
	}

	if (!alreadyLoading) {
		// Check existing threads just in case we are already loading this file
		for (AudioLoadThread *alt : mRunning) {
			if (alt->existsInQueue(soundID)) {
				alreadyLoading = true;
				break;
			}
		}
	}

	// If this sound is not in the queue, add it to the least-full thread
	if (!alreadyLoading) {
		AudioLoadThread *th = nullptr;
		int minQ = 9999999;

		for (AudioLoadThread *thi : mRunning) {
			int numQueued = thi->numQueued();

			if (numQueued < minQ) {
				th = thi;
				minQ = numQueued;
			}
		}

		if (!th) {
			th = spinupThreads();
		}

		if (th) {
			AudioQInput qInput;
			qInput.sfx = sfx;
			qInput.soundID = soundID;
			qInput.lump = sfx->lumpnum;

			th->queue(qInput);
		}
	}
}


void AudioLoaderQueue::relinkSound(int sourcetype, const void *from, const void *to, const FVector3 *optpos) {
	for (auto &pair : mPlayQueue) {
		for(unsigned int x = 0; x < pair.second.Size(); x++) {
			if (!relinkSound(pair.second[x], FSoundID::fromInt(pair.first), sourcetype, from, to, optpos)) {
				pair.second.Delete(x);
				x--;
			}
		}
	}
}

bool AudioLoaderQueue::relinkSound(AudioQueuePlayInfo &item, FSoundID sndID, int sourcetype, const void *from, const void *to, const FVector3 *optpos) {
	if (item.type == sourcetype && item.source == from) {
		//Printf(TEXTCOLOR_BRICK"Relinking an instance of : %s\n", soundEngine->GetSfx(sndID)->name.GetChars());	// TODO: Remove this debug

		item.source = to;

		if (to == NULL) {
			if (!(item.flags & CHANF_LOOP) && optpos) {
				item.pos = *optpos;
				item.type = SOURCE_Unattached;
			}
			else {
				// Delete the play instruction, we can't do a looped sound with no source
				//Printf("Cancelling play of looped sound because it no longer has a source: %s\n", soundEngine->GetSfx(sndID)->name.GetChars());
				return false;
			}
		}
	}

	return true;
}


void AudioLoaderQueue::stopSound(FSoundID soundID) {
	auto search = mPlayQueue.find(soundID.index());
	if (search != mPlayQueue.end()) {
		//Printf("Stopping play of sound in queue by request: %s\n", soundEngine->GetSfx(soundID)->name.GetChars());	// TODO: Remove debug
		search->second.Clear();
	}
}


void AudioLoaderQueue::stopSound(int channel, FSoundID soundID) {
	for (auto &pair : mPlayQueue) {
		for (unsigned int x = 0; x < pair.second.Size(); x++) {
			AudioQueuePlayInfo &info = pair.second[x];

			if (info.type == SOURCE_None &&
				(pair.first == soundID.index() || !soundID.isvalid()) &&
				(channel == CHAN_AUTO || channel == info.channel)) {
				//Printf("Stopping play of sound in queue by chan request: %s\n", soundEngine->GetSfx(pair.first)->name.GetChars());
				pair.second.Delete(x);
				x--;
			}
		}
	}
}


void AudioLoaderQueue::stopSound(int sourcetype, const void* actor, int channel, FSoundID soundID) {
	for (auto &pair : mPlayQueue) {
		for (unsigned int x = 0; x < pair.second.Size(); x++) {
			AudioQueuePlayInfo &info = pair.second[x];

			if (info.source == actor &&
				info.type == sourcetype &&
				(!soundID.isvalid() ? (info.channel == channel || channel < 0) : (pair.first == soundID.index()))) {
				//AActor *a = sourcetype == SOURCE_Actor ? (AActor *)actor : NULL;
				//Printf(TEXTCOLOR_RED"Stopping play of sound in queue by actor:chan request: %s (%s)\n", soundEngine->GetSfx(pair.first)->name.GetChars(), a ? a->GetCharacterName() : "<None>");
				pair.second.Delete(x);
				x--;
			}
		}
	}
}


void AudioLoaderQueue::stopSound(FSoundHandle& handle) {
	for (auto& pair : mPlayQueue) {
		for (unsigned int x = 0; x < pair.second.Size(); x++) {
			AudioQueuePlayInfo& info = pair.second[x];

			if (info.handle == handle) {
				pair.second.Delete(x);
				x--;
			}
		}
	}
}


void AudioLoaderQueue::stopActorSounds(int sourcetype, const void* actor, int chanmin, int chanmax) {
	const bool all = (chanmin == 0 && chanmax == 0);

	for (auto &pair : mPlayQueue) {
		for (unsigned int x = 0; x < pair.second.Size(); x++) {
			AudioQueuePlayInfo &info = pair.second[x];

			if (info.source == actor &&
				info.type == sourcetype &&
				(all || (info.channel >= chanmin && info.channel <= chanmax))) {
				AActor *a = (AActor *)actor;
				//Printf(TEXTCOLOR_RED"Stopping play of sound in queue by all actor request: %s (%s)\n", soundEngine->GetSfx(pair.first)->name.GetChars(), a ? a->GetCharacterName() : "<None>");
				pair.second.Delete(x);
				x--;
			}
		}
	}
}

int AudioLoaderQueue::getSoundPlayingInfo(int sourcetype, const void *source, FSoundID sound_id, int chann) {
	int count = 0;

	for (auto &pair : mPlayQueue) {
		for (const auto& playInfo : pair.second) {
			if (chann != -1 && chann != playInfo.channel) continue;

			if (sound_id.isvalid()) {
				if (playInfo.orgSoundID == sound_id && (sourcetype == SOURCE_Any ||
					(playInfo.source == source)))
				{
					count++;
				}
			}
			else {
				if ((sourcetype == SOURCE_Any || (playInfo.source == source)))
				{
					count++;
				}
			}
		}
	}

	return count;
}


void AudioLoaderQueue::stopAllSounds() {
	mPlayQueue.clear();
}


void AudioLoaderQueue::update() {
	updateCycles.Reset();
	updateCycles.Clock();

	int numPlayed = 0;
	double playMS = 0;
	
	// Dequeue any finished load ops and play the corresponding sounds
	for (unsigned int x = 0; x < mRunning.Size(); x++) {
		AudioQOutput loaded;

		while (mRunning[x]->popFinished(loaded)) {
			cycle_t integrationTime = cycle_t();
			integrationTime.Reset();
			integrationTime.Clock();

			// Did this thread fail?
			if (loaded.data == nullptr && !loaded.loadedSnd.isValid()) {
				totalFailed++;
				continue;
			}
			
			// Move audio data references
			if (loaded.sfx && !loaded.sfx->data.isValid()) {
				if (loaded.loadedSnd.isValid()) {
					//Printf("Moving loaded audio for : %s\n", loaded.sfx->name.GetChars());
					loaded.sfx->data = loaded.loadedSnd;
				} else {
					loaded.sfx->lumpnum = sfx_empty;
					//Printf("Invalid audio data loaded, marking sound : %s\n", loaded.sfx->name.GetChars());
				}
			}

			if (loaded.data != nullptr && loaded.createdNewData) {
				delete [] loaded.data;
			}

			totalLoaded++;

			// TODO: Set the cache pointer if there is data loaded and there is not already a cache
			// Not sure if this is necessary as the cache is probably not needed now that the sound is buffered OpenAL


			// Find associated audio and play
			if (loaded.sfx->data.isValid()) {
				auto search = mPlayQueue.find((int)loaded.soundID.index());
				if (search != mPlayQueue.end()) {
					auto& playlist = search->second;

					//Printf("Finished loading; now playing : %s (%d copies)\n", loaded.sfx->name.GetChars(), playlist.Size());

					cycle_t playTime;
					playTime.Clock();

					for (auto snd : playlist) {
						soundEngine->StartSoundER(loaded.sfx, snd.type, snd.source, snd.pos, snd.vel, snd.channel, snd.flags, loaded.soundID, snd.orgSoundID, snd.volume, snd.attenuation, &snd.rolloff, snd.pitch, snd.startTime, false, snd.handle);
						numPlayed++;
					}

					playTime.Unclock();
					playMS += playTime.TimeMS();
				}

				// Delete the playlist
				mPlayQueue.erase(loaded.soundID.index());
			}

			//mRunning[x]->totalTime.Unclock();
			integrationTime.Unclock();

			// Create a stat entry for this item
			QStat qs = {
				1,//mRunning[x]->totalTime.TimeMS(),
				1,//mRunning[x]->threadTime.TimeMS(),
				integrationTime.TimeMS()
			};

			mStats.Insert(0, qs);
			mStats.Clamp(100);
		}
	}

	updateCycles.Unclock();

	/*if (updateCycles.TimeMS() > 1.0) {
		Printf(TEXTCOLOR_YELLOW"AudioLoaderQueue::Warning Update() took more than 1ms (%0.6fms). Played %d sounds at %0.6fms \n", updateCycles.TimeMS(), numPlayed, playMS);
	}*/
}


void AudioLoaderQueue::clear() {
	mPlayQueue.clear();

	// We can't abort the current jobs yet, we'll have to let them finish
	for (unsigned int x = 0; x < mRunning.Size(); x++) {
		mRunning[x]->clearInputQueue();	// Stop any pending loads
		mRunning[x]->stop();			// Stop the thread, this will not abort the current load but will wait for it to finish, nor does it clear output queue
	}

	// Move audio data references
	update();	// Since we cleared the play queue this should just clear the output queues and link the sounds

	// Why do I keep doing forward deletes? This should go backwards. Shame.
	for (unsigned int x = 0; x < mRunning.Size(); x++) {
		delete mRunning[x];
		mRunning.Delete(x);
		x--;
	}
}


void AudioLoaderQueue::startup() {
	Printf("Booting Audio Loader Queue...");
	spinupThreads();
}

void AudioLoaderQueue::shutdown() {
	clear();

	Printf("Shut down Audio Loader Queue.");
}


int AudioLoaderQueue::queueSize() { 
	int qCount = 0;

	for (auto &t : mRunning) {
		qCount += t->numQueued();
	}

	return qCount;
}

int AudioLoaderQueue::numActive() { 
	int activeCount = 0;

	for (auto &t : mRunning) {
		if (t->numQueued() || t->currentSoundID > 0) {
			activeCount++;
		}
	}

	return activeCount;
}