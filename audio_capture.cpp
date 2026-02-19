#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio_capture.h"
#include <cstring>
#include <iostream>

//This class utilizes miniaudio capture device, houses a ring buffer of its capture, and only needs to know sample size
//THIS CLASS REQUIRES YOU TO PULL SAMPLES FAST ENOUGH. IF YOU DON'T IT WILL OVERLAP AND ERRORS WILL OCCUR.
AudioCapture::AudioCapture() : writeIndex(0), readIndex(0) {}

AudioCapture::~AudioCapture() {
	shutdown();
}

//initializes capture device and ring buffer. Size of ring buffer will be: device channel amount * size * 2
//you only need to deal with samples (frames in miniaudio terms), not channels. All channel logic is handled internally
bool AudioCapture::init(ma_uint32 size) {
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
		std::string name = captureInfos[i].name;

		std::cout << "Capture Device: " << name << "\n";
		
		//search name for monitor, necessary if on linux devices. Windows devices have this as a property to check
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
	
	//NOTE: expectation is that the working amount user will need * 2 for saftey 
	//* channel amount to only require sample amount (frame amount in miniaudio terms)
	bufferSize = size * device.capture.channels * 2;
	ringBuffer.resize(bufferSize);

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
	ma_uint32 localWrite = writeIndex.load();
	ma_uint32 totalSamples = frameCount * device.capture.channels;

	for (ma_uint32 i = 0; i < totalSamples; ++i) {
		ringBuffer[localWrite] = input[i];
		localWrite = (localWrite + 1) % bufferSize;
	}

	writeIndex.store(localWrite);
}

//when given buffer, checks where last internal read ended, and gives unread samples to buffer
//returns samples read as an ma_uint32, if a buffer limit is needed, pass max for max frames to push to buffer
ma_uint32 AudioCapture::getSamples(float* out, ma_uint32 max) {
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
		out[i] = ringBuffer[index];
	}

	readIndex.store((localRead + readSize) % bufferSize);

	return readSize;
}

//NOTE: copies last count of samples read to given out buffer. Handles channel count. Just needs frame count in miniaudio terms.
//if you would like for this to be considered as a read for your read index, call setReadIndex with the return of this function
ma_uint32 AudioCapture::getSnapshot(float* out, ma_uint32 count) {
	ma_uint32 localWrite = writeIndex.load();
	ma_uint32 start = 0;
	count *= device.capture.channels;
	
	if (localWrite - count < 0) {
		start = localWrite - count + bufferSize;
	}
	else {
		start = localWrite - count;
	}

	for (ma_uint32 i = 0; i < count; ++i) {
		ma_uint32 index = (start + i) % bufferSize;
		out[i] = ringBuffer[index];
	}
	return localWrite;
}

void AudioCapture::setReadIndex(ma_uint32 i) {
	readIndex.store(i);
}

