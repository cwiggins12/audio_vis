#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <vector>
#include <atomic>
#include <iostream>

//eventually, rather than callback, have a run function with a while loop and call it on the audio thread?
//all calls to this class only require frames, channels and ring buffer logic are internally handled
class AudioCapture {
public:
	//setup print on fail pls
	AudioCapture(int size) : writeIndex(0), readIndex(0) {
		init(size);
	}

	~AudioCapture() {
		shutdown();
	}

	void getWindow(float* out, int frameAmt) {
		int localWrite = writeIndex.load();
		int start = 0;
		frameAmt *= spec.channels;
		
		if (frameAmt > localWrite) {
			start = localWrite - frameAmt + bufferSize;
		}
		else {
			start = localWrite - frameAmt;
		}

		for (int i = 0; i < frameAmt; ++i) {
			int index = (start + i) % bufferSize;
			out[i] = buffer[index];
		}
	}

	int pop(float* out, int maxFrames) {
		int localRead = readIndex.load();
		int localWrite = writeIndex.load();
		int readSize = 0;
		maxFrames *= spec.channels;

		if (localWrite < localRead) {
			readSize = localWrite + (bufferSize - localRead);
		}
		else {
			readSize = localWrite - localRead;
		}

		if (readSize > maxFrames) {
			readSize = maxFrames;
		}

		for (int i = 0; i < readSize; ++i) {
			int index = (localRead + i) % bufferSize;
			out[i] = buffer[index];
		}

		readIndex.store((localRead + readSize) % bufferSize);

		return readSize / spec.channels;
	}

	void moveReadIndexForwardByFrames(int i) {
		int localRead = readIndex.load();
		localRead = (localRead + (i * spec.channels)) % bufferSize;
		readIndex.store(localRead);
	}

	int getNumChannels() {
		return spec.channels;
	}
	
private:
	//refactor pls
	int init(int size) {
		if (SDL_Init(SDL_INIT_AUDIO) == false) {
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

		device = SDL_OpenAudioDevice(monitor_id, &spec);
		if (!device) {
			printf("Failed to open device: %s\n", SDL_GetError());
			SDL_free(devices);
			return -1;
		}

		audioStream = SDL_CreateAudioStream(&spec, &spec);
		SDL_BindAudioStream(device, audioStream);

		SDL_SetAudioStreamGetCallback(audioStream, callback, this);

		SDL_ResumeAudioDevice(device);
		SDL_free(devices);

		bufferSize = size * spec.channels;
		buffer.resize(bufferSize);

		printf("Loopback capture started.\n");
		return 0;
	}

	void shutdown() {
		if (audioStream) {
			SDL_DestroyAudioStream(audioStream);
			audioStream = nullptr;	
		}
		if (device) {
			SDL_CloseAudioDevice(device);
			device = 0;
		}
	}

	//static callback for audiostream needs a pointer to instance of self, then calls
	static void SDLCALL callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
		AudioCapture* self = (AudioCapture*)userdata;
		self->push(additional_amount, stream);
	}

	//callback to pass to ring buffer from audiostream
	void push(int bytesToRead, SDL_AudioStream *stream) {
		int floatsToRead = bytesToRead / sizeof(float);
		int bytesRead = 0;
		int localWrite = writeIndex.load();
		if (floatsToRead + localWrite > bufferSize) {
			int amt = bufferSize - localWrite;
			bytesRead = SDL_GetAudioStreamData(stream, buffer.data() + localWrite, amt * sizeof(float));
			floatsToRead = floatsToRead - (bytesRead / sizeof(float));
			if (bytesRead == amt * sizeof(float)) {
				bytesRead += SDL_GetAudioStreamData(stream, buffer.data(), floatsToRead * sizeof(float));
			}
		}
		else {
			bytesRead += SDL_GetAudioStreamData(stream, buffer.data() + localWrite, bytesToRead);
		}
		writeIndex.store((localWrite + (bytesRead / sizeof(float))) % bufferSize);
	}

	SDL_AudioDeviceID device = 0;
	SDL_AudioStream *audioStream = nullptr;
	SDL_AudioSpec spec = {SDL_AUDIO_F32, 2, 48000};

	std::vector<float> buffer;
	std::atomic<int> writeIndex;
	std::atomic<int> readIndex;

	int bufferSize = 0;
};

