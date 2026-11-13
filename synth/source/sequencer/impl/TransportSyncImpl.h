#pragma once

#include "Component.h"
#include "MidiEventNotifier.h"
#include "PolySynth/MidiSink.h"
#include "PolySynth/MidiSource.h"
#include "PolyUtil/ExternalClockTempoMeasure.h"
#include "sequencer/TransportSync.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <ranges>

namespace polysynth
{

class TransportSyncImpl : public TransportSync
{
  public:
    TransportSyncImpl(std::function<void()> startCallback,
                      std::function<void()> stopCallback,
                      std::function<void()> pulseCallback,
                      std::function<void()> stepCallback)
      : startEvent(std::move(startCallback))
      , stopEvent(std::move(stopCallback))
      , pulseEvent(std::move(pulseCallback))
      , stepEvent(std::move(stepCallback))
    {
        assert(pulseEvent != nullptr);
        assert(startEvent != nullptr);
        assert(stopEvent != nullptr);
    }

    void start() override;
    void stop() override;
    void setMidiOutput(std::shared_ptr<MidiSink> midiOutput_) override;

    [[nodiscard]] int getPulseCount() const override { return pulseCounter; }

    void subscribeMidiEvents(int instrumentId, std::weak_ptr<MidiEventCallback> callback) override;
    void onMidiEvent(const polysynth::MidiEvent& event) override;
    void updateAudio() override;

    static constexpr MidiEvent kClockEvent = {
        .type = polysynth::MidiEvent::Type::kClock,
        .channel = polysynth::MidiEvent::kChannelOmni,
        .data = polysynth::MidiEvent::Data{ .rawData = polysynth::MidiEvent::Data::RawData{} }
    };
    static constexpr MidiEvent kStartEvent = {
        .type = polysynth::MidiEvent::Type::kStart,
        .channel = polysynth::MidiEvent::kChannelOmni,
        .data = polysynth::MidiEvent::Data{ .rawData = polysynth::MidiEvent::Data::RawData{} }
    };
    static constexpr MidiEvent kStopEvent = {
        .type = polysynth::MidiEvent::Type::kStop,
        .channel = polysynth::MidiEvent::kChannelOmni,
        .data = polysynth::MidiEvent::Data{ .rawData = polysynth::MidiEvent::Data::RawData{} }
    };

  private:
    static constexpr float kUpdatePeriod = static_cast<float>(kAudioBufferSize) / kAudioSampleRate;

    std::function<void()> startEvent;
    std::function<void()> stopEvent;
    std::function<void()> pulseEvent;
    std::function<void()> stepEvent;
    std::shared_ptr<MidiSink> midiOutput;

    MidiEventNotifier publisher;
    float pulseTime = 0.0f;
    int pulseCounter = 0;
    bool running = false;
    bool externalTransportRunning = false;

    void pulse();
    void doStart();
    void doStop();

    static constexpr int kExtClockMeasureCount = 48;
    ExternalClockTempoMeasure<kExtClockMeasureCount, kAudioBufferSize, kAudioSampleRate, kMidiPpqn>
      extClockMeasure;
};

} // polysynth
