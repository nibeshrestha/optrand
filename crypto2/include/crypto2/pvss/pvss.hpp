#pragma once

#include <cassert>
#include <cstddef>
#include <libff/common/default_types/ec_pp.hpp>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
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
#include "crypto2/pvss/Precomputes.hpp"
#include "depends/ate-pairing/include/bn.h"

#include <libff/common/serialization.hpp>
#include "Serialization.hpp"

namespace optrand_crypto {

class Context {
friend class Factory;
public:
    std::vector<PK_Group> pk_map;
    SyncSystemConfig config;
    std::vector<size_t> active_nodes;

    G1 g1,g2;
    G2 h1,h2;

    Fr secret_key;
    // This should be the index of this id in `pk_map`
    size_t my_id;


public:
    // Sharing API
    pvss_sharing_t create_sharing() const;
    pvss_sharing_t create_sharing(
        const std::vector<size_t> &indices
    ) const;
    bool verify_sharing(
        const pvss_sharing_t& pvss
    ) const; 
    bool verify_sharing(
        const pvss_sharing_t& pvss, 
        const std::vector<size_t>& indices
    ) const; 

    // Aggregation API
    pvss_aggregate_t aggregate(
        const std::vector<pvss_sharing_t>& pvec, 
        const std::vector<size_t>& sender_ids
    ) const;
    bool verify_aggregation(
        const pvss_aggregate_t& agg
    ) const;
    bool verify_aggregation(
        const pvss_aggregate_t& agg,
        const std::vector<size_t>& indices
    ) const;

    // Reconstruction API
    decryption_t decrypt(const pvss_aggregate_t& agg) const;
    bool verify_decryption(const pvss_aggregate_t& agg, const decryption_t& dec) const;
    beacon_t reconstruct(const std::vector<decryption_t>& recon) const;
    bool verify_beacon(const pvss_aggregate_t& agg, const beacon_t& beacon) const;

public:
    std::shared_ptr<Precomputes> m_precomputes_ = nullptr;

// Call if you want to improve run_time performance
public:
    void initialize_precomputations(size_t total_nodes);
};

inline void Context::initialize_precomputations(size_t total_nodes) {
    m_precomputes_ = std::make_shared<Precomputes>(total_nodes);
}

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
    std::vector<size_t> points;
    points.reserve(config.num_faults()+1);
    std::vector<PK_Group> evals;
    evals.reserve(config.num_faults()+1);
    for(const auto& dec: recon) {
        points.emplace_back(dec.origin+1);
        evals.emplace_back(dec.dec);
    }
    auto point = Polynomial::lagrange_interpolation(config.num_faults(), evals, points, m_precomputes_.get());
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
        std::cerr << "Beacon reconstruction check failed" << std::endl;
        return false;
    }
    return libff::default_ec_pp::pairing(beacon.recon, h2) == beacon.beacon;
}

inline bool Context::verify_decryption(const pvss_aggregate_t& agg, const decryption_t& dec) const {
    // DONE: Fix
    // enc[i] is nodei's encryption
    size_t origin_idx = agg.id_to_idx_map.at(dec.origin);
    return dec.verify(agg.encryptions.at(origin_idx));
}

inline decryption_t Context::decrypt(const pvss_aggregate_t &agg) const 
{
    // DONE: Fix
    // My encryption might not be in my id anymore
    size_t my_idx = agg.id_to_idx_map.at(my_id);
    return decryption_t::generate(agg.encryptions.at(my_idx), this->secret_key, my_id);
}

inline bool Context::verify_aggregation(const pvss_aggregate_t &agg) const
{
    return this->verify_aggregation(agg, active_nodes);
}

