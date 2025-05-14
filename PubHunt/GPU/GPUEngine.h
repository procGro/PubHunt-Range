/*
 * This file is part of the VanitySearch distribution (https://github.com/JeanLucPons/VanitySearch).
 * Copyright (c) 2019 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef GPUENGINEH
#define GPUENGINEH

#include <vector>
#include <string>
#include <curand.h>
#include <curand_kernel.h>

// Number of thread per block
#define ITEM_SIZE_A 60
#define ITEM_SIZE_A32 (ITEM_SIZE_A/4)

typedef struct {
	uint32_t thId;
	uint8_t* pubKey;
	uint8_t* hash160;
} ITEM;

class GPUEngine
{

public:

	GPUEngine(int nbThreadGroup, int nbThreadPerGroup, int gpuId, uint32_t maxFound,
		const uint32_t* hash160, int numHash160,
		const std::string& startKeyHex,
		const std::string& endKeyHex);

	~GPUEngine();

	bool Step(std::vector<ITEM>& dataFound, bool spinWait = false);

	int GetNbThread();
	int GetGroupSize();

	std::string deviceName;

	static void PrintCudaInfo();

private:

	bool Randomize();
	bool CallKernel();

	int nbThread;
	int nbThreadPerGroup;
	int numHash160;

	uint32_t* inputHash;
	uint32_t* inputHashPinned;

	uint64_t* inputKey;

	uint32_t* outputBuffer;
	uint32_t* outputBufferPinned;

	bool initialised;
	bool littleEndian;

	uint32_t maxFound;
	uint32_t outputSize;

	// cuda-random
	curandGenerator_t prngGPU;
	cudaStream_t stream;

	// Range parameters
	bool use_range_;
	uint64_t dev_start_key_[4];
	uint64_t dev_range_span_[4];

	// cuRAND states for in-kernel generation if Randomize changes
	curandStatePhilox4_32_10_t* dev_rand_states_;

};

#endif // GPUENGINEH
