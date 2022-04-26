#pragma once

#include "Utils.hpp"
#include "Config.hpp"
#include "crypto/pvss/pvss.hpp"

namespace optrand_crypto {

class Context;

class Factory {
public:
    SyncSystemConfig m_config_;

    Factory(SyncSystemConfig&& sys_config): m_config_{std::move(sys_config)} {}

    std::vector<Context> getContext(size_t total_nodes) const {
        // I am going to assume the following
        assert(total_nodes >= m_config_.num_replicas());

        std::vector<Fr> secret_keys;
        std::vector<PK_Group> public_keys;
        std::vector<Context> ret;
        std::vector<size_t> active_nodes{};
        secret_keys.reserve(total_nodes);
        public_keys.reserve(total_nodes);
        active_nodes.reserve(m_config_.num_replicas());
        ret.reserve(total_nodes);

        auto g1 = G1::random_element(), g2 = G1::random_element();
        auto h1 = G2::random_element(), h2 = G2::random_element(); 

        for(size_t i=0;i<total_nodes; i++) {
            auto sk = Fr::random_element();
            secret_keys.push_back(sk);
            public_keys.push_back(sk * PK_generator);
        }

        for(size_t i=0;i<m_config_.num_replicas();i++) {
            active_nodes.push_back(i);
        }

        for(size_t i=0;i<total_nodes; i++) {
            auto ctx = Context{public_keys, 
                                m_config_, 
                                active_nodes,
                                g1, g2, h1, h2, 
                                secret_keys.at(i), 
                                i, 
                                nullptr};
            ret.push_back(ctx);
        }
        return ret;
    }


    std::vector<Context> getContext() const {
        return this->getContext(m_config_.num_replicas());
    }
};

}
