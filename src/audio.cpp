//
// DS4 audio path.
//
// USB OUT (32 kHz stereo S16, matching the DS4's native BT audio rate, so no
// resampling is needed) -> 128-frame PCM blocks to core1 -> bluedroid SBC
// encoder (dual channel, 16 blocks, 8 subbands, loudness, bitpool 25 =
// 112-byte frames, header 9c 75 19) -> core0 packs 4 frames into HID output
// report 0x17 and queues it to the interrupt channel. One report carries
// 512 samples = 16 ms, and the USB isochronous stream is the pacing clock.
//
// Protocol reference: https://www.psdevwiki.com/ps4/DS4-BT and the working
// host-side PoC in ../../ds4_audio.py.
//

/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "audio.h"
#include "bt.h"
#if ENABLE_DEBUG
#include "debug.h"
#endif
#include "tusb.h"
#include <cstdio>
#include <cstring>
#include "utils.h"
#include "pico/multicore.h"
#include "pico/flash.h"
#include "pico/util/queue.h"
#include "config.h"

extern "C" {
#include "classic/btstack_sbc_bluedroid.h"
}

#define SBC_FRAME_SAMPLES 128 // 16 blocks * 8 subbands, per channel
#define SBC_FRAME_LEN     112 // dual channel, bitpool 25
#define FRAMES_PER_REPORT 4
#define REPORT_SIZE       462 // 0x17 report incl. trailing CRC-32
// Audio header byte (report offset 5): target of the SBC payload.
#define AUDIO_TARGET_SPEAKER 0x02
#define AUDIO_TARGET_HEADSET 0x24

static bool plug_headset = false;
static bool mic_active = false; // host has opened the mic IN interface (alt != 0)
static volatile bool core1_ready = false;
alignas(8) static uint32_t audio_core1_stack[2048];

struct pcm_block {
    int16_t data[SBC_FRAME_SAMPLES * 2]; // interleaved stereo
};

struct sbc_frame {
    uint8_t data[SBC_FRAME_LEN];
};

queue_t audio_fifo; // raw pcm blocks core0 -> core1
queue_t sbc_fifo;   // encoded sbc frames core1 -> core0

void set_headset(bool state) {
    plug_headset = state;
}

bool audio_headset_plugged() {
    return plug_headset;
}

// Called from tud_audio_set_itf_cb when the host opens/closes the mic IN
// interface. DS4 mic input over BT is not implemented yet; the host just
// records silence.
void set_mic_active(bool active) {
    mic_active = active;
}

bool audio_mic_active() {
    return mic_active;
}

// Assemble and queue one 0x17 audio report once 4 SBC frames are available.
// bt_write() prepends 0xA2 and fills the trailing CRC-32.
static void __not_in_flash_func(audio_bt_task)() {
    static uint16_t frame_counter = 0;

    if (queue_get_level(&sbc_fifo) < FRAMES_PER_REPORT) {
        return;
    }

    const Config_body &cfg = get_config();
    if (cfg.speaker_select == 3) { // audio disabled
        while (queue_try_remove(&sbc_fifo, NULL)) {}
        return;
    }
    const uint8_t target =
            (cfg.speaker_select == 2 || (cfg.speaker_select == 0 && plug_headset))
                ? AUDIO_TARGET_HEADSET
                : AUDIO_TARGET_SPEAKER;

    uint8_t pkt[REPORT_SIZE]{};
    pkt[0] = 0x17;
    pkt[1] = 0x40;
    pkt[2] = 0xA0;
    pkt[3] = frame_counter & 0xFF;
    pkt[4] = (frame_counter >> 8) & 0xFF;
    pkt[5] = target;
    sbc_frame frame{};
    for (int i = 0; i < FRAMES_PER_REPORT; i++) {
        if (queue_try_remove(&sbc_fifo, &frame)) {
            memcpy(pkt + 6 + i * SBC_FRAME_LEN, frame.data, SBC_FRAME_LEN);
        } else {
            printf("[Audio] Warning: sbc_fifo remove failed\n");
        }
    }
    frame_counter += FRAMES_PER_REPORT;
    bt_write(pkt, sizeof(pkt));
}

