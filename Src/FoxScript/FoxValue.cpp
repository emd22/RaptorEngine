#include "FoxValue.hpp"

#include <Core/Hash.hpp>
#include <Util/Tokenizer.hpp>

namespace fx {

eFoxType FoxStringToType(Token* token)
{
    static constexpr Hash32 scIntType = HashStr32("int");
    static constexpr Hash32 scFloatType = HashStr32("float");
    static constexpr Hash32 scStringType = HashStr32("str");

    switch (token->GetHash()) {
    case scIntType:
        return eFoxType::INT;
    case scFloatType:
        return eFoxType::FLOAT;
    case scStringType:
        return eFoxType::STRING;
    default:;
    }

    return eFoxType::NONETYPE;
}

} // namespace fx
