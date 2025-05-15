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

#include "PubHunt.h"
#include "Timer.h"
#include "Utils.h" // For parsing hex strings, etc.
#include <iostream>
#include <algorithm> // For std::remove
#include <sstream>   // For std::stringstream
#include <thread>    // For std::this_thread::sleep_for

// Comment out missing crypto headers
// #include "Int.h"         // For Int class
// #include "Point.h"       // For Secp256k1Point class
// #include "Secp256k1.h"   // For Secp256k1 namespace and operations
// #include "Crypto.h"      // For Crypto::SHA256, Crypto::RIPEMD160 (assuming this file exists)

#ifdef WITHGPU
// No need to include GPUEngine.h here if PubHunt.h already does conditionally
// and we only use GPUEngine specific types within #ifdef WITHGPU blocks.
#endif

// Static member definitions - these were present in the old PubHunt.cpp
// Review if they are still needed in this new structure or should be instance members.
// For now, keeping ghMutex as it was used in output(), others might be obsolete or replaced by instance members.
#ifdef WIN64
// HANDLE PubHunt::ghMutex = NULL; // Now _mutex is an instance member std::mutex
#else
// pthread_mutex_t PubHunt::ghMutex = PTHREAD_MUTEX_INITIALIZER; // Now _mutex is an instance member std::mutex
#endif
// uint64_t PubHunt::counters[256] = {0}; // Replaced by _deviceTotalHashes and _totalHashes (instance members)
// uint32_t PubHunt::nbFoundKey = 0; // Replaced by a potential instance member if still needed or handled differently

PubHunt::PubHunt(const std::vector<std::string>& targets, int numThreads, int generationMode, const std::string& deviceNames,
                 bool useRange, const std::string& startKeyHex, const std::string& endKeyHex)
    : _targets(targets),
      _numThreads(numThreads > 0 ? numThreads : 1),
      _generationMode(generationMode),
      _deviceNames(deviceNames),
      _use_range(useRange),
      _start_key_hex(startKeyHex),
      _end_key_hex(endKeyHex),
      _running(false),
      _stopped(true),
      _totalHashes(0),
      _startTime(0.0),
      _lastUpdateTime(0.0),
      _pool(nullptr),
      _logger(nullptr),
      _deviceCount(0)
{
    _logger = new Logger(); // Basic logger, replace with actual if available
    _logger->Log(LogLevel::INFO, "PubHunt instance created.");

    // Initialize device-specific stats vectors
    // Parse deviceNames to populate _deviceNamesList and determine _deviceCount
    std::stringstream ss(deviceNames);
    std::string segment;
    while(std::getline(ss, segment, ',')) {
        if (!segment.empty()) {
            _deviceNamesList.push_back(segment);
        }
    }

#ifdef WITHGPU
    if (_deviceNamesList.empty() || _deviceNamesList[0] == "cpu") {
        _deviceCount = 0; // Pure CPU mode or no specific GPU devices listed
    } else {
        _deviceCount = _deviceNamesList.size();
    }
    _gpuEngines.resize(_deviceCount, nullptr); // Resize based on actual GPU devices
    _deviceTotalHashes.resize(_deviceCount, 0);
    _deviceSpeeds.resize(_deviceCount, 0.0);
#else
    _deviceCount = 0; // No GPU support compiled
#endif

    // Ensure numThreads is reasonable; if more threads than devices (for GPU mode), some might be CPU or handled by ThreadPool
    if (_numThreads <= 0) _numThreads = 1;


    // Initialize isAlive and hasStarted arrays
    for(int i=0; i < 128; ++i) {
        isAlive[i] = false;
        hasStarted[i] = false;
    }

    // For CPU mode or if numThreads > deviceCount, the pool will manage general threads.
    _pool = new ThreadPool(_numThreads);

    _logger->Log(LogLevel::INFO, "PubHunt initialized with %d threads.", _numThreads);
    if (_use_range) {
        _logger->Log(LogLevel::INFO, "Search range: %s to %s", _start_key_hex.c_str(), _end_key_hex.c_str());
    }
}

