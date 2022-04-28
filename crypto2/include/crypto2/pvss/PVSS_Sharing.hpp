#pragma once

#include <vector>

#include "DecompProof.hpp"
#include "pvss.hpp"

#include "Serialization.hpp"

namespace optrand_crypto {

using SharingDleq = DleqDual<G1, G2, Fr>;

class pvss_sharing_t {
public:
    std::vector<PK_Group> encryptions;
    std::vector<Com_Group> commitments;
    std::vector<SharingDleq> dleq_proofs;
    std::vector<size_t> ids;
    DecompositionProof decomp_pi;
    #ifndef NDEBUG
    Fr secret;
    #endif

    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::pvss_sharing_t& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::pvss_sharing_t& dt);
};

inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::pvss_sharing_t& self) {
//    os << self.encryptions << std::endl;
    serializeVector(os, self.encryptions);
    serializeVector(os, self.commitments);
    serializeVector(os, self.dleq_proofs);
//    os << self.commitments << std::endl;
//    os << self.dleq_proofs << std::endl;
    os << self.decomp_pi << std::endl;
    serializeVector(os, self.ids);
    #ifndef NDEBUG
    os << self.secret << std::endl;
    #endif
    return os;
}

inline std::istream& operator>> (std::istream& in, optrand_crypto::pvss_sharing_t& self) {
    deserializeVector(in, self.encryptions);
    deserializeVector(in, self.commitments);
    deserializeVector(in, self.dleq_proofs);
//    in >> self.encryptions;
    // libff::consume_OUTPUT_NEWLINE(in);

//    in >> self.commitments;
//    libff::consume_OUTPUT_NEWLINE(in);

//    in >> self.dleq_proofs;
//    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.decomp_pi;
    libff::consume_OUTPUT_NEWLINE(in);

    deserializeVector(in, self.ids);

    #ifndef NDEBUG
    in >> self.secret;
    libff::consume_OUTPUT_NEWLINE(in);
    #endif

    return in;
}

}
