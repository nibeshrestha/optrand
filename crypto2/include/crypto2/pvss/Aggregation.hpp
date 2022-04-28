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
    std::vector<size_t> participant_ids;
    std::vector<size_t> sender_ids;

    // A map to find the enc/comm for replica i
    std::unordered_map<size_t, size_t> id_to_idx_map;

    #ifndef NDEBUG
    Fr secret;
    #endif

    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::pvss_aggregate_t& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::pvss_aggregate_t& dt);

};

inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::pvss_aggregate_t& self) {

    serializeVector(os, self.encryptions);
    serializeVector(os, self.commitments);
//    os << self.encryptions << std::endl;
//    os << self.commitments << std::endl;
    serializeVector(os, self.decomposition);
    serializeVector(os, self.participant_ids);
    serializeVector(os, self.sender_ids);
    serializeMap(os, self.id_to_idx_map);
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
    deserializeVector(in, self.participant_ids);
    deserializeVector(in, self.sender_ids);
    deserializeMap(in, self.id_to_idx_map);

    #ifndef NDEBUG
    in >> self.secret;
    libff::consume_OUTPUT_NEWLINE(in);
    #endif

    return in;
}


}