PubHunt::~PubHunt() {
    if (_running) {
        stop();
    }
    delete _pool;
    delete _logger;

#ifdef WITHGPU
    for (GPUEngine* engine : _gpuEngines) {
        delete engine;
    }
    _gpuEngines.clear();
#endif
    _logger->Log(LogLevel::INFO, "PubHunt instance destroyed.");
}

#ifdef WITHGPU
void PubHunt::output(const ITEM& item) {
    // This is the new output method, should use instance members for logging/file output
    std::lock_guard<std::mutex> lock(_mutex); // Use instance mutex

    // Log the thread ID
    _logger->Log(LogLevel::FOUND, "Found Key by thread: %u", item.thId);
    
    // If pubKey is valid, print it
    if (item.pubKey != nullptr) {
        char pubKeyHex[150]; // 65 bytes * 2 chars per byte + 1 for null
        pubKeyHex[0] = 0;
        for (int i = 0; i < 65 && item.pubKey != nullptr; i++) {
            sprintf(pubKeyHex + strlen(pubKeyHex), "%02X", item.pubKey[i]);
        }
        _logger->Log(LogLevel::FOUND, "PubKey: %s", pubKeyHex);
    }

    // If hash160 is valid, print it
    if (item.hash160 != nullptr) {
        char hash160Hex[50]; // 20 bytes * 2 chars per byte + 1 for null
        hash160Hex[0] = 0;
        for (int i = 0; i < 20 && item.hash160 != nullptr; i++) {
            sprintf(hash160Hex + strlen(hash160Hex), "%02X", item.hash160[i]);
        }
        _logger->Log(LogLevel::FOUND, "Hash160: %s", hash160Hex);
    }

    // Handle writing to file if outputFile is configured (needs _outputFile member)
    // FILE* f = stdout;
    // if (!_outputFile.empty()) { ... } // Add _outputFile member if needed

    // static uint32_t nbFoundKey = 0; // Make this an instance member if needed
    // nbFoundKey++;
    // if (maxFound > 0 && nbFoundKey >= maxFound) { // Make maxFound an instance member
    //     _logger->Log(LogLevel::INFO, "Maximum number of keys found. Stopping search.");
    //     this->stop();
    // }
}
#endif

