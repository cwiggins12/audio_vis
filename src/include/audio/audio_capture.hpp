#pragma once

#include "audio/ring_buffer.hpp"
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

struct PlaybackDeviceInfo {
	std::string name = "";
	ma_device_id deviceId;      // playback device ID (used on all platforms)
#ifndef _WIN32
	ma_device_id monitorId;     // PulseAudio monitor capture ID (Linux only)
	bool hasMonitor = false;
#endif
	bool isDefault = false;
};

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
		frameSize = size;

		// Windows: let miniaudio auto-select (will pick WASAPI)
		if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS) {
			std::cerr << "Failed to init audio context\n";
			return false;
		}

		contextReady = true;
		std::cout << "Audio backend: "
		          << ma_get_backend_name(context.backend) << "\n";

		if (!enumerateDevices()) return false;

		// find default playback device
		int defaultIdx = -1;
		for (int i = 0; i < (int)playbackDevices.size(); i++) {
			if (playbackDevices[i].isDefault) {
				defaultIdx = i;
				break;
			}
		}

		if (defaultIdx < 0) {
			std::cerr << "No default playback device found\n";
			return false;
		}

#ifndef _WIN32
		if (!playbackDevices[defaultIdx].hasMonitor) {
			std::cerr << "No default monitor device found\n";
			return false;
		}
#endif

		return initCaptureDevice(defaultIdx);
	}

	bool enumerateDevices() {
		if (!contextReady) return false;

		ma_device_info* playbackInfos;
		ma_uint32 playbackCount;
		ma_device_info* captureInfos;
		ma_uint32 captureCount;

		if (ma_context_get_devices(&context, &playbackInfos, &playbackCount,
		                           &captureInfos, &captureCount) != MA_SUCCESS) {
			std::cerr << "Failed to enumerate devices\n";
			return false;
		}

		playbackDevices.clear();

		for (ma_uint32 i = 0; i < playbackCount; i++) {
			PlaybackDeviceInfo d;
			d.name = playbackInfos[i].name;
			d.isDefault = playbackInfos[i].isDefault;
			d.deviceId = playbackInfos[i].id;

#ifndef _WIN32
			// Linux/PulseAudio: find corresponding monitor in capture list
			d.hasMonitor = false;
			std::string monitorName = "Monitor of " + d.name;
			for (ma_uint32 j = 0; j < captureCount; j++) {
				if (monitorName == captureInfos[j].name) {
					d.monitorId = captureInfos[j].id;
					d.hasMonitor = true;
					break;
				}
			}
#endif
			playbackDevices.push_back(d);
		}

		return true;
	}

	bool switchToDevice(int playbackIndex) {
		if (playbackIndex < 0 ||
		    playbackIndex >= (int)playbackDevices.size()) {
			std::cerr << "Invalid device index: " << playbackIndex << "\n";
			return false;
		}

#ifndef _WIN32
		if (!playbackDevices[playbackIndex].hasMonitor) {
			std::cerr << "Device has no monitor: "
			          << playbackDevices[playbackIndex].name << "\n";
			return false;
		}
#endif

		// teardown current device
		if (deviceReady) {
			ma_device_stop(&device);
			ma_device_uninit(&device);
			deviceReady = false;
		}

		return initCaptureDevice(playbackIndex);
	}

	std::string formatDeviceList() {
		std::string result;
		int count = std::min((int)playbackDevices.size(), 10);
		for (int i = 0; i < count; i++) {
			std::string line = std::to_string(i) + ") " + playbackDevices[i].name;
			if (i == currentDeviceIndex) line += " [ACTIVE]";
			if (playbackDevices[i].isDefault) line += " [DEFAULT]";
#ifndef _WIN32
			if (!playbackDevices[i].hasMonitor) line += " [NO MONITOR]";
#endif
			// pad or truncate to fixed width
			if ((int)line.size() > DEVICE_LINE_WIDTH)
				line = line.substr(0, DEVICE_LINE_WIDTH);
			else
				line.append(DEVICE_LINE_WIDTH - line.size(), ' ');
			result += line;
		}
		std::string footer = "Backspace) Cancel";
		footer.append(DEVICE_LINE_WIDTH - footer.size(), ' ');
		result += footer;
		return result;
	}

	void getMonoSummedWindow(float* out, ma_uint32 frameAmt, uint32_t start) {
		buffer.getMonoSummedWindow(out, frameAmt, start);
	}

	void setReadIndexForwardByFrames(uint32_t i) {
		buffer.setReadIndexForwardByFrames(i);
	}

	uint32_t getNumChannels() {
		return device.capture.channels;
	}

	uint32_t getSampleRate() {
		return device.sampleRate;
	}

	float* getRawBufferPointer() {
		return buffer.getRawBufferPointer();
	}

	uint32_t getReadIndex() {
		return buffer.readIndex.load();
	}

	uint32_t getBufferSize() { return buffer.getBufferSize(); }

	uint32_t getAccumulatedFrames() {
		return framesAccumulated.load();
	}

	void resetAccumulator() {
		resetFlag.store(true);
		framesAccumulated.store(0);
		buffer.writeIndex.store(0);
		buffer.readIndex.store(0);
		resetFlag.store(false);
	}

	void moveAccumulator(uint32_t amt) {
		framesAccumulated.fetch_sub(amt);
	}

	int getDeviceCount() {
		return (int)playbackDevices.size();
	}

	int getCurrentDeviceIndex() {
		return currentDeviceIndex;
	}

	std::vector<PlaybackDeviceInfo> playbackDevices;
	
