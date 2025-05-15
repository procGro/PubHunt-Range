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

#include "GPUEngine.h"
#include <ctime>
#include <cuda.h>
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <stdint.h>
#include "../Timer.h"
#include "GPUMath.h"
#include "GPUHash.h"
#include "GPUCompute.h"
#include <string> // For std::string
#include <vector> // For std::vector in helpers
#include <stdexcept> // For std::runtime_error
#include <iomanip> // For std::setw, std::setfill
#include <sstream> // For std::ostringstream

// Forward declarations for functions used before they're defined
__host__ bool HostBN_HexToU64Array(const std::string& hex, uint64_t arr[4]);
__host__ uint64_t HostBN_Sub(uint64_t r[4], const uint64_t a[4], const uint64_t b[4]);
__host__ uint64_t HostBN_AddOneInplace(uint64_t r[4]);
__global__ void init_curand_states_kernel(curandStatePhilox4_32_10_t *states, unsigned long long seed, int num_states);
__global__ void generate_keys_in_range_kernel(uint64_t* output_keys, curandStatePhilox4_32_10_t* states, const uint64_t* dev_start_key, const uint64_t* dev_range_span, int num_keys_to_generate);

// ---------------------------------------------------------------------------------------

static const char* __cudaRandGetErrorEnum(curandStatus_t error) {
	switch (error) {
	case CURAND_STATUS_SUCCESS:
		return "CURAND_STATUS_SUCCESS";

	case CURAND_STATUS_VERSION_MISMATCH:
		return "CURAND_STATUS_VERSION_MISMATCH";

	case CURAND_STATUS_NOT_INITIALIZED:
		return "CURAND_STATUS_NOT_INITIALIZED";

	case CURAND_STATUS_ALLOCATION_FAILED:
		return "CURAND_STATUS_ALLOCATION_FAILED";

	case CURAND_STATUS_TYPE_ERROR:
		return "CURAND_STATUS_TYPE_ERROR";

	case CURAND_STATUS_OUT_OF_RANGE:
		return "CURAND_STATUS_OUT_OF_RANGE";

	case CURAND_STATUS_LENGTH_NOT_MULTIPLE:
		return "CURAND_STATUS_LENGTH_NOT_MULTIPLE";

	case CURAND_STATUS_DOUBLE_PRECISION_REQUIRED:
		return "CURAND_STATUS_DOUBLE_PRECISION_REQUIRED";

	case CURAND_STATUS_LAUNCH_FAILURE:
		return "CURAND_STATUS_LAUNCH_FAILURE";

	case CURAND_STATUS_PREEXISTING_FAILURE:
		return "CURAND_STATUS_PREEXISTING_FAILURE";

	case CURAND_STATUS_INITIALIZATION_FAILED:
		return "CURAND_STATUS_INITIALIZATION_FAILED";

	case CURAND_STATUS_ARCH_MISMATCH:
		return "CURAND_STATUS_ARCH_MISMATCH";

	case CURAND_STATUS_INTERNAL_ERROR:
		return "CURAND_STATUS_INTERNAL_ERROR";
	}

	return "<unknown>";
}

inline void __cudaRandSafeCall(curandStatus_t err, const char* file, const int line)
{
	if (CURAND_STATUS_SUCCESS != err)
	{
		fprintf(stderr, "CudaRandSafeCall() failed at %s:%i : %s\n", file, line, __cudaRandGetErrorEnum(err));
		exit(-1);
	}
	return;
}

inline void __cudaSafeCall(cudaError err, const char* file, const int line)
{
	if (cudaSuccess != err)
	{
		fprintf(stderr, "cudaSafeCall() failed at %s:%i : %s\n", file, line, cudaGetErrorString(err));
		exit(-1);
	}
	return;
}

#define CudaRandSafeCall( err ) __cudaRandSafeCall( err, __FILE__, __LINE__ )
#define CudaSafeCall( err ) __cudaSafeCall( err, __FILE__, __LINE__ )

