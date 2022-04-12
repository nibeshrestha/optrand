#pragma once

#include "crypto2/pvss/Dleq.hpp"
#include "crypto2/pvss/PolyOps.hpp"
#include "crypto2/pvss/Utils.hpp"

// #include "libff/common/serialization.hpp"
// #include "Serialization.hpp"

namespace optrand_crypto {

class DecompositionProof {
public:
    Dleq<Com_Group, Fr> pi;
    Com_Group gs;
    static DecompositionProof generate(const Com_Group& generator, 
                                            const Polynomial& poly);

    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::DecompositionProof& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::DecompositionProof& dt);
};

inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::DecompositionProof& self) {
    os << self.pi << std::endl;
    os << self.gs << std::endl;
    return os;
}

inline std::istream& operator>> (std::istream& in, optrand_crypto::DecompositionProof& self) {
    in >> self.pi;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.gs;
    libff::consume_OUTPUT_NEWLINE(in);

    return in;
}

inline DecompositionProof DecompositionProof::generate(
    const Com_Group& generator, const Polynomial& poly) 
{
    auto secret = poly.get_secret();
    auto gs = secret*generator;
    auto pi = Dleq<Com_Group, Fr>::Prove(generator, gs,secret);
    return DecompositionProof{pi, gs};
}


}