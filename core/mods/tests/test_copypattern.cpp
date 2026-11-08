// Modular-Trackular mods. SPDX-License-Identifier: MIT
//
// Host tests for Copy to New: the empty-step and empty-pattern rules, what RAM rules out, the
// search order, the message text, and the job on a fake CE (one card check per update, the
// busy limit, the playing gap, pattern changed, cancel). What CE's own code does with it is in
// tools/emu/test_copypattern.py. Build and run with `make -C core/mods/tests`.

#include "copypattern.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace mods;
using namespace mods::copypattern;

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

// CE's step and pattern, by field name only.
struct Step
{
    int8_t note = -1;
    uint8_t instrument = 0;
    struct
    {
        uint8_t type = 0;
        uint8_t value = 0;
    } fx[2];
    int8_t volume = -1;
    int8_t micromove = -1;
};

struct Pattern
{
    struct
    {
        uint8_t length = 31;
        Step step[kSteps];
    } track[kTracks];
};

void testRules()
{
    Step s;
    check(stepIsEmpty(s), "a cleared step is empty");
    s.fx[0].value = 40;
    s.fx[1].value = 9;
    check(stepIsEmpty(s), "FX values without a type don't count");
    const int8_t notes[] = { 0, 60, -2, -3, -4, -5 };
    for (int8_t n : notes)
    {
        Step t;
        t.note = n;
        check(!stepIsEmpty(t), "note " + std::to_string(n) + " counts");
    }
    Step t;
    t.instrument = 3;
    check(!stepIsEmpty(t), "an instrument without a note counts");
    t = Step();
    t.fx[0].type = 5;
    check(!stepIsEmpty(t), "an FX1 type counts");
    t = Step();
    t.fx[1].type = 5;
    check(!stepIsEmpty(t), "an FX2 type counts");
    t = Step();
    t.volume = 0;
    check(!stepIsEmpty(t), "a V of 0 counts");
    t = Step();
    t.micromove = 0;
    check(!stepIsEmpty(t), "an m of 0 counts");

    static Pattern p;
    check(patternIsEmpty(p), "a cleared pattern is empty");
    p.track[15].step[127].note = 60;
    check(!patternIsEmpty(p), "the last step of the last track counts, past the length");
    p.track[15].step[127].note = -1;
    p.track[0].step[100].micromove = 10;
    check(!patternIsEmpty(p), "an m past the length counts");
}

void testOrder()
{
    check(nextCandidate(1, 1) == 2, "from 1, 2 first");
    check(nextCandidate(5, 255) == 1, "255 wraps to 1");
    check(nextCandidate(1, 255) == 0, "from 1, the search ends after 255");
    check(nextCandidate(255, 255) == 1, "from 255, 1 first");
    check(nextCandidate(255, 254) == 0, "from 255, the search ends after 254");
    check(nextCandidate(7, 6) == 0, "the search ends before the source");

    std::vector<int> order;
    uint8_t n = nextCandidate(200, 200);
    while (n != 0)
    {
        order.push_back(n);
        n = nextCandidate(200, n);
    }
    check(order.size() == 254 && order.front() == 201 && order[54] == 255 && order[55] == 1 &&
            order.back() == 199,
          "from 200: 201..255, then 1..199");
}

void testInUse()
{
    uint8_t blocks[255] = {};
    uint8_t midi[255] = {};
    uint8_t playlist[255] = {};
    uint16_t perf[16];
    for (auto& p : perf)
    {
        p = 1;
    }
    playlist[0] = 3;
    playlist[1] = 4;
    InUse use{ blocks, midi, playlist, sizeof(playlist), 0, nullptr, 16 };
    check(!usedInRam(use, 10), "nothing in RAM: free");
    check(usedInRam(use, 3) && usedInRam(use, 4), "in the song: used");
    blocks[9] = 1;
    check(usedInRam(use, 10), "a sample track's block: used");
    blocks[9] = 0;
    midi[9] = 0x80;
    check(usedInRam(use, 10), "a MIDI track's block: used");
    midi[9] = 0;
    use.queued = 10;
    check(usedInRam(use, 10), "queued: used");
    use.queued = 0;
    perf[5] = 10;
    check(!usedInRam(use, 10), "performance tracks only count in performance mode");
    use.performanceTracks = perf;
    check(usedInRam(use, 10), "a performance track's pattern: used");
    check(usedInRam(use, 0), "0 is never a target");
    playlist[254] = 255;
    check(usedInRam(use, 255), "the song's last slot counts");
}

