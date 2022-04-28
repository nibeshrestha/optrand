/**
 * Copyright 2018 VMware
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HOTSTUFF_ENT_H
#define _HOTSTUFF_ENT_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cstddef>
#include <ios>
#include <crypto2/pvss/Aggregation.hpp>

#include "salticidae/netaddr.h"
#include "salticidae/ref.h"
#include "hotstuff/type.h"
#include "hotstuff/util.h"
#include "hotstuff/crypto.h"

namespace hotstuff {

enum EntityType {
    ENT_TYPE_CMD = 0x0,
    ENT_TYPE_BLK = 0x1
};

enum MessageType {
    PROPOSAL = 0x0,
    CERT = 0x1
};

struct ReplicaInfo {
    ReplicaID id;
    salticidae::NetAddr addr;
    pubkey_bt pubkey;

    ReplicaInfo(ReplicaID id,
                const salticidae::NetAddr &addr,
                pubkey_bt &&pubkey):
        id(id), addr(addr), pubkey(std::move(pubkey)) {}

    ReplicaInfo(const ReplicaInfo &other):
        id(other.id), addr(other.addr),
        pubkey(other.pubkey->clone()) {}

    ReplicaInfo(ReplicaInfo &&other):
        id(other.id), addr(other.addr),
        pubkey(std::move(other.pubkey)) {}
};

class ReplicaConfig {
    std::unordered_map<ReplicaID, ReplicaInfo> replica_map;

    public:
    std::vector<ReplicaID> active_ids;
    // A duplicate array for the crypto library as it uses size_t
    std::vector<size_t> active_indices;
    size_t nreplicas;
    size_t nactive_replicas;
    size_t nmajority;
    size_t nresponsive;
    size_t nreconthres;

    double delta;

    ReplicaConfig(): nreplicas(0), nactive_replicas(0), nmajority(0), nresponsive(0), nreconthres(0), delta(0) {}

    void add_replica(ReplicaID rid, const ReplicaInfo &info) {
        replica_map.insert(std::make_pair(rid, info));
        active_ids.push_back(rid);
        active_indices.push_back(rid);
        nreplicas++;
        nactive_replicas++;
    }
    // Used to add new replica during program execution; for reconfiguration
    void add_passive_replica(ReplicaID rid, const ReplicaInfo &info){
        replica_map.insert(std::make_pair(rid, info));
    }

    void activate_replica(ReplicaID rid){
        active_ids.push_back(rid);
        active_indices.push_back(rid);
        nactive_replicas++;
    }

    void remove_replica(ReplicaID rid){
        active_ids.erase(std::remove(active_ids.begin(), active_ids.end(), rid), active_ids.end());
        active_indices.erase(std::remove(active_indices.begin(), active_indices.end(), rid), active_indices.end());
        nactive_replicas--;
    }

    const ReplicaInfo &get_info(ReplicaID rid) const {
        auto it = replica_map.find(rid);
        if (it == replica_map.end())
            throw HotStuffError("rid %s not found",
                    get_hex(rid).c_str());
        return it->second;
    }

    const PubKey &get_pubkey(ReplicaID rid) const {
        return *(get_info(rid).pubkey);
    }

    const salticidae::NetAddr &get_addr(ReplicaID rid) const {
        return get_info(rid).addr;
    }

    ReplicaID get_replica_id(uint32_t idx){
        return active_ids.at(idx);
    }

    void set_active_ids(std::vector<ReplicaID> &_active_ids){
        active_ids = _active_ids;
        nactive_replicas = active_ids.size();
        active_indices.clear();
        for(auto rid: _active_ids)
            active_indices.push_back((size_t) rid);
    }

};

class Block;
class HotStuffCore;

using block_t = salticidae::ArcObj<Block>;

class Command: public Serializable {
    friend HotStuffCore;
    public:
    virtual ~Command() = default;
    virtual const uint256_t &get_hash() const = 0;
    virtual bool verify() const = 0;
    virtual operator std::string () const {
        DataStream s;
        s << "<cmd id=" << get_hex10(get_hash()) << ">";
        return std::move(s);
    }
};

using command_t = ArcObj<Command>;

template<typename Hashable>
inline static std::vector<uint256_t>
get_hashes(const std::vector<Hashable> &plist) {
    std::vector<uint256_t> hashes;
    for (const auto &p: plist)
        hashes.push_back(p->get_hash());
    return std::move(hashes);
}

class Block {
    friend HotStuffCore;
    std::vector<uint256_t> parent_hashes;
    std::vector<uint256_t> cmds;
    quorum_cert_bt qc;
    uint256_t qc_ref_hash;
    bytearray_t extra;

    bool contains_join_request;
    ReplicaID joinReplicaID;
    bytearray_t jreq_agg;

    /* the following fields can be derived from above */
    uint256_t hash;
    std::vector<block_t> parents;
    block_t qc_ref;
    quorum_cert_bt self_qc;
    uint32_t height;
    bool delivered;
    int8_t decision;

    std::unordered_set<ReplicaID> voted;
    std::unordered_set<ReplicaID> acked;

    uint32_t view;
    optrand_crypto::pvss_aggregate_t pvss_agg;

    uint256_t _get_hash();

    public:
    Block():
        qc(nullptr),
        hash(_get_hash()),
        qc_ref(nullptr),
        self_qc(nullptr), height(0),
        delivered(false), decision(0), view(0) {}

    Block(bool delivered, int8_t decision):
        qc(nullptr),
        hash(_get_hash()),
        qc_ref(nullptr),
        self_qc(nullptr), height(0), view(0),
        delivered(delivered), decision(decision),
        contains_join_request(false) {}

    Block(const std::vector<block_t> &parents,
        const std::vector<uint256_t> &cmds,
        quorum_cert_bt &&qc,
        bytearray_t &&extra,
        uint32_t height,
        const block_t &qc_ref,
        quorum_cert_bt &&self_qc,
        int8_t decision = 0):
            parent_hashes(get_hashes(parents)),
            cmds(cmds),
            qc(std::move(qc)),
            qc_ref_hash(qc_ref ? qc_ref->get_hash() : uint256_t()),
            extra(std::move(extra)),
            hash(_get_hash()),
            parents(parents),
            qc_ref(qc_ref),
            self_qc(std::move(self_qc)),
            height(height),
            delivered(0),
            decision(decision),
            contains_join_request(false){}

    void serialize(DataStream &s) const;

    void unserialize(DataStream &s, HotStuffCore *hsc);

    const std::vector<uint256_t> &get_cmds() const {
        return cmds;
    }

    const std::vector<block_t> &get_parents() const {
        return parents;
    }

    const std::vector<uint256_t> &get_parent_hashes() const {
        return parent_hashes;
    }

    const uint256_t &get_hash() const { return hash; }

    bool verify(const ReplicaConfig &config) const;
    promise_t verify(const ReplicaConfig &config, VeriPool &vpool) const;

    int8_t get_decision() const { return decision; }

    bool is_delivered() const { return delivered; }

    uint32_t get_height() const { return height; }

    const quorum_cert_bt &get_qc() const { return qc; }

    const block_t &get_qc_ref() const { return qc_ref; }

    const bytearray_t &get_extra() const { return extra; }

    const uint256_t &get_qc_ref_hash() const { return qc_ref_hash; }

    operator std::string () const {
        DataStream s;
        s << "<block "
          << "id="  << get_hex10(hash) << " "
          << "height=" << std::to_string(height) << " "
          << "parent=" << get_hex10(parent_hashes[0]) << " "
          << "qc_ref=" << (qc_ref ? get_hex10(qc_ref->get_hash()) : "null") << ">";
        return std::move(s);
    }

    void set_join_request(bool _contains_join_req, ReplicaID jreplicaID, bytearray_t &&_jreq_agg){
        contains_join_request = _contains_join_req;
        joinReplicaID = jreplicaID;
        jreq_agg = std::move(_jreq_agg);
    }

    bool has_join_request(){
        return contains_join_request;
    }

    ReplicaID get_join_replicaID(){
        return joinReplicaID;
    }

    const bytearray_t &get_jreq_agg(){
        return jreq_agg;
    }
};

