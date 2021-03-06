#include "config/defines.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __AVR__
#include <avr/pgmspace.h>
#endif
#define DRUM 1
#define GUITAR 2
const uint8_t PROGMEM types[MIDI_ROCK_BAND_DRUMS + 1] = {
    [PS3_GUITAR_HERO_DRUMS] = DRUM,      [PS3_ROCK_BAND_DRUMS] = DRUM,
    [WII_ROCK_BAND_DRUMS] = DRUM,        [XINPUT_ROCK_BAND_DRUMS] = DRUM,
    [XINPUT_GUITAR_HERO_DRUMS] = DRUM,   [MIDI_ROCK_BAND_DRUMS] = DRUM,
    [MIDI_GUITAR_HERO_DRUMS] = DRUM,     [PS3_GUITAR_HERO_GUITAR] = GUITAR,
    [PS3_ROCK_BAND_GUITAR] = GUITAR,     [WII_ROCK_BAND_GUITAR] = GUITAR,
    [XINPUT_ROCK_BAND_GUITAR] = GUITAR,  [XINPUT_GUITAR_HERO_GUITAR] = GUITAR,
    [XINPUT_LIVE_GUITAR] = GUITAR,       [MIDI_ROCK_BAND_GUITAR] = GUITAR,
    [MIDI_GUITAR_HERO_GUITAR] = GUITAR,  [MIDI_LIVE_GUITAR] = GUITAR,
    [KEYBOARD_GUITAR_HERO_DRUMS] = DRUM, [KEYBOARD_GUITAR_HERO_GUITAR] = GUITAR,
    [KEYBOARD_ROCK_BAND_DRUMS] = DRUM,   [KEYBOARD_ROCK_BAND_GUITAR] = GUITAR,
    [KEYBOARD_LIVE_GUITAR] = GUITAR};
#ifdef __AVR__
bool isDrum(uint8_t subtype) { return pgm_read_byte(types + subtype) == DRUM; }
bool isGuitar(uint8_t subtype) {
  return pgm_read_byte(types + subtype) == GUITAR;
}
#else
bool isDrum(uint8_t subtype) { return types[subtype] == DRUM; }
bool isGuitar(uint8_t subtype) {
  return types[subtype] == GUITAR;
}
#endif