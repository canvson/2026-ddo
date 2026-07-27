#ifndef KNOWN_MODEL_H
#define KNOWN_MODEL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    double gain;
    double target_vpp;
    double input_vpp;
    uint16_t input_mVpp;
} KnownModelResult;

double KnownModel_Gain(double freq_hz);
double KnownModel_InputVpp(double freq_hz, double target_vpp);
uint16_t KnownModel_Input_mVpp(uint32_t freq_hz, uint16_t target_vpp10);
KnownModelResult KnownModel_Calc(uint32_t freq_hz, uint16_t target_vpp10);

#ifdef __cplusplus
}
#endif

#endif
