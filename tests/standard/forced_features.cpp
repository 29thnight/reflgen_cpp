// An explicit backend override must not make C++26 syntax available in an older language mode.
#define REFLGEN_USE_STD_REFLECTION 1
#include "reflgen/core/features.h"
int main() {}
