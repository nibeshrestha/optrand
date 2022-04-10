#pragma once

#include <cassert>
#include <cstddef>
#include <libff/common/default_types/ec_pp.hpp>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "PVSS_Sharing.hpp"
#include "Aggregation.hpp"
#include "Utils.hpp"
#include "Config.hpp"
#include "PolyOps.hpp"
#include "Dleq.hpp"
#include "DecompProof.hpp"
#include "Decryption.hpp"
#include "Factory.hpp"
#include "Beacon.hpp"
#include "depends/ate-pairing/include/bn.h"

#include <libff/common/serialization.hpp>
#include "Serialization.hpp"

namespace optrand_crypto {

class Context {
friend class Factory;
public:
    std::vector<PK_Group> pk_map;
    SyncSystemConfig config;

    G1 g1,g2;
    G2 h1,h2;

    Fr secret_key;
    // This should be the index of this id in `pk_map`
    size_t my_id;


public:
    // Sharing API
    pvss_sharing_t create_sharing() const;
    bool verify_sharing(const pvss_sharing_t& pvss) const; 

    // Aggregation API
    pvss_aggregate_t aggregate(const std::vector<pvss_sharing_t>& pvec, const std::vector<size_t>& id_vec) const;
    bool verify_aggregation(const pvss_aggregate_t& agg) const;

    // Reconstruction API
    decryption_t decrypt(const pvss_aggregate_t& agg) const;
    bool verify_decryption(const pvss_aggregate_t& agg, const decryption_t& dec) const;
    beacon_t reconstruct(const std::vector<decryption_t>& recon) const;
    bool verify_beacon(const pvss_aggregate_t& agg, const beacon_t& beacon) const;

