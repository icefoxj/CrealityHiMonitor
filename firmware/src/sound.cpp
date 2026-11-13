#include "sound.h"

#include <Wire.h>
#include <stdio.h>

#include "settings.h"

namespace sound {

namespace {

constexpr uint8_t kEs8311Addr = 0x18;   // the Echo Base's codec

bool              s_echo = false;
Output            s_out  = Output::None;
hm::MelodyPlayer  s_player;
char              s_desc[16] = "none";
bool              s_started = false;   // the speaker task runs (tone() starts it)

}  // namespace

bool validBuzzerPin(int pin){
    switch (pin){
        case 1: case 2: case 5: case 6: case 7: case 8: case 38: case 39:
            return true;
        default:
            return false;
    }
}

void prepare(m5::M5Unified::config_t& cfg){
    // The Echo Base's I2C is on the bottom header, G38 (SDA) / G39 (SCL)
    Wire1.begin(38, 39, 100000);
    Wire1.beginTransmission(kEs8311Addr);
    s_echo = Wire1.endTransmission() == 0;
    Wire1.end();
    if (s_echo){
        cfg.external_speaker.atomic_echo = 1;
    }
}

void reconfigure(){
    const Settings& st = settings::get();
    if (s_echo){
        s_out = Output::EchoBase;
        snprintf(s_desc, sizeof(s_desc), "echo-base");
    } else if (validBuzzerPin(st.buzzerPin)){
        if (s_started){ M5.Speaker.end(); s_started = false; }
        auto c = M5.Speaker.config();
        c.pin_data_out = st.buzzerPin;
        c.pin_bck = I2S_PIN_NO_CHANGE;
        c.pin_ws = I2S_PIN_NO_CHANGE;
        c.pin_mck = I2S_PIN_NO_CHANGE;
        c.buzzer = true;
        c.use_dac = false;
        c.magnification = 16;
        M5.Speaker.config(c);
        s_out = Output::Buzzer;
        snprintf(s_desc, sizeof(s_desc), "buzzer:%d", st.buzzerPin);
    } else {
        if (s_started){ M5.Speaker.end(); s_started = false; }
        s_out = Output::None;
        snprintf(s_desc, sizeof(s_desc), "none");
    }
    M5.Speaker.setVolume(st.volume);
}

void begin(){
    reconfigure();
}

void play(hm::AlertKind k){
    if (s_out == Output::None || settings::get().muted){
        return;
    }
    const hm::Note* m = hm::melodyFor(k);
    if (m){
        s_player.start(m, millis());
    }
}

void stop(){
    s_player.stop();
    if (s_started){
        M5.Speaker.stop();
    }
}

void loop(){
    hm::Note n;
    if (s_player.next(millis(), &n)){
        M5.Speaker.tone(n.hz, n.ms);
        s_started = true;
    }
}

Output output(){
    return s_out;
}

const char* describe(){
    return s_desc;
}

}  // namespace sound
