#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "crypto2/pvss/Aggregation.hpp"
#include "crypto2/pvss/Beacon.hpp"
#include "crypto2/pvss/Decryption.hpp"
#include "crypto2/pvss/Factory.hpp"
#include "crypto2/pvss/pvss.hpp"
#include "crypto2/pvss/Utils.hpp"

#include "crypto2/pvss/Serialization.hpp"

// #define TEST_API

// Some global helpers
std::stringstream ss_serialize;
std::stringstream ss_deserialize;

int main() {
    std::cout << "Namaskara!" << std::endl;

    // Settings
    const size_t n = 4, f = 1;

    // Must be called before using any optrand_crypto function
    optrand_crypto::initialize();

    // Create a system config
    optrand_crypto::SyncSystemConfig conf = optrand_crypto::SyncSystemConfig::FromNumReplicas(n);
    std::cout << conf.pretty_print() << std::endl;

    // Create a factory
    auto factory = optrand_crypto::Factory(std::move(conf));
    std::vector<optrand_crypto::Context> setup = factory.getContext();
    
    // Create PVSS vectors
    std::vector<optrand_crypto::pvss_sharing_t> pvss_vec;
    pvss_vec.reserve(n);
    for(size_t i=0;i<n;i++) {
        auto sharing = setup.at(i).create_sharing();
        pvss_vec.push_back(sharing);
        // DONE: Serialization test
        ss_serialize.str(std::string{});
        ss_serialize << sharing;
        auto bytes = ss_serialize.str();
        ss_deserialize.str(bytes);
        optrand_crypto::pvss_sharing_t pvss2;
        ss_deserialize >> pvss2;
        // pvss_sharing_t pvss2 = pvss_sharing_t(bytes);
        for(size_t j=0; j<n;j++) {
            if (!setup.at(j).verify_sharing(sharing)) {
                throw std::runtime_error("Verification failed");
            }
        }
        for(size_t j=0; j<n;j++) {
            if (!setup.at(j).verify_sharing(pvss2)) {
                throw std::runtime_error("serialized Verification failed");
            }
        }
    }
    std::vector<optrand_crypto::pvss_sharing_t> agg_pvss;
    std::vector<size_t> id_vec;
    for(size_t i=0;i<=f;i++) {
        agg_pvss.push_back(pvss_vec.at(i));
        id_vec.push_back(i);
    }
    auto agg = setup.at(0).aggregate(agg_pvss, id_vec);
    for(size_t i=0; i<n;i++) {
        if(!setup.at(i).verify_aggregation(agg)) {
            throw std::runtime_error("aggregation verification failed");
        }
        // DONE: Serialization test
        ss_serialize.str(std::string{});
        ss_serialize << agg;
        auto bytes = ss_serialize.str();
        optrand_crypto::pvss_aggregate_t agg2;
        ss_deserialize.str(bytes);
        ss_deserialize >> agg2;
        if(!setup.at(i).verify_aggregation(agg)) {
            throw std::runtime_error("serialized aggregation verification failed");
        }
    }

    std::vector<optrand_crypto::decryption_t> decs;
    decs.reserve(n);
    for(size_t i=0; i<n;i++) {
      auto decryption = setup.at(i).decrypt(agg);
      decs.push_back(decryption);
    }
    for(size_t i=0;i<n;i++) {
      // DONE: Serialization test
      ss_serialize.str(std::string{});
      ss_serialize << decs.at(i);
      auto bytes = ss_serialize.str();
      optrand_crypto::decryption_t dec2;
      ss_deserialize.str(bytes);
      ss_deserialize >> dec2;

      for (size_t j = 0; j < n; j++) {
        // Everyone verify dec[i]
        if (!setup.at(j).verify_decryption(agg, decs.at(i))) {
          throw std::runtime_error("decryption verification failed");
        }
        if (!setup.at(j).verify_decryption(agg, dec2)) {
          throw std::runtime_error("serialized decryption verification failed");
        }
      }
    }
    // DONE: Reconstruction
    std::vector<optrand_crypto::decryption_t> dec_subset;
    dec_subset.reserve(f+1);
    for(size_t i=0; i<=f;i++) {
        dec_subset.push_back(decs.at(i));
    }
    std::vector<optrand_crypto::beacon_t> beacons;
    beacons.reserve(n);
    for(size_t i=0;i<n;i++) {
      auto beacon = setup.at(0).reconstruct(dec_subset);
      // TODO: Serialization test
      ss_serialize.str(std::string{});
      ss_serialize << beacon;
      auto bytes = ss_serialize.str();
      optrand_crypto::beacon_t beacon2;
      ss_deserialize.str(bytes);
      ss_deserialize >> beacon2;
      for (size_t j = 0; j < n; j++) {
        if (!setup.at(j).verify_beacon(agg, beacon)) {
          throw std::runtime_error("beacon verification failed");
        }
        if (!setup.at(j).verify_beacon(agg, beacon2)) {
          throw std::runtime_error("serialized beacon verification failed");
        }
      }
      beacons.push_back(beacon);
    }

    std::cout << "All is well!" << std::endl;
    return 0;
}