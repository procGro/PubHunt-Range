# PubHunt-Range
_Hunt for Bitcoin public keys with range support._

## What it does

PubHunt-Range searches for Bitcoin public keys corresponding to given hash160 values. It can search:
1. Randomly across the entire keyspace
2. Within a specified range of private keys (new feature)

This is useful for Bitcoin [puzzle transactions](https://www.blockchain.com/btc/tx/08389f34c98c606322740c0be6a7125d9860bb8d5cb182c02f98461e5fa6cd15).

For puzzles `64`, `66`, `67`, `68`, `69`, `71`, `72` and others without known public keys, finding the public keys enables the use of [Pollard Kangaroo](https://github.com/JeanLucPons/Kangaroo) algorithm to solve these puzzles.

## Recent Improvements

- **Key Range Support**: Adds ability to search within a specific range of private keys
- **Better Compatibility**: Fixed compilation issues and improved platform support
- **Better Memory Management**: More efficient handling of GPU resources
- **Enhanced Logging**: Improved logging for better monitoring of search progress
- **ThreadPool Implementation**: Enhanced multi-threading support for both CPU and GPU modes

## Usage

```
PubHunt [-check] [-h] [-v] 
        [-gi GPU ids: 0,1...] [-gx gridsize: g0x,g0y,g1x,g1y, ...]
        [-o outputfile] [--range <start_hex>:<end_hex>] [--bits <N>] [inputFile]

 -v                       : Print version
 -gi gpuId1,gpuId2,...    : List of GPU(s) to use, default is 0
 -gx g1x,g1y,g2x,g2y, ... : Specify GPU(s) kernel gridsize, default is 8*(MP number),128
 -o outputfile            : Output results to the specified file
 -l                       : List cuda enabled devices
 -check                   : Check Int calculations
 --range start:end        : Specify a 256-bit key range in hex (64 chars each)
 --bits N                 : Specify key range from 2^(N-1) to (2^N)-1 (N=1 to 256)
 inputFile                : List of the hash160, one per line in hex format (text mode)
```

### Examples:

#### Standard random search:
```
PubHunt -gi 0 -gx 8192,1024 hashP64.txt

PubHunt v1.00

DEVICE       : GPU
GPU IDS      : 0
GPU GRIDSIZE : 8192x1024
NUM HASH160  : 1
OUTPUT FILE  : Found.txt
GPU          : GPU #0 GeForce RTX 3080 (68x64 cores) Grid(8192x1024)

[00:01:43] [GPU: 1324.87 MH/s] [T: 54,475,620,352 (36 bit)] [F: 0]
```

#### Search within a specific key range:
```
PubHunt --range 8000000000000000000000000000000000000000000000000000000000000000:80000000000000000000000000000000000000000000000000000000ffffffff hashP64.txt
```

#### Search within a specific power-of-2 range:
```
PubHunt --bits 64 hashP64.txt
```
This searches in the range from 2^63 to 2^64-1.

## Feature Flags

### Key Range Search
Use `--range` or `--bits` to define a search range:

- `--range <start_hex>:<end_hex>`: Takes two 64-character hex values for start and end of the range
- `--bits N`: Searches in range from 2^(N-1) to (2^N)-1

### GPU Selection
- `-gi`: Specify which GPU(s) to use (zero-indexed)
- `-gx`: Customize the grid size for optimal performance on your GPUs

## Building

### Windows
- Microsoft Visual Studio Community 2019 or newer
- CUDA version 10.0 or newer

### Linux
 - Edit the makefile and set up the appropriate CUDA SDK and compiler paths for nvcc.
   ```make
   CUDA       = /usr/local/cuda-11.0
   CXXCUDA    = /usr/bin/g++
   ```
 - Build with CUDA by passing CCAP value according to your GPU compute capability:
   ```sh
   $ make CCAP=86 all    # For RTX 3080/3090
   $ make CCAP=89 all    # For RTX 4090
   ```

### Common CCAP Values
- 35: Kepler architecture (GTX 700 series)
- 52: Maxwell architecture (GTX 900 series)
- 61: Pascal architecture (GTX 1000 series)
- 75: Turing architecture (RTX 2000 series)
- 86: Ampere architecture (RTX 3000 series)
- 89: Ada Lovelace architecture (RTX 4000 series)

## License
PubHunt-Range is licensed under GPLv3.

## Disclaimer
ALL THE CODES, PROGRAM AND INFORMATION ARE FOR EDUCATIONAL PURPOSES ONLY. USE IT AT YOUR OWN RISK. THE DEVELOPER WILL NOT BE RESPONSIBLE FOR ANY LOSS, DAMAGE OR CLAIM ARISING FROM USING THIS PROGRAM.