inline bool Context::verify_aggregation(
    const pvss_aggregate_t &agg,
    const std::vector<size_t>& indices
) const
{
    auto num_replicas = indices.size();
    auto num_faults = (num_replicas-1)/2;

    // Check that all the participants are known
    std::unordered_set<size_t> known_indices, new_indices;
    for(size_t i=0; i<num_replicas; i++) {
        known_indices.emplace(indices.at(i));
        new_indices.emplace(agg.participant_ids.at(i));
        if(agg.id_to_idx_map.find(indices.at(i)) == agg.id_to_idx_map.end()) {
            std::cerr << "An expected index for node " << indices.at(i) 
                        << " doesn't have an entry" << std::endl;
            return false;
        }
    }

    for(auto& nidx: agg.participant_ids) {
        if(known_indices.find(nidx) == known_indices.end()) {
            std::cerr << "Unknown index in sharing idx = " << nidx << std::endl;
            return false;
        }
    }
    // We are getting duplicates in indices, there must be a bug in the code if this is triggered
    assert(known_indices.size() == indices.size());

    if (agg.encryptions.size() != num_replicas || 
        agg.commitments.size() != num_replicas ||
        agg.decomposition.size() != agg.sender_ids.size() ||
        agg.participant_ids.size() != num_replicas ||
        agg.id_to_idx_map.size() != num_replicas ||
        agg.sender_ids.size() < num_faults) 
    {
        std::cerr << "Length mismatch" << std::endl;
        return false;
    }

    std::vector<size_t> points;
    points.reserve(num_replicas);
    for(auto& idx: agg.participant_ids) {
        points.push_back(idx+1);
    }

    // Coding check for the commitments
    if (!Polynomial::ensure_degree(agg.commitments, agg.participant_ids,num_faults, m_precomputes_.get())) {
        std::cerr << "Degree check failed" << std::endl;
        return false;
    }

    // Pairing check
    for(size_t i=0; i<num_replicas;i++) {
        // Check e(pk, comi) = e(enc, h1)
        if(!(libff::default_ec_pp::reduced_pairing(pk_map.at(agg.participant_ids.at(i)), agg.commitments.at(i)) == libff::default_ec_pp::reduced_pairing(agg.encryptions.at(i), Com_generator))) {
            std::cerr << "Pairing check failed" << std::endl;
            return false;
        }
    }

    // Decomposition proof check
    auto point = Polynomial::lagrange_interpolation(
        num_faults, 
        agg.commitments, 
        points,
        m_precomputes_.get()
    );
    auto gs_prod = Com_Group::zero();
    for(auto& dec_i: agg.decomposition) {
        if(!dec_i.pi.verify(Com_generator, dec_i.gs)) {
            std::cerr << "Decomposition in agg vec is incorrect" << std::endl;
            return false;
        }
        gs_prod = gs_prod + dec_i.gs;
    }
    return gs_prod == point;
}

// This function defaults to verifying pvss_sharing_t for active nodes
inline bool Context::verify_sharing(
    const pvss_sharing_t &pvss
) const
{
    return this->verify_sharing(pvss, active_nodes);
}

inline bool Context::verify_sharing(
    const pvss_sharing_t &pvss,
    const std::vector<size_t> &indices
) const
{
    auto num_replicas = indices.size();
    auto num_faults = (num_replicas-1)/2;

    // Check that all indices are present and known
    std::unordered_set<size_t> known_indices, new_indices;
    for(size_t i=0; i<num_replicas; i++) {
        known_indices.emplace(indices.at(i));
        new_indices.emplace(pvss.ids.at(i));
    }
    for(auto& nidx: new_indices) {
        if(known_indices.find(nidx) == known_indices.end()) {
            std::cerr << "Unknown index in sharing" << std::endl;
            return false;
        }
    }
    // We are getting duplicates in indices, there must be a bug in the code if this is triggered
    assert(known_indices.size() == indices.size());

    // Check that the sizes are correct
    if (pvss.encryptions.size() != num_replicas || 
        pvss.commitments.size() != num_replicas ||
        pvss.dleq_proofs.size() != num_replicas ||
        pvss.ids.size() != indices.size() ||
        new_indices.size() != indices.size()) 
    {
        std::cerr << "Length mismatch" << std::endl;
        return false;
    }

    // Coding check for the commitments
    if (!Polynomial::ensure_degree(pvss.commitments, pvss.ids, num_faults, m_precomputes_.get())) 
    {
        std::cerr << "Degree check failed" << std::endl;
        return false;
    }

    // Check that the  DLEQ proofs are correct
    for(size_t i=0;i<num_replicas;i++) {
        size_t id_at_idx = pvss.ids.at(i);
        if (!pvss.dleq_proofs.at(i).verify(pk_map.at(id_at_idx), 
                                pvss.encryptions.at(i), 
                                Com_generator, 
                                pvss.commitments.at(i))) 
        {
            return false;
        }
    }
    // Check decomposition proof
    std::vector<size_t> points;
    points.reserve(num_replicas);
    for(auto& idx: pvss.ids) {
        points.push_back(idx+1);
    }
    auto point = Polynomial::lagrange_interpolation(
        num_faults, 
        pvss.commitments, 
        points,
        m_precomputes_.get()
    );
    if(point != pvss.decomp_pi.gs) {
        std::cerr << "gs check failed" << std::endl;
        return false;
    }
    return pvss.decomp_pi.pi.verify(Com_generator, point);
}