void testMessage()
{
    char text[kMessageSize];
    const struct
    {
        uint8_t n;
        const char* want;
    } cases[] = { { 1, "Copied to pattern 1" },
                  { 99, "Copied to pattern 99" },
                  { 100, "Copied to pattern 100" },
                  { 255, "Copied to pattern 255" } };
    for (const auto& c : cases)
    {
        formatCopied(text, sizeof(text), c.n);
        check(strcmp(text, c.want) == 0, c.want);
    }
}

class FakeCe : public Ce
{
  public:
    uint8_t pattern = 1;
    bool on = true;
    bool performance = false;
    bool idle = true;
    bool isPlaying = false;
    uint32_t now = 1000;
    std::set<int> ram;           // used by RAM
    std::set<int> withFile;      // has a file
    std::set<int> fileUsed;      // a file with something in it
    CopyResult result = CopyResult::copied;
    std::vector<int> fileChecks; // in order
    std::vector<std::pair<int, int>> copies;
    std::vector<std::string> messages;

    uint8_t currentPattern() override { return pattern; }
    bool switchedOn() override { return on; }
    bool performanceChanged() override { return performance; }
    bool fileManagerIdle() override { return idle; }
    bool playing() override { return isPlaying; }
    uint32_t nowMs() override { return now; }
    bool usedInRam(uint8_t p) override { return ram.count(p) != 0; }
    FileState fileState(uint8_t p) override
    {
        fileChecks.push_back(p);
        if (withFile.count(p) == 0)
        {
            return FileState::none;
        }
        return fileUsed.count(p) != 0 ? FileState::used : FileState::empty;
    }
    CopyResult copy(uint8_t source, uint8_t target) override
    {
        copies.push_back({ source, target });
        return result;
    }
    void message(const char* text) override { messages.push_back(text); }
};

// Update until the job ends, at most `limit` times.
int runToEnd(FakeCe& ce, int limit = 1000)
{
    int updates = 0;
    while (running() && updates < limit)
    {
        update(ce);
        updates++;
    }
    return updates;
}

