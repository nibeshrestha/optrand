#pragma once

#include "crypto2/pvss/Utils.hpp"
#include <stdexcept>
#include <vector>

namespace optrand_crypto {

struct Polynomial {
    // Convention: We arrange the polynomials in the following order:
    // 2x^3 + 3x^2 + 4x + 5 => [5,4,3,2]
    std::vector<Fr> coeffs;

    // Return a random polynomial
    static Polynomial Random(size_t degree);

    // Evaluate the polynomial at this point
    Fr evaluate(const Fr& point) const;

    Fr get_secret() const { return coeffs.at(0); }

    template<class G>
    static bool ensure_degree(std::vector<G> group, size_t degree);

    template<class G>
    static G lagrange_interpolation(const size_t degree, const std::vector<G>& evals);

    template<class G>
    static G lagrange_interpolation(const size_t degree, const std::vector<G>& evals, const std::vector<G>& points);

};

template<class G>
inline G Polynomial::lagrange_interpolation(const size_t degree, 
                                            const std::vector<G>& evaluations) 
{
    if (evaluations.size() < degree+1) {
        throw std::runtime_error("insufficient evaluations");
    }
    G sum = G::zero();
    for(size_t j=0; j<=degree;j++) {
        Fr xj = static_cast<long>(j+1);
        Fr prod = Fr::one();
        for(size_t m=0; m<=degree;m++) {
            if(m==j) {
                continue;
            }
            Fr xm = static_cast<long>(m+1);
            prod = (xm* ((xm-xj).inverse())) * prod;
        }
        sum = sum + (prod*evaluations.at(j));
    }
    return sum;
}

template<class G>
static G lagrange_interpolation(const size_t degree, const std::vector<G>& evals, const std::vector<Fr>& points)
{
    if (evals.size() < degree+1) {
        throw std::runtime_error("insufficient evaluations");
    }
    G sum = G::zero();
    for(size_t j=0; j<=degree;j++) {
        Fr xj = points.at(j);
        Fr prod = Fr::one();
        for(size_t m=0; m<=degree;m++) {
            if(m==j) {
                continue;
            }
            Fr xm = points.at(m);
            prod = (xm* ((xm-xj).inverse())) * prod;
        }
        sum = sum + (prod*evals.at(j));
    }
    return sum;
}


template<class G>
bool Polynomial::ensure_degree(std::vector<G> evaluations, size_t degree) {
    size_t num = evaluations.size();
    if (num < degree)
        return false;

    Polynomial poly = Polynomial::Random(num-degree-2);
    G v = G::zero();
    for(size_t i=1; i<=num; i++) {
        Fr scalar_i = static_cast<long>(i);
        Fr cperp = poly.evaluate(scalar_i);
        for(size_t j=1;j<=num;j++) {
            Fr scalar_j = static_cast<long>(j);
            if(i != j) {
                cperp = cperp * ((scalar_i - scalar_j).inverse());
            }
        }
        v = v + (cperp * evaluations.at(i-1));
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
    return v == G::zero();
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