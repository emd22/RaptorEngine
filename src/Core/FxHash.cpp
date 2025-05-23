#include "FxHash.hpp"


uint32 FxHashFnv1a(const char *str)
{
    uint32 hash = FX_HASH_FNV1A_SEED;

    unsigned char ch;
    while ((ch = static_cast<unsigned char>(*(str++)))) {
        hash = (hash ^ ch) * FX_HASH_FNV1A_PRIME;
    }

    return hash;
}