    friend std::ostream& operator<<(std::ostream& os, const Context& ctx);
    friend Context parse_context(std::istream& in);
};


inline std::ostream& operator<< (std::ostream& os, const Context& ctx) {

    serializeVector(os, ctx.pk_map);
//    os << ctx.config << OUTPUT_NEWLINE;

    os << ctx.g1 << OUTPUT_NEWLINE;
    os << ctx.g2 << OUTPUT_NEWLINE;
    os << ctx.h1 << OUTPUT_NEWLINE;
    os << ctx.h2 << OUTPUT_NEWLINE;
    os << ctx.secret_key << OUTPUT_NEWLINE;
    os << ctx.my_id << OUTPUT_NEWLINE;

    return os;

}

inline beacon_t Context::reconstruct(const std::vector<decryption_t> &recon) const 
{
    assert(recon.size()>config.num_faults());
    std::vector<Fr> points;
    points.reserve(config.num_faults()+1);
    std::vector<PK_Group> evals;
    evals.reserve(config.num_faults()+1);
    for(const auto& dec: recon) {
        points.emplace_back(static_cast<long>(dec.origin+1));
        evals.emplace_back(dec.dec);
    }
    auto point = lagrange_interpolation(config.num_faults(), evals, points);
    // e(h^s, g')
    auto beacon = libff::default_ec_pp::pairing(point, h2);
    return beacon_t{point, beacon};
}

inline bool Context::verify_beacon(const pvss_aggregate_t& agg, const beacon_t& beacon) const
{
    // Check that this is the correct beacon for this aggregation by checking
    // e(h^s, g1) = e(h1, g^s)
    auto gs = Com_Group::zero();
    for(const auto& decomp: agg.decomposition) {
        gs = gs + decomp.gs;
    }
    auto lhs = libff::default_ec_pp::reduced_pairing(PK_generator, gs);
    if (lhs != libff::default_ec_pp::reduced_pairing(beacon.recon, Com_generator)) {
        std::cout << "Beacon reconstruction check failed" << std::endl;
        return false;
    }
    return libff::default_ec_pp::pairing(beacon.recon, h2) == beacon.beacon;
}

inline bool Context::verify_decryption(const pvss_aggregate_t& agg, const decryption_t& dec) const {
    return dec.verify(agg.encryptions.at(dec.origin));
}

inline decryption_t Context::decrypt(const pvss_aggregate_t &agg) const 
{
    return decryption_t::generate(agg.encryptions.at(my_id), this->secret_key, my_id);
}

inline bool Context::verify_aggregation(const pvss_aggregate_t &agg) const
{
    if (agg.encryptions.size() != config.num_replicas() || 
        agg.commitments.size() != config.num_replicas() ||
        agg.decomposition.size() != agg.id_vec.size() ||
        agg.id_vec.size() < config.num_faults()) 
    {
        std::cout << "Length mismatch" << std::endl;
        return false;
    }

    // Coding check for the commitments
    if (!Polynomial::ensure_degree(agg.commitments, config.num_faults())) {
        std::cout << "Degree check failed" << std::endl;
        return false;
    }

    // Pairing check
    for(size_t i=0; i<config.num_replicas();i++) {
        // Check e(pk, comi) = e(enc, h1)
        if(!(libff::default_ec_pp::reduced_pairing(pk_map.at(i), agg.commitments.at(i)) == libff::default_ec_pp::reduced_pairing(agg.encryptions.at(i), Com_generator))) {
            std::cout << "Pairing check failed" << std::endl;
            return false;
        }
    }

    // Decomposition proof check
    auto point = Polynomial::lagrange_interpolation(config.num_faults(), agg.commitments);
    auto gs_prod = Com_Group::zero();
    for(auto& dec_i: agg.decomposition) {
        if(!dec_i.pi.verify(Com_generator, dec_i.gs)) {
            std::cout << "Decomposition in agg vec is incorrect" << std::endl;
            return false;
        }
        gs_prod = gs_prod + dec_i.gs;
    }
    return gs_prod == point;
}

inline bool Context::verify_sharing(const pvss_sharing_t &pvss) const
{
    // Check that the sizes are correct
    if (pvss.encryptions.size() != config.num_replicas() || 
        pvss.commitments.size() != config.num_replicas() ||
        pvss.dleq_proofs.size() != config.num_replicas()) {
            std::cout << "Length mismatch" << std::endl;
            return false;
        }
    // Coding check for the commitments
    if (!Polynomial::ensure_degree(pvss.commitments, config.num_faults())) {
        std::cout << "Degree check failed" << std::endl;
        return false;
    }

    // Check that the  DLEQ proofs are correct
    for(size_t i=0;i<config.num_replicas();i++) {
        if (!pvss.dleq_proofs.at(i).verify(pk_map.at(i), pvss.encryptions.at(i), 
                                Com_generator, pvss.commitments.at(i))) {
                                    return false;
                                }
    }
    // Check decomposition proof
    auto point = Polynomial::lagrange_interpolation(config.num_faults(), pvss.commitments);
    if(point != pvss.decomp_pi.gs) {
        std::cout << "gs check failed";
        return false;
    }
    return pvss.decomp_pi.pi.verify(Com_generator, point);
}

inline pvss_sharing_t Context::create_sharing() const 
{
    // 1. Create a random polynomial
    auto poly = Polynomial::Random(config.num_faults());

    // 2. Evaluate p(i) for all n nodes
    std::vector<Fr> evals;
    evals.reserve(config.num_replicas());
    for(size_t i=0; i<config.num_replicas(); i++) {
        Fr point = (long)(i+1);
        evals.push_back(poly.evaluate(point));
    }

    // 3. Evaluate comm(i) for all n nodes
    std::vector<Com_Group> comms;
    comms.reserve(config.num_replicas());
    for(size_t i=0; i<config.num_replicas(); i++) {
        comms.push_back(evals.at(i) * Com_generator);
    }

    // 4. Evaluate enc(i) for all n nodes
    std::vector<PK_Group> encs;
    encs.reserve(config.num_replicas());
    for(size_t i=0; i<config.num_replicas(); i++) {
        encs.push_back(evals.at(i) * pk_map.at(i));
    }

    // 5. Evaluate dleq_proofs(i) for all n nodes
    std::vector<SharingDleq> proofs;
    proofs.reserve(config.num_replicas());
    for(size_t i=0; i<config.num_replicas(); i++) {
        proofs.push_back(SharingDleq::Prove(
                            pk_map.at(i), encs.at(i), 
                            Com_generator, comms.at(i), 
                            evals.at(i)
                    ));
    }

    // 6. Generate decomposition proof
    auto decomp_proof = DecompositionProof::generate(Com_generator,poly);
    #ifdef NDEBUG
    return pvss_sharing_t{encs, comms, proofs, decomp_proof};
    #else
    return pvss_sharing_t{encs, comms, proofs, decomp_proof, poly.coeffs.at(0)};
    #endif
}

inline pvss_aggregate_t Context::aggregate(const std::vector<pvss_sharing_t>& pvec,
               const std::vector<size_t>& id_vec) const
{
    assert(!pvec.empty());
    assert(pvec.size()<=id_vec.size());
    size_t n = pvec.at(0).encryptions.size();
    size_t to_agg = pvec.size();

    std::vector<PK_Group> encs;
    std::vector<Com_Group> comms;
    std::vector<DecompositionProof> decomp_proofs;
    encs.reserve(n);
    comms.reserve(n);
    decomp_proofs.reserve(to_agg);
    for(size_t i=0;i<n;i++) {
        encs.push_back(PK_Group::zero());
        comms.push_back(Com_Group::zero());
    }
    for(size_t i=0;i<to_agg;i++) {
        const auto &pvss = pvec.at(i);
        // Basic checks
        assert(pvss.commitments.size() == n);
        assert(pvss.encryptions.size() == n);
        assert(pvss.dleq_proofs.size() == n);
        // add every element to encs and comms
        for(size_t j=0;j<n;j++) {
          // 1. Combine all the encryptions
          encs.at(j) = encs.at(j) + pvss.encryptions.at(j);
          // 2. Combine all the commitments
          comms.at(j) = comms.at(j) + pvss.commitments.at(j);
        }
        // 3. Aggregate all the decomposition proofs
        decomp_proofs.push_back(pvss.decomp_pi);
    }
//    #ifndef NDEBUG
    return pvss_aggregate_t{encs, comms, decomp_proofs, id_vec};
//    #else
//    Fr sum = Fr::zero();
//    for(const auto& pvss: pvec) {
//        sum = sum + pvss.secret;
//    }
//    return pvss_aggregate_t{encs, comms, decomp_proofs, id_vec, sum};
//    #endif
}

} // namespace crypto