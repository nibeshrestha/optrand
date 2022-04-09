#pragma once

#include "Utils.hpp"
#include "Config.hpp"
#include "crypto2/pvss/pvss.hpp"

namespace optrand_crypto {

class Context;

class Factory {
public:
    SyncSystemConfig m_config_;

    Factory(SyncSystemConfig&& sys_config): m_config_{std::move(sys_config)} {}

    std::vector<Context> getContext() const {
        std::vector<Fr> secret_keys;
        std::vector<PK_Group> public_keys;
        std::vector<Context> ret;
        secret_keys.reserve(m_config_.num_replicas());
        public_keys.reserve(m_config_.num_replicas());
        ret.reserve(m_config_.num_replicas());

        auto g1 = G1::random_element(), g2 = G1::random_element();
        auto h1 = G2::random_element(), h2 = G2::random_element(); 

        for(size_t i=0;i<m_config_.num_replicas(); i++) {
            auto sk = Fr::random_element();
            secret_keys.push_back(sk);
            public_keys.push_back(sk * PK_generator);
        }

        for(size_t i=0;i<m_config_.num_replicas(); i++) {
            auto ctx = Context{public_keys, m_config_, 
                                    g1, g2, h1, h2, secret_keys.at(i), i};
            ret.push_back(ctx);
        }
        return ret;
    }

    Context parseContext(std::istream& in){
        std::vector<PK_Group> pk_map;
        G1 g1,g2;
        G2 h1,h2;

        Fr secret_key;
        size_t my_id;

        deserializeVector(in, pk_map);

//        in >> config;
//        libff::consume_OUTPUT_NEWLINE(in);
//
        in >> g1;
        libff::consume_OUTPUT_NEWLINE(in);

        in >> g2;
        libff::consume_OUTPUT_NEWLINE(in);

        in >> h1;
        libff::consume_OUTPUT_NEWLINE(in);

        in >> h2;
        libff::consume_OUTPUT_NEWLINE(in);

        in >> secret_key;
        libff::consume_OUTPUT_NEWLINE(in);

        in >> my_id;
        libff::consume_OUTPUT_NEWLINE(in);

        return Context{pk_map, m_config_, g1, g2, h1, h2, secret_key, my_id};
    }

};

}
