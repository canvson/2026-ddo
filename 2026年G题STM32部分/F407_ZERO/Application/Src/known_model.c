/*
 * File: known_model.c
 * Role: Legacy 2025 known-circuit model calculation implementation.
 * Scope: Retained for reference; not compiled into the 2026 G Target.
 */
#include "known_model.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double KnownModel_Gain(double freq_hz)
{
    double w = 2.0 * M_PI * freq_hz;
    double real = 1.0 - 1.0e-8 * w * w;
    double imag = 3.0e-4 * w;
    double den = sqrt(real * real + imag * imag);
    if (den <= 0.0) {
        return 0.0;
    }
    return 5.0 / den;
}

double KnownModel_InputVpp(double freq_hz, double target_vpp)
{
    double gain = KnownModel_Gain(freq_hz);
    if (gain <= 0.0) {
        return 0.0;
    }
    return target_vpp / gain;
}

uint16_t KnownModel_Input_mVpp(uint32_t freq_hz, uint16_t target_vpp10)
{
    double target_vpp = (double)target_vpp10 / 10.0;
    double vin_vpp = KnownModel_InputVpp((double)freq_hz, target_vpp);
    double vin_mVpp = vin_vpp * 1000.0;

    if (vin_mVpp < 0.0) {
        return 0u;
    }
    if (vin_mVpp > 65535.0) {
        return 65535u;
    }
    return (uint16_t)(vin_mVpp + 0.5);
}

KnownModelResult KnownModel_Calc(uint32_t freq_hz, uint16_t target_vpp10)
{
    KnownModelResult result;
    result.target_vpp = (double)target_vpp10 / 10.0;
    result.gain = KnownModel_Gain((double)freq_hz);
    result.input_vpp = KnownModel_InputVpp((double)freq_hz, result.target_vpp);
    result.input_mVpp = KnownModel_Input_mVpp(freq_hz, target_vpp10);
    return result;
}
