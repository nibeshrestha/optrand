#pragma once

#include <vector>

#include "DecompProof.hpp"
#include "pvss.hpp"

#include "Serialization.hpp"
#include "salticidae/type.h"
#include "salticidae/stream.h"

using namespace salticidae;

namespace optrand_crypto {

using SharingDleq = DleqDual<G1, G2, Fr>;

class pvss_sharing_t {
public:
    std::vector<PK_Group> encryptions;
    std::vector<Com_Group> commitments;
    std::vector<SharingDleq> dleq_proofs;
    DecompositionProof decomp_pi;
    #ifndef NDEBUG
    Fr secret;
    #endif

    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::pvss_sharing_t& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::pvss_sharing_t& dt);

//    void serialize(DataStream &s) const{
//        s << htole((uint32_t) encryptions.size());
//        for (auto enc: encryptions)
//            s << enc;
//    }
//
//    void unserialize(DataStream &s, optrand_crypto::pvss_sharing_t& self) const{
//        uint32_t n;
//        s >> n;
//        n = letoh(n);
//        self.encryptions.resize(n);
//        for (auto &enc: self.encryptions)
//            s >> enc;
//    }

};


inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::pvss_sharing_t& self) {
    os << self.encryptions.size() << "\n";
    for (auto enc : self.encryptions)
       os << enc << OUTPUT_NEWLINE;

    os << self.commitments.size() << "\n";
    for (auto comm: self.commitments)
        os << comm << OUTPUT_NEWLINE;

    os << self. dleq_proofs.size() << "\n";
    for (auto dleq_proof : self.dleq_proofs)
        os << dleq_proof << OUTPUT_NEWLINE;

    os << self.decomp_pi << OUTPUT_NEWLINE;

//    os << self.encryptions << std::endl;
//    os << self.commitments << std::endl;
//    os << self.dleq_proofs << std::endl;
//    os << self.decomp_pi << std::endl;
//    #ifndef NDEBUG
//    os << self.secret << std::endl;
//    #endif
    return os;
}

inline std::istream& operator>> (std::istream& in, optrand_crypto::pvss_sharing_t& self) {

    self.encryptions.clear();

    size_t s;
    in >> s;
    libff::consume_OUTPUT_NEWLINE(in);

    self.encryptions.reserve(s);

    for(size_t i = 0; i< s; i++){
        PK_Group pk;
        in >> pk;
        self.encryptions.emplace_back(pk);
        libff::consume_OUTPUT_NEWLINE(in);
    }

    in >> s;
    libff::consume_OUTPUT_NEWLINE(in);

    self.commitments.reserve(s);

    for(size_t i = 0; i< s; i++){
        Com_Group comm;
        in >> comm;
        self.commitments.emplace_back(comm);
        libff::consume_OUTPUT_NEWLINE(in);
    }

    in >> s;
    libff::consume_OUTPUT_NEWLINE(in);

    self.dleq_proofs.reserve(s);

    for(size_t i = 0; i< s; i++){
        SharingDleq pk;
        in >> pk;
        self.dleq_proofs.emplace_back(pk);
        libff::consume_OUTPUT_NEWLINE(in);
    }

    in >> self.decomp_pi;
    libff::consume_OUTPUT_NEWLINE(in);


//    in >> self.encryptions;
//    libff::consume_OUTPUT_NEWLINE(in);
//
//    in >> self.commitments;
//    libff::consume_OUTPUT_NEWLINE(in);
//
//    in >> self.dleq_proofs;
//    libff::consume_OUTPUT_NEWLINE(in);
//
//    in >> self.decomp_pi;
//    libff::consume_OUTPUT_NEWLINE(in);
//
//    #ifndef NDEBUG
//    in >> self.secret;
//    libff::consume_OUTPUT_NEWLINE(in);
//    #endif

    return in;
}

}
