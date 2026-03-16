#pragma once

#include "miniaudio.h"
#include <vector>
#include <atomic>

class RingBuffer {
public:
    //allows default or manual construction, if default, run init before use
    //no moves or copies, since the atomics make that a pain
    RingBuffer() = default;
    RingBuffer(ma_uint32 size, ma_uint32 channelAmt) {
        bufferSize = size;
        channels = channelAmt;
        buffer.resize(size);
    }
	RingBuffer(const RingBuffer&) = delete;
	RingBuffer& operator=(const RingBuffer&) = delete;
	RingBuffer(RingBuffer&&) = delete;
	RingBuffer& operator=(RingBuffer&&) = delete;
    ~RingBuffer() = default;

    float& operator[](size_t i) { return buffer[i]; }
    const float& operator[](size_t i) const { return buffer[i]; }

    void init(ma_uint32 size, ma_uint32 channelAmt) {
        bufferSize = size;
        channels = channelAmt;
        buffer.resize(size);
    }

	//NOTE: copies last count of samples read to given out buffer. 
	//Handles channel count. Just needs frame count in miniaudio terms.
	//if you would like for this to be considered as a read for your read index, 
	//call setReadIndexForwardByFrames
	void getWindow(float* out, ma_uint32 frameAmt, uint32_t passedWrite = 0) {
		uint32_t localWrite = (passedWrite == 0) ? writeIndex.load() : passedWrite;
		ma_uint32 start = 0;
		frameAmt *= channels;
		
		if (frameAmt > localWrite) {
			start = localWrite - frameAmt + bufferSize;
		}
		else {
			start = localWrite - frameAmt;
		}

		for (ma_uint32 i = 0; i < frameAmt; ++i) {
			uint32_t index = (start + i) % bufferSize;
			out[i] = buffer[index];
		}
	}

	void getMonoSummedWindow(float* out, ma_uint32 frameAmt, uint32_t passedWrite = 0) {
		uint32_t localWrite = (passedWrite == 0) ? writeIndex.load() : passedWrite;
		uint32_t start = 0;
		ma_uint32 channels = channels;
		frameAmt *= channels;

		if (frameAmt > localWrite) {
			start = localWrite - frameAmt + bufferSize;
		}
		else {
			start = localWrite - frameAmt;
		}

		for (ma_uint32 i = 0; i < frameAmt; i += channels) {
			float sum = 0;
			for (ma_uint32 ch = 0; ch < channels; ++ch) {
				sum += buffer[(start + i + ch) % bufferSize];
			}
			out[i / channels] = sum / (float)channels;
		}
	}

	int pop(float* out, ma_uint32 maxFrames, uint32_t passedWrite = 0) {
		uint32_t localRead = readIndex.load();
		uint32_t localWrite = (passedWrite == 0) ? writeIndex.load() : passedWrite;
		ma_uint32 readSize = 0;
		maxFrames *= channels;

		if (localWrite < localRead) {
			readSize = localWrite + (bufferSize - localRead);
		}
		else {
			readSize = localWrite - localRead;
		}

		if (readSize > maxFrames) {
			readSize = maxFrames;
		}

		for (ma_uint32 i = 0; i < readSize; ++i) {
			int index = (localRead + i) % bufferSize;
			out[i] = buffer[index];
		}

		readIndex.store((localRead + readSize) % bufferSize);

		return readSize / channels;
	}

	bool setReadIndexForwardByFrames(uint32_t i, uint32_t passedWrite = 0) {
		if (i > bufferSize / channels) {
			return false;
		}
		ma_uint32 oldRead = readIndex.load();
		ma_uint32 advance = i * channels;
		ma_uint32 write = (passedWrite == 0) ? writeIndex.load() : passedWrite;
		ma_uint32 available = (write - oldRead + bufferSize) % bufferSize;
		if (advance > available) {
			advance = available;
		}
		readIndex.store((oldRead + advance) % bufferSize);
        return true;
	}

	uint32_t getWindowStartFromWrite(uint32_t windowSize) {
		uint32_t samples = windowSize * channels;
		return (writeIndex.load() + bufferSize - samples) % bufferSize;
	}

	uint32_t getBufferSizeInSamples() {
		return bufferSize;
	}

	uint32_t getBufferSizeInFrames() {
		return bufferSize / channels;
	}

	float* getRawBufferPointer() {
		return buffer.data();
	}

	std::atomic<ma_uint32> writeIndex{0};
	std::atomic<ma_uint32> readIndex{0};

private:
	std::vector<float> buffer;
    int bufferSize = 0;
    int channels = 2;
};
