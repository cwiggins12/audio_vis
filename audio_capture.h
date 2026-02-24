#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <vector>
#include <atomic>
#include <iostream>

class AudioCapture {
public:
	AudioCapture() : writeIndex(0), readIndex(0) {}

	~AudioCapture() {
		shutdown();
	}

	int init() {
		if (SDL_Init(SDL_INIT_AUDIO) < 0) {
			printf("SDL_Init failed: %s\n", SDL_GetError());
			return -1;
		}
		
		int count = 0;
		SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&count);
		if (!devices || count == 0) {
			printf("No recording devices found\n");
			return -1;
		}

		SDL_AudioDeviceID monitor_id = 0;

		for (int i = 0; i < count; ++i) {
			const char *name = SDL_GetAudioDeviceName(devices[i]);
			printf("Recording Device %d: %s\n", i, name);
			if (strstr(name, "Monitor") || strstr(name, ".monitor")) {
				monitor_id = devices[i];
			}
		}

		if (!monitor_id) {
			printf("No monitor device found \n");
			SDL_free(devices);
			return -1;
		}

		SDL_AudioSpec spec;
		SDL_zero(spec);
		spec.freq = 48000;
		spec.format = SDL_AUDIO_F32;
		spec.channels = 2;

		capture_device = SDL_OpenAudioDevice(monitor_id, &spec);
		if (!capture_device) {
			printf("Failed to open device: %s\n", SDL_GetError());
			SDL_free(devices);
			return -1;
		}

		SDL_ResumeAudioDevice(capture_device);

		SDL_free(devices);

		printf("Loopback capture started.\n");
		return 0;
	}

	void shutdown() {

	}
	
	int getSamples(float* out) {

	}
	
	int getBlock(float* out) {

	}
	
	void setReadIndex(int i) {
		readIndex.store(i);
	}

	int getChannels() {
//		return device.capture.channels;
	}
	
private:
//	static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
//		AudioCapture* self = (AudioCapture*)device->pUserData;
//		self->processInput((const float*)input, frameCount);
//	}

//	void processInput(const float* input, ma_uint32 frameCount) {
//		ma_uint32 localWrite = writeIndex.load();
//		ma_uint32 totalSamples = frameCount * device.capture.channels;

//		for (ma_uint32 i = 0; i < totalSamples; ++i) {
//			ringBuffer[localWrite] = input[i];
//			localWrite = (localWrite + 1) % bufferSize;
//		}
//		writeIndex.store(localWrite);
//	}

//	struct ma_device device;
//	ma_context context;

	static SDL_AudioDeviceID capture_device = 0;

	std::vector<float> ringBuffer;
	std::atomic<int> writeIndex;
	std::atomic<int> readIndex;

	int bufferSize = 0;
};

