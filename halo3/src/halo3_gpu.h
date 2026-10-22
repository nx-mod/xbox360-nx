// halo3 - the game's graphics system on the Switch (see halo3_gpu.cpp).

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <rex/system/interfaces/graphics.h>

namespace halo3::gpu {

// Stands in for the Xbox 360 GPU: answers what the game's Direct3D waits for, and reads its command ring.
std::unique_ptr<rex::system::IGraphicsSystem> CreateGraphicsSystem();

// The game's presents so far (the debug overlay's FPS).
extern std::atomic<uint64_t> g_swaps;

}  // namespace halo3::gpu
