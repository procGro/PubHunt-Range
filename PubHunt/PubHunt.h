/*
 * This file is part of the PubHunt distribution (https://github.com/kanhavishva/PubHunt).
 * Copyright (c) 2021 KV.
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

#ifndef PUBHUNT_H
#define PUBHUNT_H

#include <string>
#include <vector>
#include <cstdint> // For uint64_t
#include <mutex>   // For std::mutex
#include "ThreadPool.h" // For ThreadPool (if it's a local header)
#include "Logger.h" // Assuming Logger is used

#ifdef WITHGPU
// #include "GPUEngine.h" // For ITEM struct and MAX_GPUS
#include "GPU/GPUEngine.h" // For ITEM struct and MAX_GPUS
#else
// Define MAX_GPUS if not defined WITHGPU, or ensure it's defined elsewhere if used unconditionally
#ifndef MAX_GPUS
#define MAX_GPUS 1 // Or a suitable default
#endif
#ifndef ITEM // Define a dummy ITEM if output is not fully conditionalized in cpp
struct ITEM { uint64_t x; uint64_t y; /* simplified */ };
#endif
#endif

#ifdef WIN64
#include <Windows.h>
#endif
#include "IntGroup.h"
#include "Timer.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cassert>
#ifndef WIN64
#include <pthread.h>
#endif
#include <stdint.h>

#define CPU_GRP_SIZE (1024*2)

class PubHunt;

// Removed TH_PARAM as it's part of the old threading model
// typedef struct TH_PARAM {
// 	int threadId;
// 	PubHunt* obj;
// 	bool isRunning;
// 	bool hasStarted;
// 	int gpuId;      // Used by GPUEngine
// 	int gridSizeX;  // Used by GPUEngine
// 	int gridSizeY;  // Used by GPUEngine
// } TH_PARAM;

class PubHunt
{

public:

	PubHunt(const std::vector<std::string>& targets, int numThreads, int generationMode, const std::string& deviceNames,
			bool useRange, const std::string& startKeyHex, const std::string& endKeyHex);

	~PubHunt();

	void search();
	void stop();
	bool isRunning() const;
	uint64_t getTotalHashes() const;
	double getSpeed() const;

#ifdef WITHGPU
	unsigned int getDeviceCount() const;
	std::string getDeviceName(unsigned int n) const;
	uint64_t getDeviceTotalHashes(unsigned int n) const;
	double getDeviceSpeed(unsigned int n) const;
	void output(const ITEM& item);
#endif

	unsigned int getNumThreads() const;

	// Public instance members for thread status
	bool isAlive[128];   // Assuming max 128 threads/devices, adjust if MAX_GPUS is better
	bool hasStarted[128];

private:

	// Utility methods implemented in PubHunt.cpp
	static std::string formatThousands(uint64_t n);
	static char* toTimeStr(int sec, char* timeStr); // timeStr buffer should be managed by caller

	void workThread(int threadId, const std::string& deviceName);
#ifdef WITHGPU
	void FindKeyGPU(int threadId, const std::string& deviceName);
#endif
	void FindKeyCPU(int threadId);

	std::vector<std::string> _targets;
	int _numThreads;
	int _generationMode; // 0 for random, 1 for ordered (not implemented fully yet for PubHunt style)
	std::string _deviceNames; // Comma separated list of devices for GPU, or "cpu"

	bool _use_range;
	std::string _start_key_hex;
	std::string _end_key_hex;

	std::vector<uint64_t> _deviceTotalHashes;
	std::vector<double> _deviceSpeeds;
	std::vector<std::string> _deviceNamesList; // Parsed from _deviceNames
#ifdef WITHGPU
	std::vector<GPUEngine*> _gpuEngines;
#endif
	unsigned int _deviceCount;

	bool _running;
	bool _stopped;
	uint64_t _totalHashes;
	double _startTime;
	std::mutex _mutex;
	ThreadPool* _pool;
	Logger* _logger;

};

#endif // PUBHUNT_H
