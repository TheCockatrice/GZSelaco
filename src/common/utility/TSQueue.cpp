#include "TSQueue.h"

/*template <typename IP, typename OP>
void ResourceLoader<IP, OP>::bgproc() { 
	std::unique_lock<std::mutex> lock(mWakeLock);

	while (mActive.load()) {
		bool processed = false;

		// Process the queue
		while (true) {
			if (mInputQ.size() > 0) {
				prepareLoad();
			}

			IP input;
			if (!mInputQ.dequeue(input)) {
				cancelLoad();
				break;
			}

			OP output;
			if (loadResource(input, output)) {
				mOutputQ.queue(output);
			}
			processed = true;

			completeLoad();
		}

		if (!processed) {
			mWake.wait_for(lock, std::chrono::milliseconds(5));
		}
	}
}*/