struct BlockHeightCmp {
    bool operator()(const block_t &a, const block_t &b) const {
        return a->get_height() < b->get_height();
    }
};

class EntityStorage {
    std::unordered_map<const uint256_t, block_t> blk_cache;
    std::unordered_map<const uint256_t, command_t> cmd_cache;
    public:
    bool is_blk_delivered(const uint256_t &blk_hash) {
        auto it = blk_cache.find(blk_hash);
        if (it == blk_cache.end()) return false;
        return it->second->is_delivered();
    }

    bool is_blk_fetched(const uint256_t &blk_hash) {
        return blk_cache.count(blk_hash);
    }

    block_t add_blk(Block &&_blk, const ReplicaConfig &/*config*/) {
        //if (!_blk.verify(config))
        //{
        //    HOTSTUFF_LOG_WARN("invalid %s", std::string(_blk).c_str());
        //    return nullptr;
        //}
        block_t blk = new Block(std::move(_blk));
        return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
    }

    const block_t &add_blk(const block_t &blk) {
        return blk_cache.insert(std::make_pair(blk->get_hash(), blk)).first->second;
    }

    block_t find_blk(const uint256_t &blk_hash) {
        auto it = blk_cache.find(blk_hash);
        return it == blk_cache.end() ? nullptr : it->second;
    }

    bool is_cmd_fetched(const uint256_t &cmd_hash) {
        return cmd_cache.count(cmd_hash);
    }

    const command_t &add_cmd(const command_t &cmd) {
        return cmd_cache.insert(std::make_pair(cmd->get_hash(), cmd)).first->second;
    }

    command_t find_cmd(const uint256_t &cmd_hash) {
        auto it = cmd_cache.find(cmd_hash);
        return it == cmd_cache.end() ? nullptr: it->second;
    }

    size_t get_cmd_cache_size() {
        return cmd_cache.size();
    }
    size_t get_blk_cache_size() {
        return blk_cache.size();
    }

    bool try_release_cmd(const command_t &cmd) {
        if (cmd.get_cnt() == 2) /* only referred by cmd and the storage */
        {
            const auto &cmd_hash = cmd->get_hash();
            cmd_cache.erase(cmd_hash);
            return true;
        }
        return false;
    }

    bool try_release_blk(const block_t &blk) {
        if (blk.get_cnt() == 2) /* only referred by blk and the storage */
        {
            const auto &blk_hash = blk->get_hash();
#ifdef HOTSTUFF_PROTO_LOG
            HOTSTUFF_LOG_INFO("releasing blk %.10s", get_hex(blk_hash).c_str());
#endif
//            for (const auto &cmd: blk->get_cmds())
//                try_release_cmd(cmd);
            blk_cache.erase(blk_hash);
            return true;
        }
#ifdef HOTSTUFF_PROTO_LOG
        else
            HOTSTUFF_LOG_INFO("cannot release (%lu)", blk.get_cnt());
#endif
        return false;
    }
};

}

#endif
