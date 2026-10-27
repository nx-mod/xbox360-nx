// halo3 - small hooks inside the game's own functions ([[midasm_hook]] in overrides.toml).
//
// Each one is called from the generated code at the address given in overrides.toml, before the instruction
// there. They are declared by the generated code itself (extern, global namespace), so the names must match.

#include <chrono>

#include <rex/thread.h>

// 0x821A85C4: the repeat of a worker thread's wait for work (sub_821A8500, started from sub_821B61B0). It polls
// a slot of its queue and the kernel's millisecond counter, for up to 66 ms, without resting. On the Xbox 360
// that spin had one of six hardware threads to itself; on the Switch it held one of the game's three cores at
// 98 % (stack samples, boot 7 in PORTING.md) and starved the rest, the audio included. A short sleep on each
// repeat keeps the worker answering within a fraction of a millisecond.
void Halo3WorkerPollYield() {
  rex::thread::Sleep(std::chrono::microseconds(100));
}

// Direct3D's fence check (overrides.toml, 0x825A7F1C): once per poll while the GPU has not reached the fence.
void Halo3FenceSpinYield() {
  rex::thread::Sleep(std::chrono::microseconds(100));
}

// XAudio's spin barrier (overrides.toml, 0x826C62AC): a short sleep per wait, so the partner gets the core. Short
// because the mixer meets there several times per 5.33 ms frame.
void Halo3AudioBarrierYield() {
  rex::thread::Sleep(std::chrono::microseconds(20));
}
