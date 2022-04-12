#pragma once

#include <vector>

#include "Utils.hpp"
#include "DecompProof.hpp"

#include "crypto2/pvss/Serialization.hpp"
#include "libff/common/serialization.hpp"

namespace optrand_crypto {
class pvss_sharing_t;

class pvss_aggregate_t {
public:
    std::vector<PK_Group> encryptions;
    std::vector<Com_Group> commitments;

    std::vector<DecompositionProof> decomposition;
    std::vector<size_t> id_vec;

    #ifndef NDEBUG
    Fr secret;
    #endif

    pvss_aggregate_t(){}

    pvss_aggregate_t(const std::vector<PK_Group> &encryptions, const std::vector<Com_Group> &commitments,
            const std::vector<DecompositionProof> &decomposition, const std::vector<size_t> &id_vec):
            encryptions(encryptions), commitments(commitments), decomposition(decomposition), id_vec(id_vec) {}

    pvss_aggregate_t(const pvss_aggregate_t &other):
        encryptions(other.encryptions),
        commitments(other.commitments),
        decomposition(other.decomposition),
        id_vec(other.id_vec) {}



    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::pvss_aggregate_t& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::pvss_aggregate_t& dt);

};

inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::pvss_aggregate_t& self) {

    serializeVector(os, self.encryptions);
    serializeVector(os, self.commitments);
//    os << self.encryptions << std::endl;
//    os << self.commitments << std::endl;
    serializeVector(os, self.decomposition);
    serializeVector(os, self.id_vec);
    #ifndef NDEBUG
    os << self.secret << std::endl;
    #endif
    return os;
}

inline std::istream& operator>> (std::istream& in, optrand_crypto::pvss_aggregate_t& self) {
//    in >> self.encryptions;
//    libff::consume_OUTPUT_NEWLINE(in);
//
//    in >> self.commitments;
//    libff::consume_OUTPUT_NEWLINE(in);

    deserializeVector(in, self.encryptions);
    deserializeVector(in, self.commitments);
    deserializeVector(in, self.decomposition);
    deserializeVector(in, self.id_vec);

    #ifndef NDEBUG
    in >> self.secret;
    libff::consume_OUTPUT_NEWLINE(in);
    #endif

    return in;
}


}