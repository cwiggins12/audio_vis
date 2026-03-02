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

		//NOTE: expectation is that the working amount user will need 
		//* channel amount to only require sample amount (frame amount in miniaudio terms)
		bufferSize = size * device.capture.channels;
		firstFillIndex = bufferSize / 2;
		buffer.resize(bufferSize);

		if (ma_device_start(&device) != MA_SUCCESS) {
			return false;
		}

		return true;
	}

	//NOTE: copies last count of samples read to given out buffer. Handles channel count. Just needs frame count in miniaudio terms.
	//if you would like for this to be considered as a read for your read index, call setReadIndexForwardByFrames
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
		if (i > bufferSize / device.capture.channels) {
			printf("setReadIndexForwardByFrames(): given i outside possible range\n");
			return;
		}
		ma_uint32 oldRead = readIndex.load();
		ma_uint32 advance = i * device.capture.channels;
		ma_uint32 write = writeIndex.load();
		ma_uint32 available = (write - oldRead + bufferSize) % bufferSize;
		if (advance > available) {
			advance = available;
		}
		readIndex.store((oldRead + advance) % bufferSize);
	}

	unsigned int getWindowStartFromWrite(unsigned int windowSize) {
		ma_uint32 samples = windowSize * device.capture.channels;
		return (writeIndex.load() + bufferSize - samples) % bufferSize;
	}

	unsigned int getNumChannels() {
		return device.capture.channels;
	}

	unsigned int getSampleRate() {
		return device.sampleRate;
	}

	unsigned int getBufferSizeInSamples() {
		return bufferSize;
	}

	unsigned int getBufferSizeInFrames() {
		return bufferSize / device.capture.channels;
	}

	float* getRawBufferPointer() {
		return buffer.data();
	}

	unsigned getWriteIndex() {
		return writeIndex.load();
	}

	unsigned getReadIndex() {
		return readIndex.load();
	}

	bool getFirstFillFlag() {
		return firstFillDone.load();
	}
	
private:
	void shutdown() {
		ma_device_stop(&device);
		ma_device_uninit(&device);
		ma_context_uninit(&context);
	}

	//NOTE: these 2 are the write funcs handled in ma's thread to fill the ring buffer.
	static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount) {
		AudioCapture* self = (AudioCapture*)device->pUserData;
		self->processInput((const float*)input, frameCount);
	}

	//TODO:this needs a global uint64 to accumulate, rather than the first fill logic.
	//It needs a public setter to reset on analysis loop completion
	void processInput(const float* input, ma_uint32 frameCount) {
		ma_uint32 localWrite = writeIndex.load();
		ma_uint32 totalSamples = frameCount * device.capture.channels;
		bool setFirstFill = false;
		if (!firstFillDone.load() && (localWrite > firstFillIndex || localWrite + totalSamples > firstFillIndex)) {
			setFirstFill = true;
		}
		for (ma_uint32 i = 0; i < totalSamples; ++i) {
			buffer[localWrite] = input[i];
			localWrite = (localWrite + 1) % bufferSize;
		}
		if (setFirstFill) {
			firstFillDone.store(true);
		}
		writeIndex.store(localWrite);
	}

	//NOTE: total size = bufferSize(size passed in * channels) * 4 + sizeof(device) + sizeof(context) + 29 bytes
	struct ma_device device;
	ma_context context;

	std::vector<float> buffer;
	std::atomic<ma_uint32> writeIndex;
	std::atomic<ma_uint32> readIndex;
	ma_uint32 bufferSize;
	//eventually these 2 can be in the parent class
	ma_uint32 firstFillIndex;
	std::atomic<bool> firstFillDone{false};
};

