//
// DS4 audio path: USB speaker PCM (32 kHz stereo S16) -> SBC (dual channel,
// 16 blocks, 8 subbands, loudness, bitpool 25) -> HID output report 0x17
// (4 x 112-byte SBC frames per report, one report per 16 ms of audio).
//

#ifndef DS4_BRIDGE_AUDIO_H
#define DS4_BRIDGE_AUDIO_H

#include <cstdint>

void audio_init();
void audio_loop();
void core1_entry();
void set_headset(bool state);
bool audio_headset_plugged();
void set_mic_active(bool active);
bool audio_mic_active();

#endif //DS4_BRIDGE_AUDIO_H