// ---------------------------------------------------------------------------------------

__global__ void compute_hash(uint64_t* keys, uint32_t* hash160, int numHash160, uint32_t maxFound, uint32_t* found)
{

	int id = (blockIdx.x * blockDim.x + threadIdx.x) * 4;
	ComputeHash(keys + id, hash160, numHash160, maxFound, found);

}

// ---------------------------------------------------------------------------------------

using namespace std;

int _ConvertSMVer2Cores(int major, int minor)
{

	// Defines for GPU Architecture types (using the SM version to determine
	// the # of cores per SM
	typedef struct {
		int SM;  // 0xMm (hexidecimal notation), M = SM Major version,
		// and m = SM minor version
		int Cores;
	} sSMtoCores;

	sSMtoCores nGpuArchCoresPerSM[] = {
		{0x20, 32}, // Fermi Generation (SM 2.0) GF100 class
		{0x21, 48}, // Fermi Generation (SM 2.1) GF10x class
		{0x30, 192},
		{0x32, 192},
		{0x35, 192},
		{0x37, 192},
		{0x50, 128},
		{0x52, 128},
		{0x53, 128},
		{0x60,  64},
		{0x61, 128},
		{0x62, 128},
		{0x70,  64},
		{0x72,  64},
		{0x75,  64},
		{0x80,  64},
		{0x86, 128},
		{-1, -1}
	};

	int index = 0;

	while (nGpuArchCoresPerSM[index].SM != -1) {
		if (nGpuArchCoresPerSM[index].SM == ((major << 4) + minor)) {
			return nGpuArchCoresPerSM[index].Cores;
		}

		index++;
	}

	return 0;

}

// ----------------------------------------------------------------------------

