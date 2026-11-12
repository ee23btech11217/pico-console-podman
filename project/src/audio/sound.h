#pragma once

#include <cstddef>
#include <cstdint>

// The stored music plays faster with the sphere level: +MUSIC_SPEED_PER_LEVEL_PCT per sphere, up to MUSIC_SPEED_MAX_PCT
#ifndef MUSIC_SPEED_PER_LEVEL_PCT
#define MUSIC_SPEED_PER_LEVEL_PCT 4
#endif
#ifndef MUSIC_SPEED_MAX_PCT
#define MUSIC_SPEED_MAX_PCT 130
#endif

enum SoundId : uint8_t {
    SND_SHOT_VULCAN = 0,
    SND_SHOT_LASER,
    SND_SHOT_MISSILE,
    SND_HIT_ENEMY,
    SND_HIT_PLAYER,
    SND_ENEMY_KILLED_SMALL,
    SND_ENEMY_KILLED_BIG,
    SND_PLAYER_KILLED,
    SND_GET_FRAGMENT,
    SND_GET_UPGRADE,
    SND_MENU_SELECT,
    SND_MENU_START,
    SND_LAUNCH,
    SND_ARRIVE,
    SND_TIME_ALARM,
    SND_DODGE,
    SND_COUNT
};

void sound_init();

void sound_request(uint32_t bits);

void sound_music(bool on);

void sound_music_level(uint8_t level);

// Playback speed of the stored music in percent (100 = as recorded; it follows with a slew, no jumps).
void sound_music_speed(int percent);

// Sleep: stop the DAC and drive the speaker transistor low (no clock may be needed after this); resume restarts it.
void sound_suspend();
void sound_resume();

// One line for the log: whether a music pack was found, the decoder self-test, what plays and the DAC interrupt rate.
void sound_describe(char *buf, size_t n);
