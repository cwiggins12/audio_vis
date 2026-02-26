#pragma once

#include "miniaudio.h"
#include <vector>
#include <atomic>
#include <cstring>
#include <cstdio>

class AudioCapture {
public:
	//This class utilizes miniaudio capture device, houses a ring buffer of its capture, and only needs to know sample size
	//THIS CLASS REQUIRES YOU TO PULL SAMPLES FAST ENOUGH. IF YOU DON'T IT WILL OVERLAP AND ERRORS WILL OCCUR.
	AudioCapture() : writeIndex(0), readIndex(0) {}

	~AudioCapture() {
		shutdown();
	}

	AudioCapture(const AudioCapture&) = delete;
	AudioCapture& operator=(const AudioCapture&) = delete;
	AudioCapture(AudioCapture&&) = delete;
	AudioCapture& operator=(AudioCapture&&) = delete;

	//initializes capture device and ring buffer. Size of ring buffer will be: device channel amount * size * 2
	//you only need to deal with samples (frames in miniaudio terms), not channels. All channel logic is handled internally
	bool init(unsigned int size) {
		if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
			return false;
		}
		ma_device_info* captureInfos;
		ma_uint32 captureCount;

		if (ma_context_get_devices(&context, nullptr, nullptr, &captureInfos, &captureCount) != MA_SUCCESS) {
			return false;
		}

		ma_device_id selectedId = {0};
		bool foundMonitor = false;

		for (ma_uint32 i = 0; i < captureCount; i++) {
			char* name = captureInfos[i].name;
			printf("Capture Device: %s \n", name);
			
			//search name for monitor, necessary if on linux devices. Windows devices have this as a property to check
			if (strstr(name, "monitor") || strstr(name, "Monitor")) {
				selectedId = captureInfos[i].id;
				foundMonitor = true;
				break;
			}
		}
		ma_device_config config = ma_device_config_init(ma_device_type_capture);
		config.capture.format = ma_format_f32;
		config.capture.channels = 0;
		config.sampleRate = 0;
		config.dataCallback = dataCallback;
		config.pUserData = this;

		if (foundMonitor) {
			config.capture.pDeviceID = &selectedId;
			printf("Using monitor device \n");
		}
		else {
			printf("No monitor found \n");
			return false;
		}

		if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
			return false;
		}

		if (ma_device_start(&device) != MA_SUCCESS) {
			return false;
		}
		
		//NOTE: expectation is that the working amount user will need 
		//* channel amount to only require sample amount (frame amount in miniaudio terms)
		bufferSize = size * device.capture.channels;
		buffer.resize(bufferSize);

		return true;
	}

	
	//when given buffer, checks where last internal read ended, and gives unread samples to buffer
	//returns samples read as an ma_uint32, if a buffer limit is needed, pass max for max frames to push to buffer
	ma_uint32 getSamples(float* out, unsigned int max) {
		ma_uint32 localRead = readIndex.load();
		ma_uint32 localWrite = writeIndex.load();
		ma_uint32 readSize = 0;
		max *= device.capture.channels;

		if (localWrite < localRead) {
			readSize = localWrite + (bufferSize - localRead);
		}
		else {
			readSize = localWrite - localRead;
		}

		if (readSize > max) {
			readSize = max;
		}

		for (ma_uint32 i = 0; i < readSize; ++i) {
			ma_uint32 index = (localRead + i) % bufferSize;
			out[i] = buffer[index];
		}

		readIndex.store((localRead + readSize) % bufferSize);

		return readSize;
	}
	
	//NOTE: copies last count of samples read to given out buffer. Handles channel count. Just needs frame count in miniaudio terms.
	//if you would like for this to be considered as a read for your read index, call setReadIndex with the return of this function
	void getWindow(float* out, ma_uint32 frameAmt) {
		ma_uint32 localWrite = writeIndex.load();
		ma_uint32 start = 0;
		frameAmt *= device.capture.channels;
		
		if (frameAmt > localWrite) {
			start = localWrite - frameAmt + bufferSize;
		}
		else {
			start = localWrite - frameAmt;
		}

		for (ma_uint32 i = 0; i < frameAmt; ++i) {
			ma_uint32 index = (start + i) % bufferSize;
			out[i] = buffer[index];
		}
	}

	void getMonoSummedWindow(float* out, int frameAmt) {
		int localWrite = writeIndex.load();
		int start = 0;
		int channels = device.capture.channels;
		frameAmt *= channels;

		if (frameAmt > localWrite) {
			start = localWrite - frameAmt + bufferSize;
		}
		else {
			start = localWrite - frameAmt;
		}

		for (int i = 0; i < frameAmt; i += channels) {
			float sum = 0;
			for (int ch = 0; ch < channels; ++ch) {
				sum += buffer[(start + i + ch) % bufferSize];
			}
			out[i / channels] = sum / (float)channels;
		}
	}

	int pop(float* out, int maxFrames) {
		int localRead = readIndex.load();
		int localWrite = writeIndex.load();
		int readSize = 0;
		maxFrames *= device.capture.channels;

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

		return readSize / device.capture.channels;
	}

	void setReadIndexForwardByFrames(unsigned int i) {
		int localRead = readIndex.load();
		localRead = (localRead + (i * device.capture.channels)) % bufferSize;
		readIndex.store(localRead);
	}

	unsigned int getNumChannels() {
		return device.capture.channels;
	}

	unsigned int getSampleRate() {
		return device.sampleRate;
	}
	
private:
	void shutdown() {
		ma_device_stop(&device);
		ma_device_uninit(&device);
		ma_context_uninit(&context);
	}

	static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
		AudioCapture* self = (AudioCapture*)device->pUserData;
		self->processInput((const float*)input, frameCount);
	}

	void processInput(const float* input, ma_uint32 frameCount) {
		ma_uint32 localWrite = writeIndex.load();
		ma_uint32 totalSamples = frameCount * device.capture.channels;

		for (ma_uint32 i = 0; i < totalSamples; ++i) {
			buffer[localWrite] = input[i];
			localWrite = (localWrite + 1) % bufferSize;
		}
		writeIndex.store(localWrite);
	}

	struct ma_device device;
	ma_context context;

	std::vector<float> buffer;
	std::atomic<ma_uint32> writeIndex;
	std::atomic<ma_uint32> readIndex;

	ma_uint32 bufferSize = 0;
};

