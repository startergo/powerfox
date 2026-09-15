#ifndef POWERFOX_MATH_SHIM_H
#define POWERFOX_MATH_SHIM_H
#ifdef __cplusplus
extern "C" {
#endif
long long llrintf(float);
long long llrintl(long double);
long long llrint(double);
long long llroundf(float);
long long llroundl(long double);
long long llround(double);
#ifdef __cplusplus
}
#endif
#endif
