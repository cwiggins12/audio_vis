#pragma once

#include "miniaudio.h"
#include <vector>
#include <atomic>

class AudioCapture {
public:
	AudioCapture();
	~AudioCapture();

	bool init(ma_uint32 size);
	void shutdown();

	ma_uint32 getSamples(float* out, ma_uint32 max);

	ma_uint32 getSnapshot(float* out, ma_uint32 count);

	void setReadIndex(ma_uint32 i);

private:
	static void dataCallback(struct ma_device* device, void* output, const void* input, ma_uint32 frameCount);
	void processInput(const float* input, ma_uint32 frameCount);

	struct ma_device device;
	ma_context context;

	std::vector<float> ringBuffer;
	std::atomic<ma_uint32> writeIndex;
	std::atomic<ma_uint32> readIndex;

	ma_uint32 bufferSize = 0;
};
