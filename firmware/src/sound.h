#pragma once

// Alert sounds.
//
// The CoreS3 has a speaker (AW88298 amplifier), set up by M5Unified.
//
// The AtomS3R has no speaker of its own; two add-ons work:
//
// - Atomic Echo Base (speaker + ES8311 codec, stacks under the Atom):
//   found at boot by probing the codec at I2C 0x18 on G38/G39, then driven
//   by M5Unified (external_speaker.atomic_echo).
// - A piezo buzzer on a free GPIO (Grove port G1/G2, or G5-G8, G38, G39
//   on the bottom header), set with the console command "buzzer <pin>".
//
// Without either, alerts are visual only.

#include <M5Unified.h>

#include "alert.h"

namespace sound {

enum class Output : uint8_t { None, EchoBase, Buzzer, Speaker };

// Before M5.begin(): look for an Echo Base and enable it in the config
void prepare(m5::M5Unified::config_t& cfg);
// After M5.begin() and settings::begin()
void begin();
// Re-applies the buzzer pin / volume from the settings
void reconfigure();

void play(hm::AlertKind k);
void stop();
void loop();   // every frame: starts the melody's next note

Output output();
const char* describe();   // "speaker", "echo-base", "buzzer:2", "none"

// GPIOs the buzzer may use on an AtomS3R (none on the CoreS3)
bool validBuzzerPin(int pin);

}  // namespace sound
