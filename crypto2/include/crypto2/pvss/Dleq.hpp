#pragma once

#include "crypto2/pvss/Utils.hpp"
#include "internal/picosha.h"

namespace optrand_crypto {

inline Fr hashToField(const unsigned char * bytes, size_t len) {
    // hash bytes, but output a hex string not bytes
    std::string hex;
    picosha2::hash256_hex_string(bytes, bytes + len, hex);
    hex.pop_back(); // small enough for BN-P254 field

    // convert hex to mpz_t
    mpz_t rop;
    mpz_init(rop);
    mpz_set_str(rop, hex.c_str(), 16);

    // convert mpz_t to Fr
    Fr fr = libff::bigint<Fr::num_limbs>(rop);
    mpz_clear(rop);

    return fr;
}

inline Fr hashToField(const std::string& message)
{
    return hashToField(reinterpret_cast<const unsigned char*>(message.c_str()), message.size());
}

template<class Z> 
Z hashToField(const std::string& message);

template<class Z> 
Z hashToField(const unsigned char* bytes, size_t len);


template<class Ga, class Gb, class Z>
struct DleqDual{
    Ga gra;
    Gb grb;
    Z pi;

    static DleqDual<Ga, Gb, Z> Prove(const Ga& g, const Ga& gx, const Gb &h, const Gb& hx, const Z& x);
    bool verify(const Ga& g, const Ga& gx, const Gb& h, const Gb& hx) const;

    template<class Gaa, class Gbb, class Zz>
    friend std::ostream& operator<<(std::ostream& os, const DleqDual<Gaa, Gbb, Zz>& dt);
    template<class Gaa, class Gbb, class Zz>
    friend std::istream& operator>>(std::istream& in, DleqDual<Gaa,Gbb,Zz>& dt);
};

template<class Ga, class Gb, class Z>
inline std::ostream& operator<< (std::ostream& os, const DleqDual<Ga,Gb, Z>& self) {
    os << self.gra << std::endl;
    os << self.grb << std::endl;
    os << self.pi << std::endl;
    return os;
}

template<class Ga, class Gb, class Z>
inline std::istream& operator>> (std::istream& in, DleqDual<Ga, Gb,Z>& self) {
    in >> self.gra;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.grb;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.pi;
    libff::consume_OUTPUT_NEWLINE(in);

    return in;
}

template<class Ga, class Gb, class Z>
bool DleqDual<Ga, Gb, Z>::verify(const Ga& g, const Ga& gx, const Gb& h, const Gb& hx) const {

    std::stringstream ss;
    ss << g << std::endl;
    ss << gx << std::endl;
    ss << h << std::endl;
    ss << hx << std::endl;
    ss << gra << std::endl;
    ss << grb << std::endl;
    Z c = hashToField(ss.str());

    // g1^pi == gra+(c*gx)
    // g2^pi == gra+(c*hx)
    auto lhs1 =  pi*g;
    auto rhs1 = gra+(c*gx);
    if (lhs1 != rhs1) {
        std::cout << "Left check fail for dleq" << std::endl;
        return false;
    }
    auto lhs2 = pi*h;
    auto rhs2 = grb+(c*hx);
    return lhs2 == rhs2;
}


template<class Ga, class Gb, class Z>
DleqDual<Ga, Gb, Z> DleqDual<Ga,Gb,Z>::Prove(const Ga& g, const Ga& gx, const Gb& h, const Gb& hx, const Z& x)
{
    // First choose a random r
    Z r = Z::random_element();
    Ga gra = r * g;
    Gb grb = r * h;

    // Choose c = H(g,gx,h,hx, gra, grb)
    std::stringstream ss;
    ss << g << std::endl;
    ss << gx << std::endl;
    ss << h << std::endl;
    ss << hx << std::endl;
    ss << gra << std::endl;
    ss << grb << std::endl;
    Z c = hashToField(ss.str());

    // Set proof = r - cx
    return DleqDual<Ga, Gb, Z>{gra, grb, r+(x*c)};
}

template<class G, class Z>
struct Dleq{
    G gr;
    Z pi;

    static Dleq<G, Z> Prove(const G& g, const G& gx, const Z& x);
    bool verify(const G& g, const G& gx) const;

    template<class Gg, class Zz>
    friend std::ostream& operator<<(std::ostream& os, const Dleq<Gg,Zz>& dt);
    template<class Gg, class Zz>
    friend std::istream& operator>>(std::istream& in, Dleq<Gg,Zz>& dt);
};

template<class G, class Z>
inline std::ostream& operator<< (std::ostream& os, const Dleq<G, Z>& self) {
    os << self.gr << std::endl;
    os << self.pi << std::endl;
    return os;
}

template<class G, class Z>
inline std::istream& operator>> (std::istream& in, Dleq<G,Z>& self) {
    in >> self.gr;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.pi;
    libff::consume_OUTPUT_NEWLINE(in);

    return in;
}

template<class G, class Z>
bool Dleq<G, Z>::verify(const G& g, const G& gx) const {
    std::stringstream ss;
    ss << g << std::endl;
    ss << gx << std::endl;
    ss << gr << std::endl;
    Z c = hashToField(ss.str());

    auto lhs = pi*g;
    auto rhs = gr+(c*gx);

    return lhs==rhs;
}

template<class G, class Z>
Dleq<G, Z> Dleq<G, Z>::Prove(const G& g, const G& gx, const Z& x)
{
    // First choose a random r
    Z r = Z::random_element();
    G gr = r * g;

    // Choose c = H(g,gx,gr)
    std::stringstream ss;
    ss << g << std::endl;
    ss << gx << std::endl;
    ss << gr << std::endl;
    Z c = hashToField(ss.str());

    // Set proof = r - cx
    return Dleq<G, Z>{gr, r+(x*c)};
}

}