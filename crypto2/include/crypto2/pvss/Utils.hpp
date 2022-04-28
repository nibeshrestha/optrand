#pragma once

#include <libff/common/default_types/ec_pp.hpp>
#include <vector>


namespace optrand_crypto {

#define MY_ASSERT(x,str) ({if((x)){throw std::runtime_error(str);}})

inline void initialize() {
    // Apparently, libff logs some extra info when computing pairings
    libff::inhibit_profiling_info = true;

    // AB: We _info disables printing of information and _counters prevents tracking of profiling information. If we are using the code in parallel, disable both the logs.
    libff::inhibit_profiling_counters = true;

    // Initializes the default EC curve, so as to avoid "surprises"
    libff::default_ec_pp::init_public_params();
}

// Type of group G1
using G1 = typename libff::default_ec_pp::G1_type;
// Type of group G2
using G2 = typename libff::default_ec_pp::G2_type;
// Type of group GT (recall pairing e : G1 x G2 -> GT)
using GT = typename libff::default_ec_pp::GT_type;
// Type of the finite field "in the exponent" of the EC group elements
using Fr = typename libff::default_ec_pp::Fp_type;

using PK_Group = G1;
using Com_Group = G2;

#define PK_generator g1
#define Com_generator h1

class printable {
    virtual std::string pretty_print() const = 0;
};


template<class T>
void printVector(const std::vector<T>& vec)
{
    for(size_t i=0; i<vec.size(); i++) {
        std::cout << "i: " << i
                  << ", "
                  << vec[i] << ", "
                  << std::endl;
    }
}


} // namespace optrand_crypto