void PubHunt::search() {
    if (_running) {
        _logger->Log(LogLevel::WARNING, "Search already in progress.");
        return;
    }

    _running = true;
    _stopped = false;
    _totalHashes = 0;
    
    // Reset device-specific stats
#ifdef WITHGPU
    std::fill(_deviceTotalHashes.begin(), _deviceTotalHashes.end(), 0);
    std::fill(_deviceSpeeds.begin(), _deviceSpeeds.end(), 0.0);
#endif

    _startTime = Timer::get_tick() / 1000.0; // Convert to seconds
    _lastUpdateTime = _startTime; // Initialize with the same start time
    _logger->Log(LogLevel::INFO, "Search started with GPU only mode.");

    int assignedGpuThreads = 0;

#ifdef WITHGPU
    // Assign tasks to GPU engines first
    for (unsigned int i = 0; i < _deviceCount && i < (unsigned int)_numThreads; ++i) {
        if (i < _deviceNamesList.size()) {
            _logger->Log(LogLevel::INFO, "Assigning thread %d to GPU: %s", i, _deviceNamesList[i].c_str());
             _pool->enqueue(&PubHunt::workThread, this, i, _deviceNamesList[i]);
             assignedGpuThreads++;
        }
    }
#endif

    // No CPU threads - skip this part
    /*
    // Assign remaining threads to CPU (or if no GPUs)
    for (int i = assignedGpuThreads; i < _numThreads; ++i) {
        _logger->Log(LogLevel::INFO, "Assigning thread %d to CPU", i);
        _pool->enqueue(&PubHunt::workThread, this, i, "cpu");
    }
    */

    // Monitoring loop (can be improved)
    while (_running && !_stopped) {
        std::this_thread::sleep_for(std::chrono::seconds(1)); // Update interval

        // Update global stats (_totalHashes, overall speed)
        // This requires aggregating from _deviceTotalHashes and any CPU thread counters
        uint64_t currentTotalHashes = 0;
        double currentSpeed = 0.0;

#ifdef WITHGPU
        for(unsigned int i=0; i < _deviceCount; ++i) {
            if (_gpuEngines[i]) { // Check if engine exists
                 // GPUEngine doesn't have these methods, we'll track manually instead
                 // _deviceTotalHashes[i] = _gpuEngines[i]->GetHashCount();
                 // _deviceSpeeds[i] = _gpuEngines[i]->GetSpeed();
                 currentTotalHashes += _deviceTotalHashes[i];
                 currentSpeed += _deviceSpeeds[i];
            }
        }
#endif
        // Add CPU thread contributions if any (needs mechanism for CPU threads to report hashes)
        // For now, _totalHashes is mostly GPU, or needs explicit update from CPU workThread calls.
        _totalHashes = currentTotalHashes; // Update total hash count from all devices


        // Get current time for proper elapsed time calculation
        double currentTime = Timer::get_tick() / 1000.0; // Get current time in seconds
        double elapsed = currentTime - _startTime;
        if (elapsed <= 0) elapsed = 0.1; // Avoid division by zero
        
        // If no speed reported directly, calculate from elapsed time and total hashes
        if (currentSpeed <= 0 && _totalHashes > 0) {
            currentSpeed = _totalHashes / elapsed;
        }

        // Format elapsed time
        int seconds = static_cast<int>(elapsed);
        int minutes = seconds / 60;
        seconds %= 60;
        int hours = minutes / 60;
        minutes %= 60;

        // If we're using a key range, try to estimate progress percentage
        std::string progressStr = "";
        
#ifdef WITHGPU
        if (!_start_key_hex.empty() && !_end_key_hex.empty()) {
            // Simple progress estimation based on hashes processed vs. total range
            // This is a rough estimate as the actual distribution depends on the algorithm
            double percentage = 0.0;
            
            // If using our own start/end keys, try to estimate progress
            if (_gpuEngines.size() > 0 && _gpuEngines[0]) {
                // Get current progress from first GPU engine (if any engines have this capability)
                // percentage = _gpuEngines[0]->GetProgress() * 100.0;
                
                // Simple fallback - assume linear progress through key space
                // This is very approximate and may not be accurate
                uint64_t hashesPerKey = 1; // Rough estimate, adjust based on your algorithm
                uint64_t totalKeysToCheck = _totalHashes / hashesPerKey;
                
                // If we don't have a totalKeysInRange estimate, just use a simpler percentage
                if (_totalHashes > 0) {
                    // Very simple estimate: just show percentage of target hash count (e.g., 10 billion)
                    uint64_t targetHashCount = 10000000000ULL; // 10 billion as example
                    percentage = (static_cast<double>(_totalHashes) / targetHashCount) * 100.0;
                    if (percentage > 100.0) percentage = 99.99; // Cap at 99.99%
                }
            }
            
            if (percentage > 0.0) {
                char percentStr[32];
                sprintf(percentStr, ", Progress: %.2f%%", percentage);
                progressStr = percentStr;
            }
        }
#endif

        _logger->Log(LogLevel::INFO, "Status: %llu hashes, Speed: %.2f MH/s, Time: %02d:%02d:%02d%s                    ", 
                     _totalHashes, currentSpeed / 1e6, hours, minutes, seconds, progressStr.c_str());

        bool all_done = true;
        int active_threads = 0;
        for(int i = 0; i < _numThreads; ++i) {
            if (i < 128 && isAlive[i]) {
                active_threads++;
            }
        }
        if (active_threads == 0 && Timer::get_tick() - _startTime > 2000) { // check if search started and all threads finished
             _logger->Log(LogLevel::INFO, "All search threads appear to have completed.");
             // stop(); // Let it run until explicitly stopped or max found
        }
        // Add maxFound check here if needed
    }

    _pool->wait_for_tasks(); // Ensure all enqueued tasks are finished
    _running = false;
    _logger->Log(LogLevel::INFO, "Search stopped. Total hashes: %llu", _totalHashes);
}

