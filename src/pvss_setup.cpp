//
// Created by nibesh on 4/7/22.
//

#include <error.h>
#include <vector>
#include <iostream>
#include <fstream>

#include "salticidae/util.h"
#include "crypto2/pvss/Factory.hpp"

using namespace optrand_crypto;
using namespace std;
using namespace salticidae;


int main(int argc, char **argv) {
    Config config("hotstuff.conf");

    auto opt_n = Config::OptValInt::create(1);
    config.add_opt("num", opt_n, Config::SET_VAL);
    config.parse(argc, argv);

    int n = opt_n->get();
    if (n < 1)
        error(1, 0, "n must be >0");

    optrand_crypto::initialize();

    auto conf = SyncSystemConfig::FromNumReplicas(n);
    auto factory = Factory(std::move(conf));
    auto setup = factory.getContext();

    for (int i = 0; i < n; i++) {
        std::string filename = "pvss-sec" + std::to_string(i) + ".conf";
        ofstream fp;
        fp.open(filename);
        fp << setup.at(i);
        fp.close();
    }

    std::vector<pvss_sharing_t> pvss_vec;
    std::vector<size_t> id_vec;
    std::vector<pvss_aggregate_t> agg_vec;
    int k, f = (n - 1) / 2, idx = 0;

    // buffer 2n aggregated transcripts
    for (int i = 0; i < 2*n; i++) {
        idx = i % n;
        for (int j = i; j < i + f + 1; j++) {
            k = j % n;
            auto sharing = setup.at(k).create_sharing();
            pvss_vec.push_back(sharing);
            id_vec.push_back(k);
        }

        auto agg = setup.at(idx).aggregate(pvss_vec, id_vec);
        if (!setup.at(idx).verify_aggregation(agg)) {
            throw std::runtime_error("aggregation verification failed");
        }

        agg_vec.push_back(agg);

        pvss_vec.clear();
        id_vec.clear();
    }

    std::ofstream file;
    file.open("pvss-setup.dat");

    optrand_crypto::serializeVector(file, agg_vec);
    file.close();


//    std::ifstream dat_stream;
//    dat_stream.open("pvss-setup.dat");
//    if (dat_stream.fail())
//        throw std::runtime_error("PVSS Setup File Error!");
//
//    std::vector<optrand_crypto::pvss_aggregate_t> agg_vec2;
//    optrand_crypto::deserializeVector(dat_stream, agg_vec2);
//    dat_stream.close();
//
//    std::cout << "Size of output vector " << agg_vec2.size() << std::endl;

}
