#pragma once

#include "Utils.hpp"

namespace optrand_crypto {
class SystemConfig: public printable {
public:
    virtual size_t num_replicas() const = 0;
    virtual size_t num_faults() const = 0;
    virtual bool is_valid() const { return num_faults() < num_replicas(); };

    std::string pretty_print() const override {
        std::stringstream ss;
        ss << "SystemConfig { # Replicas: " 
            << num_replicas()
            << ", # Faults: "
            << num_faults() 
            << " }";
        return ss.str();
    }

};

class SyncSystemConfig: public SystemConfig {
public:
    size_t num_replicas_;
    size_t num_faults_;

    // returns a default structure for (n,(n-1)/2) system
    static SyncSystemConfig FromNumReplicas(size_t num_replicas) {
        return SyncSystemConfig{num_replicas, (num_replicas-1)/2};
    }

    // Given the value for f, return a (2f+1,f) config
    static SyncSystemConfig FromNumFaults(size_t num_faults) {
        return SyncSystemConfig{(2*num_faults)+1, num_faults};
    }

// Implement SystemConfig
public:
    size_t num_replicas() const override {
        return num_replicas_;
    }

    size_t num_faults() const override {
        return num_faults_;
    }

    // Check whether this structure is valid
    bool is_valid() const override {
        return 2*num_faults() < num_replicas();
    }


// Implement printable
public:
    std::string pretty_print() const override {
        std::stringstream ss;
        ss << "SyncSystemConfig { # Replicas: " 
            << num_replicas()
            << ", # Faults: "
            << num_faults()
            << " }";
        return ss.str();
    }

private:
    SyncSystemConfig(size_t num_replicas, size_t num_faults): SystemConfig{}, num_replicas_{num_replicas}, num_faults_{num_faults} {}
};

inline std::ostream& operator<< (std::ostream& os, const SyncSystemConfig& self) {
    os << self.num_replicas() << std::endl;
    os << self.num_faults() << std::endl;
    return os;
}

inline std::istream& operator>> (std::istream& in, SyncSystemConfig& self) {
    in >> self.num_replicas_;
    libff::consume_OUTPUT_NEWLINE(in);

    in >> self.num_faults_;
    libff::consume_OUTPUT_NEWLINE(in);
    return in;
}

}