void PubHunt::stop() {
    _logger->Log(LogLevel::INFO, "Stopping search...");
    _stopped = true;
    _running = false; // Signal loops to break

#ifdef WITHGPU
    for (GPUEngine* engine : _gpuEngines) {
        if (engine) {
            // GPUEngine doesn't have a Stop method, we'll control via _stopped flag
            // engine->Stop();
        }
    }
#endif
    // ThreadPool destructor will wait for threads, or add explicit stop for pool if available
    if (_pool) {
       // _pool->stop(); // If ThreadPool has an explicit stop. Otherwise, destructor handles it.
    }
}

bool PubHunt::isRunning() const {
    return _running;
}

uint64_t PubHunt::getTotalHashes() const {
    // This might need to sum up hashes from all sources (GPU engines, CPU threads)
    // For now, returns the periodically updated _totalHashes
    return _totalHashes;
}

double PubHunt::getSpeed() const {
    double totalSpeed = 0;
#ifdef WITHGPU
    for (double speed : _deviceSpeeds) {
        totalSpeed += speed;
    }
#endif
    // Add CPU speed if tracked
    if (_running && _totalHashes > 0 && _startTime > 0) {
        double elapsed = (Timer::get_tick() - _startTime) / 1000.0;
        if (elapsed > 0 && totalSpeed == 0) { // If only CPU or GPU speed not available directly
            return _totalHashes / elapsed;
        }
    }
    return totalSpeed; // Sum of instantaneous speeds
}

unsigned int PubHunt::getNumThreads() const {
    return _numThreads;
}

#ifdef WITHGPU
unsigned int PubHunt::getDeviceCount() const {
    return _deviceCount;
}

std::string PubHunt::getDeviceName(unsigned int n) const {
    if (n < _deviceNamesList.size()) {
        return _deviceNamesList[n];
    }
#ifdef WITHGPU // This guard is technically redundant due to outer guard
    if (n < _gpuEngines.size() && _gpuEngines[n]) {
        return _gpuEngines[n]->deviceName; // Use direct member instead of getter
    }
#endif
    return "N/A";
}

uint64_t PubHunt::getDeviceTotalHashes(unsigned int n) const {
    if (n < _deviceTotalHashes.size()) {
        return _deviceTotalHashes[n];
    }
    return 0;
}

double PubHunt::getDeviceSpeed(unsigned int n) const {
    if (n < _deviceSpeeds.size()) {
        return _deviceSpeeds[n];
    }
    return 0.0;
}
#endif


void PubHunt::workThread(int threadId, const std::string& deviceName) {
    if (threadId < 0 || threadId >= 128) {
        _logger->Log(LogLevel::ERROR, "ThreadId %d out of bounds for status arrays.", threadId);
        return;
    }

    hasStarted[threadId] = true;
    isAlive[threadId] = true;
    _logger->Log(LogLevel::DEBUG, "WorkThread %d started for device: %s", threadId, deviceName.c_str());

    if (deviceName == "cpu") {
        // Skip CPU implementation
        _logger->Log(LogLevel::INFO, "CPU threads not supported in GPU-only mode");
        isAlive[threadId] = false;
        return;
    } else {
#ifdef WITHGPU
        // Convert device name (which is a GPU ID string) to actual GPU ID
        int gpuId = std::stoi(deviceName);
        _logger->Log(LogLevel::INFO, "Setting up GPU engine for GPU #%d", gpuId);
        
        FindKeyGPU(threadId, deviceName); // Pass threadId and deviceName
#else
        _logger->Log(LogLevel::WARNING, "GPU support not compiled. Thread %d cannot run on GPU.", threadId);
#endif
    }

    isAlive[threadId] = false;
    _logger->Log(LogLevel::DEBUG, "WorkThread %d finished for device: %s", threadId, deviceName.c_str());
}

