#pragma once

#include <cstddef>

#include "crypto2/pvss/Dleq.hpp"
#include "crypto2/pvss/Utils.hpp"

namespace optrand_crypto {

using DecryptionDleq = Dleq<PK_Group, Fr>;

class decryption_t {
public:
    PK_Group dec;
    DecryptionDleq pi;
    // Index in the pk_map
    size_t origin;

    static decryption_t generate(const PK_Group& enc, const Fr& secret, size_t my_id);
    bool verify(const PK_Group& enc) const;

    friend std::ostream& operator<<(std::ostream& os, const optrand_crypto::decryption_t& dt);
    friend std::istream& operator>>(std::istream& in, optrand_crypto::decryption_t& dt);

};

inline std::ostream& operator<< (std::ostream& os, const optrand_crypto::decryption_t& self) {
    os << self.dec << std::endl;
    os << self.pi << std::endl;
    os << self.origin << std::endl;
    return os;
}

inline std::istream& operator>> (std::istream& in, optrand_crypto::decryption_t& self) {
    in >> self.dec;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.pi;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.origin;
    libff::consume_OUTPUT_NEWLINE(in);

    return in;
}

inline decryption_t decryption_t::generate(const PK_Group& enc, const Fr& sk, size_t my_id)
{
    auto dec = sk.inverse() * enc;
    auto pi = DecryptionDleq::Prove(dec, enc, sk);
    return decryption_t{dec, pi, my_id};
}

inline bool decryption_t::verify(const PK_Group& enc) const 
{
    return pi.verify(dec, enc);
}

}