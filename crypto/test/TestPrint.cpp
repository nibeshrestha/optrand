#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "crypto/pvss/Aggregation.hpp"
#include "crypto/pvss/Beacon.hpp"
#include "crypto/pvss/Config.hpp"
#include "crypto/pvss/Decryption.hpp"
#include "crypto/pvss/Factory.hpp"
#include "crypto/pvss/Precomputes.hpp"
#include "crypto/pvss/pvss.hpp"
#include "crypto/pvss/Utils.hpp"

#include "crypto/pvss/Serialization.hpp"

// #define TEST_API

// Some global helpers
std::stringstream ss_serialize;
std::stringstream ss_deserialize;

std::vector<size_t> random_subset(
  const std::vector<size_t> all_indices, 
  size_t subset_size
)
{
  assert(subset_size <= all_indices.size());

  std::vector<size_t> all_indices_copy(all_indices.size());
  std::copy(all_indices.begin(), all_indices.end(), all_indices_copy.begin());

  std::random_shuffle(all_indices_copy.begin(), all_indices_copy.end());

  std::vector<size_t> subset_indices;
  subset_indices.reserve(subset_size);
  for(size_t i = 0 ; i < subset_size ; i++ ) {
    subset_indices.push_back(all_indices_copy.at(i));
  }

  return subset_indices;
}

void test_run(
  const std::vector<size_t> &active_nodes,
  const std::vector<optrand_crypto::Context>& setup
)
{
  size_t n = active_nodes.size();
  size_t f = (n-1)/2;
  size_t total_nodes = setup.size();

  // Create PVSS vectors
  std::vector<optrand_crypto::pvss_sharing_t> pvss_vec;
  pvss_vec.reserve(total_nodes);
  for (size_t i = 0; i < total_nodes; i++) {
    auto sharing = setup.at(i).create_sharing();
    pvss_vec.push_back(sharing);
    // DONE: Serialization test
    ss_serialize.str(std::string{});
    ss_serialize << sharing;
    auto bytes = ss_serialize.str();
    ss_deserialize.str(bytes);
    optrand_crypto::pvss_sharing_t pvss2;
    ss_deserialize >> pvss2;
    for (size_t j = 0; j < total_nodes; j++) {
      if (!setup.at(j).verify_sharing(sharing)) {
        throw std::runtime_error("Verification failed");
      }
    }
    for (size_t j = 0; j < total_nodes; j++) {
      if (!setup.at(j).verify_sharing(pvss2)) {
        throw std::runtime_error("serialized Verification failed");
      }
    }
  }

  // Create aggregated PVSS
  std::vector<optrand_crypto::pvss_sharing_t> agg_pvss;
  std::vector<size_t> id_vec;
  agg_pvss.reserve(f + 1);
  id_vec.reserve(f + 1);
  for (size_t i = 0; i <= f; i++) {
    // Use the first f+1 active nodes for testing
    agg_pvss.push_back(pvss_vec.at(active_nodes.at(i)));
    id_vec.push_back(active_nodes.at(i));
  }
  auto agg = setup
                .at(active_nodes.at(0))
                .aggregate(agg_pvss, id_vec); // First active node aggregates
  for (size_t i = 0; i < total_nodes; i++) {
    if (!setup.at(i).verify_aggregation(agg)) {
      throw std::runtime_error("aggregation verification failed");
    }
    // DONE: Serialization test
    ss_serialize.str(std::string{});
    ss_serialize << agg;
    auto bytes = ss_serialize.str();
    optrand_crypto::pvss_aggregate_t agg2;
    ss_deserialize.str(bytes);
    ss_deserialize >> agg2;
    if (!setup.at(i).verify_aggregation(agg)) {
      throw std::runtime_error("serialized aggregation verification failed");
    }
  }

  // Decrypt the shares
  std::vector<optrand_crypto::decryption_t> decs;
  decs.reserve(n);
  for (auto& idx: active_nodes) {
    auto decryption = setup.at(idx).decrypt(agg);
    decs.push_back(decryption);
  }
  for (size_t i = 0; i < n; i++) {
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

  // Reconstruction of the beacon
  std::vector<optrand_crypto::decryption_t> dec_subset;
  dec_subset.reserve(f + 1);
  for (size_t i = 0; i <= f; i++) {
    dec_subset.push_back(decs.at(i));
  }
  std::vector<optrand_crypto::beacon_t> beacons;
  beacons.reserve(total_nodes);
  for (size_t i = 0; i < n; i++) {
    auto beacon = setup.at(i).reconstruct(dec_subset);
    // DONE: Serialization test
    ss_serialize.str(std::string{});
    ss_serialize << beacon;
    auto bytes = ss_serialize.str();
    optrand_crypto::beacon_t beacon2;
    ss_deserialize.str(bytes);
    ss_deserialize >> beacon2;
    for (size_t j = 0; j < total_nodes; j++) {
      if (!setup.at(j).verify_beacon(agg, beacon)) {
        throw std::runtime_error("beacon verification failed");
      }
      if (!setup.at(j).verify_beacon(agg, beacon2)) {
        throw std::runtime_error("serialized beacon verification failed");
      }
    }
    beacons.push_back(beacon);
  }
}

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

int main() {
    std::cout << "Namaskara!" << std::endl;

    // Settings
    const size_t n = 4;
    const size_t total_nodes = n+2; // 2 extra nodes to test reconfiguration

    // Must be called before using any optrand_crypto function
    optrand_crypto::initialize();

    // Create a system config
    optrand_crypto::SyncSystemConfig conf = optrand_crypto::SyncSystemConfig::FromNumReplicas(n);
    std::cout << conf.pretty_print() << std::endl;

    // Create a factory
    auto factory = optrand_crypto::Factory(std::move(conf));

    // Get context for all the nodes
    auto setup = factory.getContext(total_nodes);

    // Initialize the precomputations for every context
    for(auto& context: setup) {
      context.initialize_precomputations(total_nodes);
    }
    

    std::vector<size_t> all_indices, active_nodes;
    all_indices.reserve(total_nodes);
    active_nodes.reserve(n);
    for(size_t i = 0 ; i < total_nodes; i++) {
      all_indices.push_back(i);
      if ( i < n) {
        active_nodes.push_back(i);
      }
    }

    test_run(active_nodes, setup);

    // Change the active node set
    auto reshuffled = random_subset(all_indices, n);
    for(size_t i=0; i<total_nodes;i++) {
      setup.at(i).active_nodes = reshuffled;
    }
    // Test changed active node set
    std::cout << "Using active nodes: ";
    printVector(reshuffled);
    test_run(reshuffled, setup);

    std::cout << "All is well!" << std::endl;
    return 0;
}