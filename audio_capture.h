#pragma once

#include "ring_buffer.h"
#include <cstring>
#include <cstdio>

class AudioCapture {
public:
	//This class utilizes miniaudio capture device, houses a ring buffer of its capture, and only needs to know sample size
	//THIS CLASS REQUIRES YOU TO PULL SAMPLES FAST ENOUGH. IF YOU DON'T IT WILL OVERLAP AND ERRORS WILL OCCUR.
	AudioCapture() {}

	~AudioCapture() {
		shutdown();
	}

	AudioCapture(const AudioCapture&) = delete;
	AudioCapture& operator=(const AudioCapture&) = delete;
	AudioCapture(AudioCapture&&) = delete;
	AudioCapture& operator=(AudioCapture&&) = delete;

	//initializes capture device and ring buffer.
	//Size of ring buffer will be: device channel amount * size * 2
	//you only need to deal with samples (frames in miniaudio terms), not channels.
	//All channel logic is handled internally
	bool init(uint32_t size) {
		if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
			return false;
		}
		ma_device_info* captureInfos;
		ma_uint32 captureCount;

		//TODO: eventually make the device finding a helper func
		if (ma_context_get_devices(&context, nullptr, nullptr, 
								   &captureInfos, &captureCount) != MA_SUCCESS) {
			return false;
		}

		ma_device_id selectedId = {0};
		bool foundMonitor = false;

		for (ma_uint32 i = 0; i < captureCount; i++) {
			char* name = captureInfos[i].name;
			printf("Capture Device: %s \n", name);
			
			//search name for monitor, necessary if on linux devices. 
			//Windows devices have this as a property to check
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
		//* channel amount to only require user interface to deal in sample amount 
		//(frame amount in miniaudio terms)
		bufferSize = size * device.capture.channels;
		buffer.init(bufferSize, device.capture.channels);

		if (ma_device_start(&device) != MA_SUCCESS) {
			return false;
		}

		return true;
	}

	//NOTE: copies last count of samples read to given out buffer. 
	//Handles channel count. Just needs frame count in miniaudio terms.
	//if you would like for this to be considered as a read for your read index, 
	//call setReadIndexForwardByFrames
	void getWindow(float* out, ma_uint32 frameAmt, uint32_t passedWrite = 0) {
		buffer.getWindow(out, frameAmt, passedWrite);
	}

	void getMonoSummedWindow(float* out, ma_uint32 frameAmt, uint32_t passedWrite = 0) {
		buffer.getMonoSummedWindow(out, frameAmt, passedWrite);
	}

	int pop(float* out, ma_uint32 maxFrames, uint32_t passedWrite = 0) {
		return buffer.pop(out, maxFrames, passedWrite);
	}

	void setReadIndexForwardByFrames(uint32_t i, uint32_t passedWrite = 0) {
		if (!buffer.setReadIndexForwardByFrames(i, passedWrite)) {
			printf("setReadIndexForwardByFrames(): given i outside possible range\n");
			return;
		}
	}

	uint32_t getWindowStartFromWrite(uint32_t windowSize) {
		return buffer.getWindowStartFromWrite(windowSize);
	}

	uint32_t getNumChannels() {
		return device.capture.channels;
	}

	uint32_t getSampleRate() {
		return device.sampleRate;
	}

	uint32_t getBufferSizeInSamples() {
		return bufferSize;
	}

	uint32_t getBufferSizeInFrames() {
		return bufferSize / device.capture.channels;
	}

	float* getRawBufferPointer() {
		return buffer.getRawBufferPointer();
	}

	uint32_t getWriteIndex() {
		return buffer.writeIndex.load();
	}

	uint32_t getReadIndex() {
		return buffer.readIndex.load();
	}

	uint32_t getAccumulatedFrames() {
		return framesAccumulated.load();
	}

	void resetAccumulator() {
		framesAccumulated.store(0);
		buffer.writeIndex.store(0);
		buffer.readIndex.store(0);
	}

	void moveAccumulator(uint32_t amt) {
		framesAccumulated.fetch_sub(amt);
	}
	
private:
	void shutdown() {
		ma_device_stop(&device);
		ma_device_uninit(&device);
		ma_context_uninit(&context);
	}

	//NOTE: these 2 are the write funcs handled in ma's thread to fill the ring buffer. 
	//would like to add peak and rms readings here 
	//possibly since they are made for audio thread speed
	static void dataCallback(ma_device* device, void* output, 
							 const void* input, ma_uint32 frameCount) {
		AudioCapture* self = (AudioCapture*)device->pUserData;
		self->processInput((const float*)input, frameCount);
	}

	void processInput(const float* input, ma_uint32 frameCount) {
		ma_uint32 localWrite = buffer.writeIndex.load();
		ma_uint32 totalSamples = frameCount * device.capture.channels;

		for (ma_uint32 i = 0; i < totalSamples; ++i) {
			buffer[localWrite] = input[i];
			localWrite = (localWrite + 1) % bufferSize;
		}

		buffer.writeIndex.store(localWrite);
		framesAccumulated.fetch_add(frameCount);
	}

	//NOTE: total size = bufferSize(size passed in * channels) * sizeof(float) + 
	//sizeof(device) + sizeof(context) + 28 bytes + 8 for buffer config copies
	struct ma_device device;
	ma_context context;

	RingBuffer buffer;
	ma_uint32 bufferSize;

	std::atomic<uint32_t> framesAccumulated{0};
};