private:
	bool initCaptureDevice(int playbackIndex) {
#ifdef _WIN32
		// Windows: use loopback mode — captures output of a playback device directly
		ma_device_config config = ma_device_config_init(ma_device_type_loopback);
		config.capture.format = ma_format_f32;
		config.capture.channels = 0;
		config.sampleRate = 0;
		config.dataCallback = dataCallback;
		config.pUserData = this;
		config.capture.pDeviceID = &playbackDevices[playbackIndex].deviceId;
#else
		// Linux: use capture mode with PulseAudio monitor device
		ma_device_config config = ma_device_config_init(ma_device_type_capture);
		config.capture.format = ma_format_f32;
		config.capture.channels = 0;
		config.sampleRate = 0;
		config.dataCallback = dataCallback;
		config.pUserData = this;
		config.capture.pDeviceID = &playbackDevices[playbackIndex].monitorId;
#endif

		if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
			std::cerr << "Failed to init capture device\n";
			return false;
		}

		bufferSize = frameSize * device.capture.channels;
		buffer.reset(bufferSize, device.capture.channels);

		if (ma_device_start(&device) != MA_SUCCESS) {
			std::cerr << "Failed to start capture device\n";
			ma_device_uninit(&device);
			return false;
		}

		deviceReady = true;
		currentDeviceIndex = playbackIndex;

		std::cout << "Capturing: " << playbackDevices[playbackIndex].name << "\n";
		std::cout << "  Sample Rate: " << device.sampleRate << "\n";
		std::cout << "  Channels:    " << device.capture.channels << "\n";

		return true;
	}

	void shutdown() {
		if (deviceReady) {
			ma_device_stop(&device);
			ma_device_uninit(&device);
		}
		if (contextReady) {
			ma_context_uninit(&context);
		}
	}

	//NOTE: these 2 are the write funcs handled in ma's thread to fill the ring buffer.
	static void dataCallback(ma_device* device, void* output,
							 const void* input, ma_uint32 frameCount) {
		AudioCapture* self = (AudioCapture*)device->pUserData;
		self->processInput((const float*)input, frameCount);
	}

	void processInput(const float* input, ma_uint32 frameCount) {
		//flag fixes potential sync race when mid resetAccumulator()
		if (resetFlag.load()) return;
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
	//sizeof(device) + sizeof(context) + 28 bytes + 9 for buffer config copies
	struct ma_device device;
	ma_context context;

	RingBuffer buffer;
	ma_uint32 bufferSize = 0;
	ma_uint32 frameSize = 0;

	int currentDeviceIndex = -1;
	bool contextReady = false;
	bool deviceReady = false;

	std::atomic<uint32_t> framesAccumulated{0};
	std::atomic<bool> resetFlag = false;

	static constexpr int DEVICE_LINE_WIDTH = 72;
};
