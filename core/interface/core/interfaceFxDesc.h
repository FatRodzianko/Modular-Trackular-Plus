#ifndef INTERFACE_INTERFACEFXDESC_H_
#define INTERFACE_INTERFACEFXDESC_H_

// Section keys in /System/Text/fx_hints.txt (ordered to match InterfaceGlobals::fxIDs).
// On FEATURE_SYNTH builds, "<KEY>.SYNTH" is checked first, falling back to "<KEY>".
constexpr const char* const fxHintKeys[FX_COUNT] = {
    "NONE",              // FX_TYPE_NONE
    "OFF",               // FX_TYPE_OFF
    "VOLUME",            // FX_TYPE_VOLUME
    "PANNING",           // FX_TYPE_PANNING
    "MICROTUNING",       // FX_TYPE_MICROTUNING
    "GLIDE",             // FX_TYPE_GLIDE
    "TEMPO",             // FX_TYPE_TEMPO
    "SWING",             // FX_TYPE_SWING
    "MICROMOVE",         // FX_TYPE_MICROMOVE
    "GATE_LENGTH",       // FX_TYPE_GATE_LENGTH
    "CHANCE",            // FX_TYPE_CHANCE
    "ROLL",              // FX_TYPE_ROLL
    "ARP",               // FX_TYPE_ARP
    "RANDOM_NOTE",       // FX_TYPE_RANDOM_NOTE
    "RANDOM_INSTRUMENT", // FX_TYPE_RANDOM_INSTRUMENT
    "RANDOM_VALUE",      // FX_TYPE_RANDOM_VALUE
    "RANDOM_VOLUME",     // FX_TYPE_RANDOM_VOLUME
    "REVERSE_PLAYBACK",  // FX_TYPE_REVERSE_PLAYBACK
    "POSITION",          // FX_TYPE_POSITION
    "SAMPLE_SLICE",      // FX_TYPE_SAMPLE_SLICE
    "VOLUME_LFO",        // FX_TYPE_VOLUME_LFO
    "PANNING_LFO",       // FX_TYPE_PANNING_LFO
    "FILTER_LFO",        // FX_TYPE_FILTER_LFO
    "POSITION_LFO",      // FX_TYPE_POSITION_LFO
    "FINETUNE_LFO",      // FX_TYPE_FINETUNE_LFO
    "OVERDRIVE",         // FX_TYPE_OVERDRIVE
    "FILTER_LOWPASS",    // FX_TYPE_FILTER_LOWPASS
    "FILTER_BANDPASS",   // FX_TYPE_FILTER_BANDPASS
    "FILTER_HIGHPASS",   // FX_TYPE_FILTER_HIGHPASS
    "DELAY_SEND",        // FX_TYPE_DELAY_SEND
    "REVERB_SEND",       // FX_TYPE_REVERB_SEND
    "BIT_DEPTH",         // FX_TYPE_BIT_DEPTH
    "TUNE",              // FX_TYPE_TUNE
    "SLIDE_UP",          // FX_TYPE_SLIDE_UP
    "SLIDE_DOWN",        // FX_TYPE_SLIDE_DOWN
    "AMP_ENV",           // FX_TYPE_AMP_ENV
    "FILTER_ENV",        // FX_TYPE_FILTER_ENV
    "GRANULAR_LENGTH",   // FX_TYPE_GRANULAR_LENGTH
    "SEND_CC_A",         // FX_TYPE_SEND_CC_A
    "SEND_CC_B",         // FX_TYPE_SEND_CC_B
    "SEND_CC_C",         // FX_TYPE_SEND_CC_C
    "SEND_CC_D",         // FX_TYPE_SEND_CC_D
    "SEND_CC_E",         // FX_TYPE_SEND_CC_E
    "SEND_CC_F",         // FX_TYPE_SEND_CC_F
    "MIDI_CHORD",        // FX_TYPE_MIDI_CHORD
    "BREAK_PATTERN",     // FX_TYPE_BREAK_PATTERN
};

#endif /* INTERFACE_INTERFACEFXDESC_H_ */
