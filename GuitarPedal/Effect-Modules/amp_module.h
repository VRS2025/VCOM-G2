#pragma once
#ifndef AMP_MODULE_H
#define AMP_MODULE_H

#include <stdint.h>
#include <cmath>
#include "daisysp.h"
#include "base_effect_module.h"
#include <RTNeural/RTNeural.h>
#include "ImpulseResponse/ImpulseResponse.h"

#ifdef __cplusplus

/** @file amp_module.h */

using namespace daisysp;


namespace bkshepherd
{

// Однополюсный ФНЧ — замена удалённого из свежего DaisySP класса Tone (тот же API).
class OnePoleTone {
  public:
    void Init(float sample_rate) { sr_ = sample_rate; SetFreq(20000.0f); z_ = 0.0f; }
    void SetFreq(float f) {
        if (f < 1.0f) f = 1.0f;
        if (f > sr_ * 0.49f) f = sr_ * 0.49f;
        b1_ = expf(-2.0f * 3.14159265358979f * f / sr_);
        a0_ = 1.0f - b1_;
    }
    float Process(float in) { z_ = a0_ * in + b1_ * z_; return z_; }

  private:
    float sr_ = 48000.0f, a0_ = 1.0f, b1_ = 0.0f, z_ = 0.0f;
};

class AmpModule : public BaseEffectModule
{
  public:
    AmpModule();
    ~AmpModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void SelectModel();
    void SelectIR();
    void CalculateMix();
    void CalculateTone();
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    float GetBrightnessForLED(int led_id) const override;

  private:


    float m_gainMin;
    float m_gainMax;

    float wetMix;
    float dryMix;

    float nnLevelAdjust;
    int   m_currentModelindex = -1;

    float m_toneFreqMin;    
    float m_toneFreqMax;

    OnePoleTone tone; // Low Pass (замена DaisySP Tone)
    //Balance bal;     // Balance for volume correction in filtering

    float m_cachedEffectMagnitudeValue;

    ImpulseResponse mIR;
    int   m_currentIRindex = -1;
};
} // namespace bkshepherd
#endif
#endif
