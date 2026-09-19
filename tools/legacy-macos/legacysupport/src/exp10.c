#include "MacportsLegacySupport.h"

#if __MPLS_LIB_SUPPORT_EXP10__

extern double pow(double, double);
extern float powf(float, float);

double __exp10(double x) {
    return pow(10.0, x);
}

float __exp10f(float x) {
    return powf(10.0f, x);
}

#endif