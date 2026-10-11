#include "runtime/vu0_ops.hpp"
#include "stubs/visualvu1.hpp"

float InverseLength(float *vector) {
    return vu0::Rsqrt(1.0f, vu0::LengthSquared(vector));
}
