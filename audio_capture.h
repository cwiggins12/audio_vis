#pragma once

#include "miniaudio.h"
#include <vector>
#include <atomic>

class AudioCapture {
public:
	AudioCapture();
	~AudioCapture();

	bool init();
	void shutdown();

	void getSamples(std::vector<float>& out);

private:
	static void dataCallback(struct ma_device* device, void* output, const void* input, ma_uint32 frameCount);
	void processInput(const float* input, ma_uint32 frameCount);

	struct ma_device device;
	ma_context context;

	std::vector<float> ringBuffer;
	std::atomic<size_t> writeIndex;

	unsigned int channels;
	unsigned int sampleRate;

	static constexpr size_t BUFFER_SIZE = 44100 * 2;
};
