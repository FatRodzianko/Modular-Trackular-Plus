// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for the card guard's estimate: how far the next-pattern request is, from track 0's
// last step, its current step and the tempo. What the firmware reads from the sequencer is in
// modsCardGuard.cpp; tools/emu/sim_songstop.py runs it in the whole firmware.
// Build and run with `make -C core/mods/tests`.

#include "modsCardGuard.h"

#include <cstdio>
#include <string>

using namespace mods;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& name)
{
    checks++;
    if (!condition)
    {
        failures++;
        printf("FAIL %s\n", name.c_str());
    }
}

} // namespace

int main()
{
    // On the last step the request is made, or about to be.
    check(loadRequestNear(15, 15, 120.0f, kLoadGuardMs), "last step is near");
    // A length shortened under the play position.
    check(loadRequestNear(7, 12, 120.0f, kLoadGuardMs), "past the last step is near");
    // No tempo to go by.
    check(loadRequestNear(63, 0, 0.0f, kLoadGuardMs), "no tempo is near");

    // 120 BPM: a step is 125 ms, 93.75 ms with the most swing. The step playing now doesn't count.
    check(loadRequestNear(15, 12, 120.0f, kLoadGuardMs), "120 BPM, 2 steps left (187.5 ms) near");
    check(!loadRequestNear(15, 11, 120.0f, kLoadGuardMs), "120 BPM, 3 steps left (281 ms) clear");
    check(!loadRequestNear(15, 0, 120.0f, kLoadGuardMs), "120 BPM, pattern start clear");

    // 800 BPM: a 16-step pattern never leaves 200 ms clear; a 64-step one does at its start.
    for (uint8_t pos = 0; pos <= 15; pos++)
    {
        check(loadRequestNear(15, pos, 800.0f, kLoadGuardMs),
              "800 BPM, 16 steps, step " + std::to_string(pos) + " near");
    }
    // 14.0625 ms a step: 15 steps (210.9 ms) clear, 14 (196.9 ms) near.
    check(!loadRequestNear(63, 47, 800.0f, kLoadGuardMs), "800 BPM, 15 steps left clear");
    check(loadRequestNear(63, 48, 800.0f, kLoadGuardMs), "800 BPM, 14 steps left near");

    // The estimate is the shortest the steps can be: never longer than the plain steps.
    for (int tempo = 20; tempo <= 1000; tempo += 7)
    {
        for (uint8_t pos = 0; pos < 127; pos += 3)
        {
            const float plainMs = static_cast<float>(127 - pos - 1) * 15000.0f / tempo;
            if (plainMs < kLoadGuardMs)
            {
                check(loadRequestNear(127, pos, static_cast<float>(tempo), kLoadGuardMs),
                      "plain steps under the guard are near, tempo " + std::to_string(tempo));
            }
        }
    }

    printf("test_cardguard: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