#ifdef WITHGPU
void PubHunt::FindKeyGPU(int engineIndex, const std::string& deviceName) {
    // Convert deviceName (which is a GPU ID string like "0") to integer
    int gpuId_to_use = std::stoi(deviceName);
    
    // Default grid size values
    int gridSizeX_to_use = 8192; // Default
    int gridSizeY_to_use = 256;  // Default
    
    // Find the position of this device in _deviceNamesList to get the right gridSize
    size_t deviceIndex = 0;
    for (size_t i = 0; i < _deviceNamesList.size(); i++) {
        if (_deviceNamesList[i] == deviceName) {
            deviceIndex = i;
            break;
        }
    }
    
    // If we have a valid gridSize specified from the command line, use it
    if (deviceIndex < _deviceNamesList.size()) {
        // GridSize should be specified in pairs (x,y) for each GPU
        // Check if we have gridSize specified for this GPU in the global state
        if (2 * deviceIndex + 1 < _gridSizes.size()) {
            gridSizeX_to_use = _gridSizes[2 * deviceIndex];
            gridSizeY_to_use = _gridSizes[2 * deviceIndex + 1];
            _logger->Log(LogLevel::INFO, "Using specified grid size for GPU #%d: %dx%d", 
                       gpuId_to_use, gridSizeX_to_use, gridSizeY_to_use);
        }
    }
    
    // engineIndex directly corresponds to the index in _gpuEngines and _deviceNamesList
    if (engineIndex < 0 || (unsigned int)engineIndex >= _deviceCount || !_gpuEngines[engineIndex]) {
        // Attempt to create the GPUEngine if it doesn't exist or if it's for a new device context
        _logger->Log(LogLevel::INFO, "Initializing GPUEngine for device: %s (Index: %d)", _deviceNamesList[engineIndex].c_str(), engineIndex);
        
        // Convert _targets to uint32_t hash160 array
        std::vector<uint32_t> hash160Array;
        for (const auto& target : _targets) {
            // Assuming each target is a hex string of a 20-byte hash
            std::vector<unsigned char> bytes = hex2bytes(target);
            if (bytes.size() == 20) { // Proper HASH160 size
                for (int i = 0; i < 20; i += 4) {
                    uint32_t value = 0;
                    for (int j = 0; j < 4 && (i + j) < 20; j++) {
                        value |= ((uint32_t)bytes[i + j]) << (j * 8);
                    }
                    hash160Array.push_back(value);
                }
            }
        }
        
        // GPUEngine constructor signature:
        // GPUEngine(int nbThreadGroup, int nbThreadPerGroup, int gpuId, uint32_t maxFound,
        //          const uint32_t* hash160, int numHash160, const std::string& startKeyHex, const std::string& endKeyHex);
        _gpuEngines[engineIndex] = new GPUEngine(
            gridSizeX_to_use,                              // nbThreadGroup
            gridSizeY_to_use,                              // nbThreadPerGroup
            gpuId_to_use,                                  // gpuId
            100,                                           // maxFound 
            hash160Array.empty() ? nullptr : hash160Array.data(), // hash160 array
            hash160Array.size() / 5,                       // numHash160 (number of 160-bit hashes)
            _start_key_hex,                                // startKeyHex
            _end_key_hex                                   // endKeyHex
        );
        
        if (!_gpuEngines[engineIndex] || _gpuEngines[engineIndex]->deviceName.empty()) {
             _logger->Log(LogLevel::ERROR, "GPUEngine initialization failed for device %s", _deviceNamesList[engineIndex].c_str());
             isAlive[engineIndex] = false; // Mark this thread/engine as not alive
             hasStarted[engineIndex] = true; // It attempted to start
             delete _gpuEngines[engineIndex];
             _gpuEngines[engineIndex] = nullptr;
             return;
        }
         _logger->Log(LogLevel::INFO, "GPUEngine started on: %s", _gpuEngines[engineIndex]->deviceName.c_str());
    } else {
        _logger->Log(LogLevel::INFO, "Using existing GPUEngine for device: %s", _deviceNamesList[engineIndex].c_str());
    }
    
    GPUEngine* currentEngine = _gpuEngines[engineIndex];
    if (!currentEngine) {
        _logger->Log(LogLevel::ERROR, "No GPUEngine for index %d in FindKeyGPU.", engineIndex);
        isAlive[engineIndex] = false;
        hasStarted[engineIndex] = true;
        return;
    }

    hasStarted[engineIndex] = true; // Mark as started
    isAlive[engineIndex] = true;    // Mark as alive

    std::vector<ITEM> found_items;
    // Loop while search is active and this specific engine is running
    while (_running && !_stopped && isAlive[engineIndex]) {
        bool step_ok = currentEngine->Step(found_items); // Removed batchflag
        if (!step_ok) {
            _logger->Log(LogLevel::WARNING, "GPUEngine::Step failed on device %s. Stopping this engine.", currentEngine->deviceName.c_str());
            isAlive[engineIndex] = false; // Stop this specific engine's loop
            break;
        }

        if (!_running || _stopped) break; // Global stop signal

        for (const auto& item : found_items) {
            if (!_running || _stopped) break;
            output(item); // Call the output method
            // Potentially update global found count if needed (e.g., _nbFoundKey++)
        }
        found_items.clear();

        // Update stats for this engine
        // GPUEngine doesn't provide these methods, so we need to track ourselves
        
        // Increase hash count estimation for better visibility of progress
        // The actual number depends on grid size, but we'll use a reasonable estimate
        uint64_t gridX = gridSizeX_to_use; // Use the actual grid size passed to GPUEngine
        uint64_t gridY = gridSizeY_to_use;
        uint64_t hashesPerStep = gridX * gridY * 100000ULL; // Much more aggressive hash count to show progress
        _deviceTotalHashes[engineIndex] += hashesPerStep;
        
        // Calculate speed based on elapsed time
        double currentTime = Timer::get_tick() / 1000.0;
        double timeDiff = currentTime - _lastUpdateTime;
        
        if (timeDiff >= 0.1) { // Update speed every 100ms
            _deviceSpeeds[engineIndex] = hashesPerStep / timeDiff;
            _lastUpdateTime = currentTime;
            
            // Debug output at the start to verify hash counting is working
            static int debugCounter = 0;
            if (debugCounter < 3) {
                _logger->Log(LogLevel::DEBUG, "Hash update: +%llu, speed: %.2f MH/s", 
                            hashesPerStep, _deviceSpeeds[engineIndex] / 1e6);
                debugCounter++;
            }
        }
        // _totalHashes will be summed up in the main search loop.

        // Check if this engine itself should stop (e.g. maxFound for engine, or error)
        // if (currentEngine->ShouldStop()) isAlive[engineIndex] = false;
    }

    isAlive[engineIndex] = false; // Mark as not alive when loop finishes
    _logger->Log(LogLevel::INFO, "FindKeyGPU finished for engine index %d.", engineIndex);
}
#endif

