#pragma once

#include "crypto2/pvss/Utils.hpp"

#include <cstddef>
#include <unordered_map>

namespace optrand_crypto {

struct Precomputes {
    size_t n;
    std::unordered_map<size_t, std::unordered_map<size_t,Fr>> inverse_map;

    Precomputes(size_t n):n{n} {
        inverse_map.reserve(n);
        for(size_t i=1; i<=n; i++) {
            Fr scalar_i = static_cast<long>(i);
            inverse_map[i] = std::unordered_map<size_t, Fr>(n);
            for(size_t j=1;j<=n;j++) {
                Fr scalar_j = static_cast<long>(j);
                if(i != j) {
                    inverse_map[i][j] = ((scalar_i - scalar_j).inverse());
                }
            }
        }
    }
};

}