void __not_in_flash_func(audio_loop)() {
    // Ensure core1 has initialized the encoder before we process audio
    if (!core1_ready) return;

    audio_bt_task();

    // Read USB speaker PCM and chunk it into SBC-frame-sized blocks.
    if (!tud_audio_available()) return;

    static pcm_block block{};
    static uint block_pos = 0; // int16 samples filled so far

    int16_t raw[128];
    const uint32_t bytes_read = tud_audio_read(raw, sizeof(raw));
    const uint32_t samples = bytes_read / sizeof(int16_t);

    for (uint32_t i = 0; i < samples; i++) {
        block.data[block_pos++] = raw[i];
        if (block_pos == SBC_FRAME_SAMPLES * 2) {
            block_pos = 0;
            if (queue_is_full(&audio_fifo)) {
                queue_try_remove(&audio_fifo, NULL);
            }
            if (!queue_try_add(&audio_fifo, &block)) {
                printf("[Audio] Warning: audio_fifo add failed\n");
            }
        }
    }
}

void audio_init() {
    queue_init(&audio_fifo, sizeof(pcm_block), 4);
    queue_init(&sbc_fifo, sizeof(sbc_frame), 2 * FRAMES_PER_REPORT);
#if ENABLE_DEBUG
    debug_fill_core1_stack_watermark(audio_core1_stack,
                                     sizeof(audio_core1_stack) / sizeof(audio_core1_stack[0]));
#endif
    multicore_launch_core1_with_stack(core1_entry, audio_core1_stack, sizeof(audio_core1_stack));
}

static btstack_sbc_encoder_bluedroid_t sbc_encoder_ctx;
static const btstack_sbc_encoder_t *sbc_encoder;

// PCM blocks from core0 -> SBC encode -> sbc_fifo for the 0x17 reports.
static void __not_in_flash_func(speaker_proc)() {
    pcm_block block{};
    if (!queue_try_remove(&audio_fifo, &block)) {
        return;
    }
    if (get_config().speaker_select == 3) {
        return;
    }
    sbc_frame frame{};
    const uint8_t status = sbc_encoder->encode_signed_16(&sbc_encoder_ctx, block.data, frame.data);
    if (status != 0) {
#if ENABLE_VERBOSE
        printf("[Audio] SBC encode failed: %u\n", status);
#endif
        return;
    }
    if (queue_is_full(&sbc_fifo)) {
        queue_try_remove(&sbc_fifo, NULL);
    }
    if (!queue_try_add(&sbc_fifo, &frame)) {
        printf("[Audio] Warning: sbc_fifo add failed\n");
    }
}

void __not_in_flash_func(core1_entry)() {
    // Register core1 as a flash-safe victim so core0's flash_safe_execute() really
    // parks this core while flash is accessed, instead of letting it fault on XIP.
    // Requires PICO_FLASH_ASSUME_CORE1_SAFE=0.
    flash_safe_execute_core_init();

    // The instance vtable is shared; this core has its context.
    sbc_encoder = btstack_sbc_encoder_bluedroid_init_instance(&sbc_encoder_ctx);
    const uint8_t status = sbc_encoder->configure(
        &sbc_encoder_ctx, SBC_MODE_STANDARD,
        16, 8, SBC_ALLOCATION_METHOD_LOUDNESS,
        32000, 25, SBC_CHANNEL_MODE_DUAL_CHANNEL);
    if (status != 0) {
        printf("[Audio] SBC encoder configure failed: %u\n", status);
        return;
    }

    // Signal core0 that the encoder is ready
    core1_ready = true;

    while (true) {
        speaker_proc();
    }
}
