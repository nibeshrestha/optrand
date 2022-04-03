#pragma once

#include <vector>

#include "Utils.hpp"
#include "DecompProof.hpp"

#include "crypto/pvss/Serialization.hpp"
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

    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::pvss_aggregate_t& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::pvss_aggregate_t& dt);

};

inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::pvss_aggregate_t& self) {
    os << self.encryptions << std::endl;
    os << self.commitments << std::endl;
    serializeVector(os, self.decomposition);
    serializeVector(os, self.id_vec);
    #ifndef NDEBUG
    os << self.secret << std::endl;
    #endif
    return os;
}

inline std::istream& operator>> (std::istream& in, optrand_crypto::pvss_aggregate_t& self) {
    in >> self.encryptions;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.commitments;
    libff::consume_OUTPUT_NEWLINE(in);

    deserializeVector(in, self.decomposition);
    deserializeVector(in, self.id_vec);

    #ifndef NDEBUG
    in >> self.secret;
    libff::consume_OUTPUT_NEWLINE(in);
    #endif

    return in;
}


}