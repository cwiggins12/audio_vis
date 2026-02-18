#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "audio_capture.h"
#include <cstring>
#include <string>
#include <iostream>

AudioCapture::AudioCapture() : writeIndex(0), channels(2), sampleRate(44100) {
	ringBuffer.resize(BUFFER_SIZE);
}

AudioCapture::~AudioCapture() {
	shutdown();
}

bool AudioCapture::init() {
	if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
		return false;
	}
	ma_device_info* captureInfos;
	ma_uint32 captureCount;

	if (ma_context_get_devices(&context, nullptr, nullptr, &captureInfos, & captureCount) != MA_SUCCESS) {
		return false;
	}

	ma_device_id selectedId = {0};
	bool foundMonitor = false;

	for (ma_uint32 i = 0; i < captureCount; i++) {
		std::string name = captureInfos[i].name;

		std::cout << "Capture Device: " << name << "\n";

		if (name.find("monitor") != std::string::npos || name.find("Monitor") != std::string::npos) {
			selectedId = captureInfos[i].id;
			foundMonitor = true;
			break;
		}
	}

	ma_device_config config = ma_device_config_init(ma_device_type_capture);
	config.dataCallback = dataCallback;
	config.pUserData = this;

	if (foundMonitor) {
		config.capture.pDeviceID = &selectedId;
		std::cout << "Using monitor device \n";
	}
	else {
		std::cout << "No monitor found. Using default device. \n";
	}

	if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
		return false;
	}

	if (ma_device_start(&device) != MA_SUCCESS) {
		return false;
	}

	return true;
}

void AudioCapture::shutdown() {
	ma_device_uninit(&device);
	ma_context_uninit(&context);
}

void AudioCapture::dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
	AudioCapture* self = (AudioCapture*)device->pUserData;
	self->processInput((const float*)input, frameCount);
}

void AudioCapture::processInput(const float* input, ma_uint32 frameCount) {
	size_t localWrite = writeIndex.load();
	size_t totalSamples = frameCount * channels;

	for (size_t i = 0; i < totalSamples; ++i) {
		ringBuffer[localWrite] = input[i];
		localWrite = (localWrite + 1) % BUFFER_SIZE;
	}

	writeIndex.store(localWrite);
}

void AudioCapture::getSamples(std::vector<float>& out) {
	out.resize(BUFFER_SIZE);

	size_t localWrite = writeIndex.load();

	for (size_t i = 0; i < BUFFER_SIZE; ++i) {
		size_t index = (localWrite + i) % BUFFER_SIZE;
		out[i] = ringBuffer[index];
	}
}

