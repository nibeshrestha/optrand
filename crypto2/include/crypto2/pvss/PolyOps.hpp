#pragma once

#include "crypto2/pvss/Precomputes.hpp"
#include "crypto2/pvss/Utils.hpp"
#include <cstddef>
#include <libff/common/default_types/ec_pp.hpp>
#include <libff/algebra/scalar_multiplication/multiexp.hpp>
#include <stdexcept>
#include <vector>


/*
In SPURT, nodes need to compute an expression of this
form to: (i) validate the polynomial commitments sent during
commitment phase; (ii) validate the aggregated polynomial
sent by the leader; and (iii) compute the beacon output from
reconstruction shares
*/

namespace optrand_crypto {

template<class Group>
Group multiExp(
    typename std::vector<Group>::const_iterator base_begin,
    typename std::vector<Group>::const_iterator base_end,
    typename std::vector<Fr>::const_iterator exp_begin,
    typename std::vector<Fr>::const_iterator exp_end
    )
{
    long sz = base_end - base_begin;
    long expsz = exp_end - exp_begin;
    if(sz != expsz)
        throw std::runtime_error("multiExp needs the same number of bases as exponents");
    //size_t numCores = getNumCores();

    if(sz > 4) {
        if(sz > 16384) {
            return libff::multi_exp<Group, Fr, libff::multi_exp_method_BDLO12>(base_begin, base_end,
                exp_begin, exp_end, 1);//numCores);
        } else {
            return libff::multi_exp<Group, Fr, libff::multi_exp_method_bos_coster>(base_begin, base_end,
                exp_begin, exp_end, 1);//numCores);
        }
    } else {
        return libff::multi_exp<Group, Fr, libff::multi_exp_method_naive>(base_begin, base_end,
            exp_begin, exp_end, 1);
    }
}

struct Polynomial {
    // Convention: We arrange the polynomials in the following order:
    // 2x^3 + 3x^2 + 4x + 5 => [5,4,3,2]
    std::vector<Fr> coeffs;

    // Return a random polynomial
    static Polynomial Random(size_t degree);

    // Evaluate the polynomial at this point
    Fr evaluate(const Fr& point) const;

    Fr get_secret() const { return coeffs.at(0); }

    // DONE: Fix to use provided points
    template<class G>
    static bool ensure_degree(
            std::vector<G> group, 
            std::vector<size_t> indices,  
            size_t degree, 
            const Precomputes* precomputes = nullptr
    );

    template<class G>
    static G lagrange_interpolation(const size_t degree,    
                                        const std::vector<G>& evals, 
                                        const std::vector<size_t>& points,
                                        const Precomputes* precomputes = nullptr);

};

template<class G>
G Polynomial::lagrange_interpolation(const size_t degree, 
                                    const std::vector<G>& evals, 
                                    const std::vector<size_t>& points,
                                    const Precomputes* precomputes)
{
    if (evals.size() < degree+1) {
        throw std::runtime_error("insufficient evaluations");
    }
    auto use_precomputes = precomputes != nullptr;
    std::vector<Fr> interpolants;
    interpolants.reserve(degree+1);
    for(size_t j=0; j<=degree;j++) {
        Fr xj = static_cast<long>(points.at(j));
        Fr prod = Fr::one();
        for(size_t m=0; m<=degree;m++) {
            if(m==j) {
                continue;
            }
            Fr xm = static_cast<long>(points.at(m));
            if(use_precomputes) {
                prod = (xm* precomputes->inverse_map.at(points.at(m)).at(points.at(j))) * prod;
            } else {
                prod = (xm* ((xm-xj).inverse())) * prod;
            }
        }
        interpolants.push_back(prod);
    }
    return multiExp<G>(evals.begin(), 
                        evals.begin()+static_cast<long>(degree+1), 
                        interpolants.begin(), 
                        interpolants.end());
}

template<class G>
bool Polynomial::ensure_degree(std::vector<G> evaluations, 
    std::vector<size_t> indices,
    size_t degree, 
    const Precomputes* precomputes) 
{
    auto use_precomputes = precomputes != nullptr;
    size_t num = evaluations.size();
    if (num < degree)
        return false;

    // I am going to assume the following:
    assert(evaluations.size() == indices.size());

    Polynomial poly = Polynomial::Random(num-degree-2);
    G val = G::zero();

    std::vector<Fr> cperps;
    cperps.reserve(num);
    for(size_t i=0; i<num; i++) {
        size_t idx = indices.at(i)+1;
        Fr scalar_i = static_cast<long>(idx);
        Fr cperp = poly.evaluate(scalar_i);
        for(size_t j=0;j<num;j++) {
            size_t jdx = indices.at(j)+1;
            if(idx != jdx) {
                if (use_precomputes) {
                    cperp = cperp * precomputes->inverse_map.at(idx).at(jdx);
                } else {
                    Fr scalar_j = static_cast<long>(jdx);
                    cperp = cperp * ((scalar_i-scalar_j).inverse());
                }
            }
        }
        cperps.push_back(cperp);
        // v = v + (cperp * evaluations.at(i-1));
    }
    //         let poly = math::Polynomial::generate(n - self.threshold - 1);
    //         let mut v = Point::infinity();
    //         for i in 0..n {
    //             let idx = i as usize;
    //             let mut cperp = poly.evaluate(Scalar::from_u32(i));
    //             for j in 0..n {
    //                 if i != j {
    //                     cperp = cperp * (Scalar::from_u32(i) - Scalar::from_u32(j)).inverse();
    //                 }
    //             }
    //             let commitment = &self.commitments[idx];
    //             v = v + commitment.point.mul(&cperp);
    //         }

    //         v == Point::infinity()
    val = multiExp<G>( evaluations.begin(),
        evaluations.end(),
        cperps.begin(),
        cperps.end());
    return val == G::zero();
}

inline Polynomial Polynomial::Random(size_t degree) {
    std::vector<Fr> poly;
    poly.reserve(degree+1);
    
    for(size_t i=0;i<=degree;i++) {
        poly.push_back(Fr::random_element());
    }

    return Polynomial{poly};
}

inline Fr Polynomial::evaluate(const Fr& point) const {
    // Use horner's rule
    auto result = Fr::zero(); // Initialize result
 
    // Evaluate value of polynomial using Horner's method
    auto degree = coeffs.size();
    for (size_t i=1; i<=degree; i++) {
        result = (result* point) + coeffs.at(degree-i);
    }

    // example evaluation
    // degree=4, 
    // i=1
    // result = 2
    // i=2
    // result = 2x + 3 
    // i=3
    // result = 2x^2 + 3x + 4
    // i=4
    // result = 2x^3 + 3x^2 + 4x + 5

    return result;
}

}