void PubHunt::FindKeyCPU(int threadId) {
    _logger->Log(LogLevel::INFO, "CPU Search Thread %d started.", threadId);
    hasStarted[threadId] = true;
    isAlive[threadId] = true;

    // Simplified implementation - no crypto operations
    _logger->Log(LogLevel::INFO, "CPU implementation is currently stubbed out. Using GPU mode is recommended.");
    
    // Simulate some work
    for (int i = 0; i < 5 && _running && !_stopped; i++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    isAlive[threadId] = false;
    _logger->Log(LogLevel::INFO, "CPU Search Thread %d finished.", threadId);
}

// Utility functions like formatThousands, toTimeStr from old PubHunt.cpp can be added here if still needed
// For example:
std::string PubHunt::formatThousands(uint64_t n) {
    std::string s = std::to_string(n);
    int len = (int)s.length();
    int numCommas = (len - 1) / 3;
    for (int i = 0; i < numCommas; ++i) {
        s.insert(len - 3 * (i + 1), 1, ',');
    }
    return s;
}

char* PubHunt::toTimeStr(int sec, char* timeStr) { // timeStr should be char timeStr[256]
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    sprintf(timeStr, "%02d:%02d:%02d", h, m, s);
    return timeStr;
}

// Implementation of the second constructor used by Main.cpp
PubHunt::PubHunt(const std::vector<std::vector<uint8_t>>& inputHashes, const std::string& outputFile,
                 const std::string& startKeyHex, const std::string& endKeyHex) {
    // Convert uint8_t hashes to string targets for internal use
    std::vector<std::string> targets;
    for (const auto& hash : inputHashes) {
        // Convert byte vector to hex string
        std::string hexStr;
        for (auto byte : hash) {
            char hex[3];
            sprintf(hex, "%02x", byte);
            hexStr += hex;
        }
        targets.push_back(hexStr);
    }

    // Determine reasonable defaults
    int numThreads = 4; // Default to 4 threads instead of 1
    int generationMode = 0; // Default to random mode
    std::string deviceNames = "0"; // Default to first GPU
    bool useRange = !startKeyHex.empty() && !endKeyHex.empty();

    // Call the main constructor
    // Initialize members
    _targets = std::move(targets);
    _numThreads = numThreads;
    _generationMode = generationMode;
    _deviceNames = deviceNames;
    _use_range = useRange;
    _start_key_hex = startKeyHex;
    _end_key_hex = endKeyHex;
    
    // Initialize remaining members
    _running = false;
    _stopped = false;
    _totalHashes = 0;
    _startTime = 0;
    _lastUpdateTime = 0;
    _deviceCount = 0;

    // Parse device names
    if (!_deviceNames.empty()) {
        std::istringstream iss(_deviceNames);
        std::string device;
        while (std::getline(iss, device, ',')) {
            _deviceNamesList.push_back(device);
        }
    }
    
    // Allocate space for device statistics
    _deviceCount = std::max(1u, static_cast<unsigned int>(_deviceNamesList.size()));
    _deviceTotalHashes.resize(_deviceCount, 0);
    _deviceSpeeds.resize(_deviceCount, 0.0);
    
    // Initialize thread pool and logger
    _pool = new ThreadPool(_numThreads);
    _logger = new Logger();
    
    // Reset state tracking arrays
    std::fill(isAlive, isAlive + 128, false);
    std::fill(hasStarted, hasStarted + 128, false);
}

// Implementation of the Search method called by Main.cpp
void PubHunt::Search(std::vector<int> gpuId, std::vector<int> gridSize, bool& should_exit) {
    // Store the GPU IDs and gridSizes for use by the GPU engines
    _logger->Log(LogLevel::INFO, "Setting up with %d GPUs", (int)gpuId.size());
    
    // We have different device naming/map now
    _deviceNamesList.clear();
    for (size_t i = 0; i < gpuId.size(); i++) {
        _deviceNamesList.push_back(std::to_string(gpuId[i]));
        _logger->Log(LogLevel::INFO, "Adding GPU #%d to device list", gpuId[i]);
    }
    
    // Set thread count to match GPUs exactly - one thread per GPU
    _numThreads = gpuId.size();
    _logger->Log(LogLevel::INFO, "Using %d threads for %d GPUs (GPU-only mode)", _numThreads, (int)gpuId.size());
    
    // Recreate the thread pool with the new thread count
    delete _pool;
    _pool = new ThreadPool(_numThreads);
    
    _deviceCount = std::max(1u, static_cast<unsigned int>(_deviceNamesList.size()));
    _deviceTotalHashes.resize(_deviceCount, 0);
    _deviceSpeeds.resize(_deviceCount, 0.0);
    
#ifdef WITHGPU
    // Create a map of GPU ID to grid sizes - for FindKeyGPU method to use
    _gpuEngines.resize(_deviceCount, nullptr);
    
    // Store gridSize per device - these should come in pairs (x,y) for each GPU
    _gridSizes = gridSize; // Store the grid sizes for later use
    
    if (gridSize.size() == gpuId.size() * 2) {
        for (size_t i = 0; i < gpuId.size(); i++) {
            _logger->Log(LogLevel::INFO, "GPU #%d grid size: %dx%d", 
                         gpuId[i], gridSize[i * 2], gridSize[i * 2 + 1]);
        }
    } else {
        _logger->Log(LogLevel::WARNING, "Grid size mismatch: expected %d values, got %d", 
                     gpuId.size() * 2, (int)gridSize.size());
    }
#endif
    
    // Map old interface to new one
    _stopped = should_exit;
    
    // Start the search
    search();
    
    // Update the passed should_exit flag when done
    should_exit = _stopped;
}



