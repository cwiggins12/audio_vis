#pragma once

#include "stb/miniaudio.h"
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
	void getWindow(float* out, ma_uint32 frameAmt, uint32_t start) {
		ma_uint32 samples = frameAmt * channels;

		for (ma_uint32 i = 0; i < samples; ++i) {
			uint32_t index = (start + i) % bufferSize;
			out[i] = buffer[index];
		}
	}

	void getMonoSummedWindow(float* out, ma_uint32 frameAmt, uint32_t start) {
		float sumMult = 1.0f / channels;
		for (ma_uint32 i = 0; i < frameAmt; ++i) {
			float sum = 0;
			for (ma_uint32 ch = 0; ch < channels; ++ch) {
				sum += buffer[(start + i * channels + ch) % bufferSize];
			}
			out[i] = sum * sumMult;
		}
	}

	void setReadIndexForwardByFrames(uint32_t hopSize) {
		ma_uint32 oldRead = readIndex.load();
		ma_uint32 advance = hopSize * channels;
		readIndex.store((oldRead + advance) % bufferSize);
	}

	float* getRawBufferPointer() {
		return buffer.data();
	}

	uint32_t getBufferSize() const { return bufferSize; }

	std::atomic<ma_uint32> writeIndex{0};
	std::atomic<ma_uint32> readIndex{0};

private:
	std::vector<float> buffer;
    ma_uint32 channels = 0;
	uint32_t bufferSize = 0;
};