void testJob()
{
    init();
    {
        FakeCe ce;
        ce.pattern = 1;
        press(ce);
        check(running() && ce.fileChecks.empty() && ce.messages.empty(), "the press touches no files");
        update(ce);
        check(!running() && ce.copies.size() == 1 && ce.copies[0] == std::make_pair(1, 2),
              "pattern 2 has no file: copied in the first update");
        check(ce.messages.size() == 1 && ce.messages[0] == "Copied to pattern 2", "the message");
    }
    {
        FakeCe ce;
        ce.pattern = 10;
        ce.ram = { 11, 12, 13 };
        ce.withFile = { 14, 15, 16 };
        ce.fileUsed = { 14, 15 };
        press(ce);
        const int updates = runToEnd(ce);
        check(ce.fileChecks == std::vector<int>({ 14, 15, 16 }), "RAM rules out 11-13 without the card");
        check(updates == 3, "one card check per update");
        check(ce.copies.size() == 1 && ce.copies[0].second == 16, "an empty file is a target");
    }
    {
        FakeCe ce;
        ce.pattern = 254;
        ce.ram = { 255 };
        press(ce);
        runToEnd(ce);
        check(ce.copies.size() == 1 && ce.copies[0].second == 1, "wraps past 255 to 1");
    }
    {
        FakeCe ce;
        ce.pattern = 3;
        for (int p = 1; p <= 255; p++)
        {
            if (p % 2 == 0)
            {
                ce.ram.insert(p);
            }
            else
            {
                ce.withFile.insert(p);
                ce.fileUsed.insert(p);
            }
        }
        press(ce);
        runToEnd(ce);
        check(ce.copies.empty() && ce.messages.size() == 1 && ce.messages[0] == "No empty pattern left",
              "none left: nothing written");
        check(ce.fileChecks.size() == 127 && ce.fileChecks.front() == 5 && ce.fileChecks.back() == 1,
              "every other odd pattern checked once, not the source");
    }
    {
        FakeCe ce;
        ce.withFile = { 2, 3 };
        ce.fileUsed = { 2, 3 };
        press(ce);
        update(ce);
        ce.pattern = 7;
        update(ce);
        check(!running() && ce.copies.empty() && ce.messages.back() == "Pattern changed - try again",
              "switched pattern before the copy");
    }
    {
        FakeCe ce;
        ce.result = CopyResult::patternChanged;
        press(ce);
        runToEnd(ce);
        check(ce.messages.back() == "Pattern changed - try again", "the snapshot saw a switch");
        ce.result = CopyResult::writeFailed;
        ce.messages.clear();
        press(ce);
        runToEnd(ce);
        check(ce.messages.back() == "Copy failed - SD card", "a write error");
    }
    {
        FakeCe ce;
        ce.idle = false;
        press(ce);
        update(ce);
        ce.now += 4999;
        update(ce);
        check(running() && ce.fileChecks.empty(), "busy: waits, no card check");
        ce.idle = true;
        update(ce);
        check(!running() && ce.copies.size() == 1, "idle again: goes on");

        ce.idle = false;
        ce.messages.clear();
        press(ce);
        update(ce);
        ce.now += 5000;
        update(ce);
        check(!running() && ce.messages.size() == 1 && ce.messages[0] == "Busy - try again",
              "busy for 5 s: gives up");
    }
    {
        FakeCe ce;
        ce.isPlaying = true;
        ce.withFile = { 2, 3, 4 };
        ce.fileUsed = { 2, 3 };
        press(ce);
        update(ce);
        update(ce);
        check(ce.fileChecks.size() == 1, "playing: no second check within 100 ms");
        ce.now += 100;
        update(ce);
        check(ce.fileChecks.size() == 2, "playing: the next check after 100 ms");
        ce.now += 100;
        update(ce);
        check(!running() && ce.copies.size() == 1 && ce.copies[0].second == 4, "playing: copied");
    }
    {
        FakeCe ce;
        ce.withFile = { 2 };
        ce.fileUsed = { 2 };
        press(ce);
        update(ce);
        ce.on = false;
        update(ce);
        check(!running() && ce.messages.empty() && ce.copies.empty(), "switch Off: dropped silently");

        ce.on = true;
        press(ce);
        update(ce);
        cancel();
        update(ce);
        check(!running() && ce.messages.empty() && ce.copies.empty(), "cancel: dropped silently");

        ce.on = false;
        press(ce);
        check(!running(), "the press does nothing with the switch Off");
    }
    {
        FakeCe ce;
        ce.withFile = { 2, 3 };
        ce.fileUsed = { 2, 3 };
        press(ce);
        ce.pattern = 9;
        press(ce);
        ce.pattern = 1;
        runToEnd(ce);
        check(ce.copies.size() == 1 && ce.copies[0].first == 1, "a second press while running is ignored");
    }
    {
        FakeCe ce;
        ce.performance = true;
        press(ce);
        check(!running() && ce.messages.size() == 1 && ce.messages[0] == "Not in performance mode",
              "performance mode: refused at the press");
        ce.performance = false;
        ce.messages.clear();
        ce.withFile = { 2 };
        ce.fileUsed = { 2 };
        press(ce);
        update(ce);
        ce.performance = true;
        update(ce);
        check(!running() && ce.copies.empty() && ce.messages[0] == "Not in performance mode",
              "performance mode: refused during the search");
    }
}

} // namespace

int main()
{
    testRules();
    testOrder();
    testInUse();
    testMessage();
    testJob();
    printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