GPUEngine::GPUEngine(int nbThreadGroup, int nbThreadPerGroup, int gpuId, uint32_t maxFound,
	const uint32_t* hash160, int numHash160,
	const std::string& startKeyHex, // Added
	const std::string& endKeyHex)   // Added
{
	this->dev_rand_states_ = nullptr;
	this->use_range_ = false;

	// Initialise CUDA
	this->nbThreadPerGroup = nbThreadPerGroup;
	this->numHash160 = numHash160;

	initialised = false;

	int deviceCount = 0;
	CudaSafeCall(cudaGetDeviceCount(&deviceCount));

	// This function call returns 0 if there are no CUDA capable devices.
	if (deviceCount == 0) {
		printf("GPUEngine: There are no available device(s) that support CUDA\n");
		exit(-1);
	}

	CudaSafeCall(cudaSetDevice(gpuId));

	cudaDeviceProp deviceProp;
	CudaSafeCall(cudaGetDeviceProperties(&deviceProp, gpuId));

	if (nbThreadGroup == -1)
		nbThreadGroup = deviceProp.multiProcessorCount * 8;

	this->nbThread = nbThreadGroup * nbThreadPerGroup;
	this->maxFound = maxFound;
	this->outputSize = (maxFound * ITEM_SIZE_A + 4);

	char tmp[512];
	sprintf(tmp, "GPU #%d %s (%dx%d cores) Grid(%dx%d)",
		gpuId, deviceProp.name, deviceProp.multiProcessorCount,
		_ConvertSMVer2Cores(deviceProp.major, deviceProp.minor),
		nbThread / nbThreadPerGroup,
		nbThreadPerGroup);
	deviceName = std::string(tmp);

	// Prefer L1 (We do not use __shared__ at all)
	CudaSafeCall(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

	size_t stackSize = 49152;
	CudaSafeCall(cudaDeviceSetLimit(cudaLimitStackSize, stackSize));

	// Allocate memory
	CudaSafeCall(cudaMalloc((void**)&inputKey, nbThread * 4 * sizeof(uint64_t)));

	CudaSafeCall(cudaMalloc((void**)&outputBuffer, outputSize));
	CudaSafeCall(cudaHostAlloc(&outputBufferPinned, outputSize, cudaHostAllocWriteCombined | cudaHostAllocMapped));

	int K_SIZE = 5;

	CudaSafeCall(cudaMalloc((void**)&inputHash, numHash160 * K_SIZE * sizeof(uint32_t)));
	CudaSafeCall(cudaHostAlloc(&inputHashPinned, numHash160 * K_SIZE * sizeof(uint32_t), cudaHostAllocWriteCombined | cudaHostAllocMapped));

	memcpy(inputHashPinned, hash160, numHash160 * K_SIZE * sizeof(uint32_t));

	CudaSafeCall(cudaMemcpy(inputHash, inputHashPinned, numHash160 * K_SIZE * sizeof(uint32_t), cudaMemcpyHostToDevice));
	CudaSafeCall(cudaFreeHost(inputHashPinned));
	inputHashPinned = NULL;

	// Create a stream for non-blocking operations
	CudaSafeCall(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
	
	// Skip cuRAND initialization which seems to be causing hangs
	// Just use simple randomize
	
	// Store key range if provided
	if (!startKeyHex.empty() && !endKeyHex.empty()) {
		uint64_t host_start_key[4], host_end_key[4], host_range_span[4];
		if (HostBN_HexToU64Array(startKeyHex, host_start_key) && 
			HostBN_HexToU64Array(endKeyHex, host_end_key)) {
			
			if (HostBN_Sub(host_range_span, host_end_key, host_start_key) == 1) { // if end < start (borrow occurred)
				printf("GPUEngine Error: End key must be greater than or equal to start key.\n");
				// For now, proceed without range.
				this->use_range_ = false;
			} else {
				HostBN_AddOneInplace(host_range_span); // range_span = end - start + 1

				CudaSafeCall(cudaMalloc((void**)&dev_start_key_, 4 * sizeof(uint64_t)));
				CudaSafeCall(cudaMemcpy(dev_start_key_, host_start_key, 4 * sizeof(uint64_t), cudaMemcpyHostToDevice));

				CudaSafeCall(cudaMalloc((void**)&dev_range_span_, 4 * sizeof(uint64_t)));
				CudaSafeCall(cudaMemcpy(dev_range_span_, host_range_span, 4 * sizeof(uint64_t), cudaMemcpyHostToDevice));
				
				this->use_range_ = true;
				printf("GPUEngine: Using key range %s to %s\n", startKeyHex.c_str(), endKeyHex.c_str());
			}
		} else {
			printf("GPUEngine Warning: Invalid hex string for range.Proceeding without range.\n");
			this->use_range_ = false;
		}
	}

	// Initialize with simple random data instead of using cuRAND
	Randomize();

	CudaSafeCall(cudaGetLastError());
	initialised = true;
}

// ----------------------------------------------------------------------------

int GPUEngine::GetGroupSize()
{
	return GRP_SIZE;
}

// ----------------------------------------------------------------------------

void GPUEngine::PrintCudaInfo()
{
	const char* sComputeMode[] = {
		"Multiple host threads",
		"Only one host thread",
		"No host thread",
		"Multiple process threads",
		"Unknown",
		NULL
	};

	int deviceCount = 0;
	CudaSafeCall(cudaGetDeviceCount(&deviceCount));

	// This function call returns 0 if there are no CUDA capable devices.
	if (deviceCount == 0) {
		printf("GPUEngine: There are no available device(s) that support CUDA\n");
		return;
	}

	for (int i = 0; i < deviceCount; i++) {
		CudaSafeCall(cudaSetDevice(i));
		cudaDeviceProp deviceProp;
		CudaSafeCall(cudaGetDeviceProperties(&deviceProp, i));
		printf("GPU #%d %s (%dx%d cores) (Cap %d.%d) (%.1f MB) (%s)\n",
			i, deviceProp.name, deviceProp.multiProcessorCount,
			_ConvertSMVer2Cores(deviceProp.major, deviceProp.minor),
			deviceProp.major, deviceProp.minor, (double)deviceProp.totalGlobalMem / 1048576.0,
			sComputeMode[deviceProp.computeMode]);
	}
}

// ----------------------------------------------------------------------------

GPUEngine::~GPUEngine()
{
	CudaSafeCall(cudaFree(inputKey));
	CudaSafeCall(cudaFree(inputHash));

	CudaSafeCall(cudaFreeHost(outputBufferPinned));
	CudaSafeCall(cudaFree(outputBuffer));

	CudaSafeCall(cudaStreamDestroy(stream));

	if (use_range_) {
		CudaSafeCall(cudaFree(dev_start_key_));
		CudaSafeCall(cudaFree(dev_range_span_));
	}
	
	// Free cuRAND states if allocated
	if (dev_rand_states_ != nullptr) {
		CudaSafeCall(cudaFree(dev_rand_states_));
		dev_rand_states_ = nullptr;
	}
}

// ----------------------------------------------------------------------------

int GPUEngine::GetNbThread()
{
	return nbThread;
}

// ----------------------------------------------------------------------------

bool GPUEngine::CallKernel()
{

	// Reset nbFound
	CudaSafeCall(cudaMemset(outputBuffer, 0, 4));

	// Call the kernel (Perform STEP_SIZE keys per thread) 
	compute_hash << < nbThread / nbThreadPerGroup, nbThreadPerGroup >> >
		(inputKey, inputHash, numHash160, maxFound, outputBuffer);

	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess) {
		printf("GPUEngine: callKernel: %s\n", cudaGetErrorString(err));
		return false;
	}
	return true;

}

// ----------------------------------------------------------------------------

bool GPUEngine::Step(std::vector<ITEM>& dataFound, bool spinWait)
{
	dataFound.clear();
	bool ret = true;

	ret = Randomize();

	ret = CallKernel();

	// Get the result
	if (spinWait) {
		CudaSafeCall(cudaMemcpy(outputBufferPinned, outputBuffer, outputSize, cudaMemcpyDeviceToHost));
	}
	else {
		// Use cudaMemcpyAsync to avoid default spin wait of cudaMemcpy wich takes 100% CPU
		cudaEvent_t evt;
		CudaSafeCall(cudaEventCreate(&evt));
		CudaSafeCall(cudaMemcpyAsync(outputBufferPinned, outputBuffer, 4, cudaMemcpyDeviceToHost, 0));
		CudaSafeCall(cudaEventRecord(evt, 0));
		while (cudaEventQuery(evt) == cudaErrorNotReady) {
			// Sleep 1 ms to free the CPU
			Timer::SleepMillis(1);
		}
		CudaSafeCall(cudaEventDestroy(evt));
	}

	// Look for found
	uint32_t nbFound = outputBufferPinned[0];
	if (nbFound > maxFound) {
		nbFound = maxFound;
	}

	// When can perform a standard copy, the kernel is eneded
	CudaSafeCall(cudaMemcpy(outputBufferPinned, outputBuffer, nbFound * ITEM_SIZE_A + 4, cudaMemcpyDeviceToHost));

	for (uint32_t i = 0; i < nbFound; i++) {
		uint32_t* itemPtr = outputBufferPinned + (i * ITEM_SIZE_A32 + 1);
		ITEM it;
		it.thId = itemPtr[0];
		it.pubKey = (uint8_t*)(itemPtr + 1);
		it.hash160 = (uint8_t*)(itemPtr + 10);
		dataFound.push_back(it);
	}

	return ret;
}

// ----------------------------------------------------------------------------

bool GPUEngine::Randomize()
{
	// Properly use the range information for key generation
	if (use_range_) {
		// Initialize cuRAND states if not already initialized
		if (dev_rand_states_ == nullptr) {
			printf("Initializing cuRAND states for range-based search...\n");
			
			// Allocate memory for cuRAND states, one per thread
			CudaSafeCall(cudaMalloc((void**)&dev_rand_states_, nbThread * sizeof(curandStatePhilox4_32_10_t)));
			
			// Initialize cuRAND states with current time as seed
			unsigned long long seed = (unsigned long long)std::time(0);
			int threadsPerBlock = 256;
			int blocks = (nbThread + threadsPerBlock - 1) / threadsPerBlock;
			
			init_curand_states_kernel<<<blocks, threadsPerBlock>>>(
				dev_rand_states_, seed, nbThread);
			
			CudaSafeCall(cudaDeviceSynchronize());
			CudaSafeCall(cudaGetLastError());
		}
		
		// Use the generate_keys_in_range_kernel to generate keys within the specified range
		int threadsPerBlock = 256;
		int blocks = (nbThread + threadsPerBlock - 1) / threadsPerBlock;
		
		generate_keys_in_range_kernel<<<blocks, threadsPerBlock>>>(
			inputKey, dev_rand_states_, dev_start_key_, dev_range_span_, nbThread);
		
		CudaSafeCall(cudaDeviceSynchronize());
		CudaSafeCall(cudaGetLastError());
		
		return true;
	} 
	else {
		// Fall back to the original simple randomization when no range is specified
		CudaSafeCall(cudaMemset(inputKey, 0, nbThread * 4 * sizeof(uint64_t)));
		
		uint64_t* hostKeys = new uint64_t[nbThread * 4];
		for (int i = 0; i < nbThread * 4; i++) {
			uint64_t seed = std::time(0) ^ (i << 8);
			hostKeys[i] = seed;
		}
		
		CudaSafeCall(cudaMemcpy(inputKey, hostKeys, nbThread * 4 * sizeof(uint64_t), cudaMemcpyHostToDevice));
		delete[] hostKeys;
		
		return true;
	}
}

// ----------------------------------------------------------------------------

// Helper function to convert hex char to int
__host__ int hex_char_to_int(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// Helper function: Host-side 256-bit hex string to uint64_t[4]
__host__ bool HostBN_HexToU64Array(const std::string& hex, uint64_t arr[4]) {
	if (hex.length() != 64) return false; // 256 bits = 32 bytes = 64 hex chars
	for (int i = 0; i < 4; ++i) arr[i] = 0;
	for (int i = 0; i < 4; ++i) { // Each u64 is 16 hex chars
		for (int j = 0; j < 16; ++j) {
			char c = hex[i * 16 + j];
			int val = hex_char_to_int(c);
			if (val == -1) return false;
			arr[3-i] |= (uint64_t)val << ((15 - j) * 4); // Fill from MSB u64 down, MSB char first
		}
	}
	// The above loop fills arr[3] with MSB ... arr[0] with LSB of the 256-bit number
	// Let's adjust to standard little-endian for u64 words (arr[0]=LSW, arr[3]=MSW)
	uint64_t temp_arr[4];
	for (int i = 0; i < 4; ++i) temp_arr[i] = 0;
	int hex_idx = 63;
	for(int arr_idx = 0; arr_idx < 4; ++arr_idx) { // For each uint64_t in the array (LSW to MSW)
		for(int char_idx = 0; char_idx < 16; ++char_idx) { // For each hex char in the uint64_t
			if(hex_idx < 0) break;
			int val = hex_char_to_int(hex[hex_idx--]);
			if(val == -1) return false;
			temp_arr[arr_idx] |= (uint64_t)val << (char_idx * 4);
		}
	}
	memcpy(arr, temp_arr, 4 * sizeof(uint64_t));
	return true;
}

// Host-side 256-bit subtraction: r = a - b. Returns borrow.
__host__ uint64_t HostBN_Sub(uint64_t r[4], const uint64_t a[4], const uint64_t b[4]) {
	uint64_t borrow = 0;
	for (int i = 0; i < 4; ++i) {
		uint64_t temp = a[i] - borrow;
		borrow = (a[i] < borrow); // Borrow from previous subtraction
		borrow += (temp < b[i]);  // Borrow for current subtraction
		r[i] = temp - b[i];
	}
	return borrow; // 1 if a < b, 0 otherwise
}

// Host-side 256-bit addition of 1: r = r + 1. Returns carry.
__host__ uint64_t HostBN_AddOneInplace(uint64_t r[4]) {
	uint64_t carry = 1;
	for (int i = 0; i < 4 && carry; ++i) {
		uint64_t old_val = r[i];
		r[i] += carry;
		if (r[i] < old_val) carry = 1; // overflow
		else carry = 0;
	}
	return carry;
}

// Device kernel to initialize cuRAND states
__global__ void init_curand_states_kernel(curandStatePhilox4_32_10_t *states, unsigned long long seed, int num_states) {
	int tid = blockIdx.x * blockDim.x + threadIdx.x;
	if (tid < num_states) {
		curand_init(seed, tid, 0, &states[tid]);
	}
}

// Device function for 256-bit random number (fills r with 4 uint64_t)
// Uses Philox, which is good for parallel PRNG.
__device__ void DeviceBN_GetRandom256(curandStatePhilox4_32_10_t *state, uint64_t r[4]) {
	// Use curand to generate random 32-bit values and combine them into 64-bit
	uint4 r1, r2;
	// Use curand4 to generate 4 random 32-bit values at once
	r1.x = curand(state);
	r1.y = curand(state);
	r1.z = curand(state);
	r1.w = curand(state);
	r2.x = curand(state);
	r2.y = curand(state);
	r2.z = curand(state);
	r2.w = curand(state);
	
	// Combine 32-bit values into 64-bit values
	r[0] = ((uint64_t)r1.y << 32) | r1.x;
	r[1] = ((uint64_t)r1.w << 32) | r1.z;
	r[2] = ((uint64_t)r2.y << 32) | r2.x;
	r[3] = ((uint64_t)r2.w << 32) | r2.z;
}

// Device function for 256-bit addition: r = a + b (uint64_t[4])
// Uses PTX assembly macros for efficient carry handling.
__device__ void DeviceBN_Add256(uint64_t r[4], const uint64_t a[4], const uint64_t b[4]) {
	asm volatile ("add.cc.u64 %0, %1, %2;" : "=l"(r[0]) : "l"(a[0]), "l"(b[0]));
	asm volatile ("addc.cc.u64 %0, %1, %2;" : "=l"(r[1]) : "l"(a[1]), "l"(b[1]));
	asm volatile ("addc.cc.u64 %0, %1, %2;" : "=l"(r[2]) : "l"(a[2]), "l"(b[2]));
	asm volatile ("addc.u64 %0, %1, %2;" : "=l"(r[3]) : "l"(a[3]), "l"(b[3]));
}

// Device function for 256-bit subtraction: r = a - b. Returns borrow out of MSB.
// r = a - b. Returns 1 if a < b (borrow needed from MSB), 0 otherwise.
__device__ uint64_t DeviceBN_Sub256(uint64_t r[4], const uint64_t a[4], const uint64_t b[4]) {
	uint64_t borrow_out;
	asm volatile ("sub.cc.u64 %0, %1, %2;" : "=l"(r[0]) : "l"(a[0]), "l"(b[0]));
	asm volatile ("subc.cc.u64 %0, %1, %2;" : "=l"(r[1]) : "l"(a[1]), "l"(b[1]));
	asm volatile ("subc.cc.u64 %0, %1, %2;" : "=l"(r[2]) : "l"(a[2]), "l"(b[2]));
	asm volatile ("subc.u64 %0, %1, %2;" : "=l"(r[3]) : "l"(a[3]), "l"(b[3]));
	// Check final carry/borrow flag. This is tricky with only PTX subc.
	// A common way is to check if r > a after subtraction when b is non-zero.
	// Or, more directly, the carry flag from the last subc.u64 can be captured.
	// For simplicity, let's assume a full comparison for borrow check after subtraction, or rely on host side pre-check.
	// A true borrow out for a > b would mean r[3] (MSB) indicates this. 
	// If a < b, then the result will wrap around. 
	// The host side HostBN_Sub already checks for a < b via its return.
	// On device, we might need to check if any intermediate a[i]-borrow < b[i]. 
	// A simpler check: if (a < b) then (a-b) will be (MAX_UINT256 - (b-a) + 1)
	// For rejection sampling, we mainly need comparison, not the result of (R - M_span)
	// So, this Sub256 might not be directly used by rejection sampling, but comparison will.
	// Let's leave this as a standard subtract for now.
	// To get the borrow out (a < b?): compare a with b directly.
	// This function is just a - b, the borrow logic is separate for comparison.
	return 0; // Placeholder for borrow out, proper check is involved.
}

// Device function for 256-bit comparison: returns true if a >= b, false otherwise.
__device__ bool DeviceBN_IsGreaterOrEqual256(const uint64_t a[4], const uint64_t b[4]) {
	// Compare from Most Significant Word to Least Significant Word
	if (a[3] > b[3]) return true;
	if (a[3] < b[3]) return false;
	// a[3] == b[3]
	if (a[2] > b[2]) return true;
	if (a[2] < b[2]) return false;
	// a[2] == b[2]
	if (a[1] > b[1]) return true;
	if (a[1] < b[1]) return false;
	// a[1] == b[1]
	return (a[0] >= b[0]);
}

// Kernel to generate keys in a specified range using rejection sampling
__global__ void generate_keys_in_range_kernel(
	uint64_t* output_keys, 
	curandStatePhilox4_32_10_t* states,
	const uint64_t* dev_start_key,  
	const uint64_t* dev_range_span, 
	int num_keys_to_generate
) {
	int tid = blockIdx.x * blockDim.x + threadIdx.x;
	if (tid >= num_keys_to_generate) return;

	uint64_t random_val_256bit[4];
	uint64_t final_key_256bit[4];

	// Loop for rejection sampling
	// We need to ensure dev_range_span is not zero if it's used as an upper bound for random_val.
	// The span is (end - start + 1), so it should be at least 1.
	// If span is 0 (e.g. start > end, which should be caught by host), this loop is problematic.
	bool span_is_zero = true;
	for(int i=0; i<4; ++i) if(dev_range_span[i] != 0) {span_is_zero = false; break;}
	if(span_is_zero) { // Should not happen if host logic is correct (range_span >= 1)
		// Default to start_key or handle error
		for(int i=0; i<4; ++i) final_key_256bit[i] = dev_start_key[i];
	} else {
		do {
			DeviceBN_GetRandom256(&states[tid], random_val_256bit);
			// Generate R in [0, 2^256 - 1]. We want R in [0, dev_range_span - 1].
			// If random_val_256bit >= dev_range_span, regenerate.
		} while (DeviceBN_IsGreaterOrEqual256(random_val_256bit, dev_range_span));
		// Now, random_val_256bit is in the range [0, dev_range_span - 1]
		DeviceBN_Add256(final_key_256bit, dev_start_key, random_val_256bit);
	}

	uint64_t* key_ptr = output_keys + (tid * 4);
	key_ptr[0] = final_key_256bit[0];
	key_ptr[1] = final_key_256bit[1];
	key_ptr[2] = final_key_256bit[2];
	key_ptr[3] = final_key_256bit[3];
}

// ----------------------------------------------------------------------------

