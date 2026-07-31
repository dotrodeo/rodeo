#pragma once
#include <cstdint>
#include "daisysp.h"

// Instrument-wide parameter values ("the panel is the instrument").
//
// In M1 these are fixed, musical placeholder defaults so the voice makes sound
// over MIDI. From M4 the external-ADC control surface / ParamStore will drive
// these fields; the engine keeps reading a Patch either way.
struct Patch
{
    // --- Oscillators ---
    uint8_t osc1_wave   = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    uint8_t osc2_wave   = daisysp::Oscillator::WAVE_POLYBLEP_SAW;
    int     osc1_octave = 0;    // octaves
    int     osc2_octave = 0;    // octaves
    float   osc2_semi   = 0.0f; // coarse tune (semitones)
    float   osc2_detune = 7.0f; // fine detune (cents) — gentle chorus
    float   osc1_level  = 0.5f; // 0..1
    float   osc2_level  = 0.5f; // 0..1

    // --- Filter (per voice) ---
    float filt_cutoff     = 1000.0f; // Hz (envelope base)
    float filt_res        = 0.35f;   // 0..1
    float filt_env_amount = 3.0f;    // octaves of envelope sweep

    // --- Filter ADSR (seconds / 0..1 sustain) ---
    float filt_a = 0.005f, filt_d = 0.40f, filt_s = 0.20f, filt_r = 0.35f;

    // --- Amp ADSR ---
    float amp_a = 0.005f, amp_d = 0.20f, amp_s = 0.70f, amp_r = 0.35f;

    // --- Master ---
    float master_vol = 0.4f;
};
