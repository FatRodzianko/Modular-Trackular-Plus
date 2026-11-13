#pragma once

#include "PolySynth/MidiSink.h"
#include "PolySynth/MidiSource.h"
#include "source/Component.h"

namespace polysynth
{

class TransportSync
  : public MidiSource
  , public MidiSink
  , public Component
{
  public:
    TransportSync()
      : MidiSource(MidiSource::Type::kInternalKeyboard)
    {
    }

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void setMidiOutput(std::shared_ptr<MidiSink> midiOutput) = 0;

    [[nodiscard]] virtual int getPulseCount() const = 0;

    void setTempo(float tempo)
    {
        if (clockInternal)
        {
            tempoBpm = tempo;
        }
    }
    [[nodiscard]] tiny_optional<float> getTempo() const { return tempoBpm; }

    void setClockInternal(bool internal) { this->clockInternal = internal; }
    [[nodiscard]] bool isClockInternal() const { return clockInternal; }
    void setTransportInternal(bool internal) { transportInternal = internal; }
    [[nodiscard]] bool isTransportInternal() const { return transportInternal; }

    [[nodiscard]] bool isClockOut() const { return sendClock; }
    void setClockOut(bool sendClock_) { sendClock = sendClock_; }
    [[nodiscard]] bool isTransportOut() const { return sendTransport; }
    void setTransportOut(bool sendTransport_) { sendTransport = sendTransport_; }

    void setWaitForExternalStart(bool wait) { this->waitForExternalStart = wait; }
    [[nodiscard]] bool isWaitForExternalStart() const { return waitForExternalStart; }

  protected:
    // NOLINTNEXTLINE(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    tiny_optional<float> tempoBpm = kDefaultTempo;

  private:
    bool clockInternal = true;
    bool transportInternal = true;

    bool sendClock = false;
    bool sendTransport = false;
    bool waitForExternalStart = false;
};

} // polysynth