// This function defaults to using active_nodes to create the sharing
inline pvss_sharing_t Context::create_sharing() const 
{
    return this->create_sharing(active_nodes);
}

inline pvss_sharing_t Context::create_sharing(
    const std::vector<size_t> &indices
) const
{
    // 1. Create a random polynomial
    auto degree = (indices.size()-1)/2;
    auto poly = Polynomial::Random(degree);

    // 2. Evaluate p(indices[i]) for all nodes in indices
    std::vector<Fr> evals;
    evals.reserve(indices.size());
    for(auto& idx: indices) {
        Fr point = static_cast<long>(idx+1);
        evals.push_back(poly.evaluate(point));
    }

    // 3. Evaluate comm(indices[i]) for all nodes in indices
    std::vector<Com_Group> comms;
    comms.reserve(indices.size());
    for(size_t i=0; i<indices.size(); i++) {
        comms.push_back(evals.at(i) * Com_generator);
    }

    // 4. Evaluate enc(indices[i]) for all nodes in indices
    std::vector<PK_Group> encs;
    encs.reserve(indices.size());
    for(size_t i=0; i<indices.size(); i++) {
        encs.push_back(evals.at(i) * pk_map.at(indices.at(i)));
    }

    // 5. Evaluate dleq_proofs(i) for all n nodes
    std::vector<SharingDleq> proofs;
    proofs.reserve(indices.size());
    for(size_t i=0; i<indices.size(); i++) {
        proofs.push_back(SharingDleq::Prove(
                            pk_map.at(indices.at(i)), 
                            encs.at(i), 
                            Com_generator, 
                            comms.at(i), 
                            evals.at(i)
                    ));
    }

    // 6. Generate decomposition proof
    auto decomp_proof = DecompositionProof::generate(Com_generator,poly);

    #ifdef NDEBUG
    return pvss_sharing_t{encs, comms, proofs, indices, decomp_proof};
    #else
    return pvss_sharing_t{encs, comms, proofs, indices, decomp_proof, poly.coeffs.at(0)};
    #endif
}

// This function will aggregate all the sharings in pvec
// NOTE: sender_ids can be larger than pvec, the extra ones are ignored
inline pvss_aggregate_t Context::aggregate(
    const std::vector<pvss_sharing_t>& pvec,
    const std::vector<size_t>& sender_ids
) const
{
    assert(!pvec.empty());
    // This is done so that we can perform aggregation during reconfiguration where n and f maybe different
    size_t n = pvec.at(0).ids.size();
    size_t to_agg = pvec.size();
    assert(to_agg<=sender_ids.size());

    std::vector<PK_Group> encs;
    std::vector<Com_Group> comms;
    std::vector<DecompositionProof> decomp_proofs;
    std::vector<size_t> participant_ids;
    encs.reserve(n);
    comms.reserve(n);
    participant_ids.reserve(n);
    decomp_proofs.reserve(to_agg);
    for(size_t i=0;i<n;i++) {
        size_t id_at_idx = pvec.at(0).ids.at(i);
        encs.push_back(PK_Group::zero());
        comms.push_back(Com_Group::zero());
        participant_ids.push_back(id_at_idx);
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
          // Check that we are combining pvss vectors for the same people
          assert(pvss.ids.at(j) == participant_ids.at(j));
        }
        // 3. Aggregate all the decomposition proofs
        decomp_proofs.push_back(pvss.decomp_pi);
    }

    // Create id to index map
    std::unordered_map<size_t, size_t> id_to_idx_map;
    id_to_idx_map.reserve(n);
    for(size_t i=0; i<n;i++) {
        // Hinting that participant i is in index i
        id_to_idx_map.emplace(participant_ids[i], i);
    }
        
    #ifdef NDEBUG
    return pvss_aggregate_t{encs, comms, decomp_proofs, participant_ids, sender_ids, id_to_idx_map};
    #else
    Fr sum = Fr::zero();
    for(const auto& pvss: pvec) {
        sum = sum + pvss.secret;
    }
    return pvss_aggregate_t{encs, comms, decomp_proofs, participant_ids, sender_ids, id_to_idx_map, sum};
    #endif
}

} // namespace crypto