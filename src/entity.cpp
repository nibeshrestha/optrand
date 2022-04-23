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

#include "hotstuff/entity.h"
#include "hotstuff/consensus.h"
#include "hotstuff/hotstuff.h"

namespace hotstuff {

void Block::serialize(DataStream &s) const {
    s << htole((uint32_t)parent_hashes.size());
    for (const auto &hash: parent_hashes)
        s << hash;
    s << htole((uint32_t)cmds.size());
    for (auto cmd: cmds)
        s << cmd;
    if (qc)
        s << (uint8_t)1 << *qc << qc_ref_hash;
    else
        s << (uint8_t)0;
    s << htole((uint32_t)extra.size()) << extra;
    s << view;
    s << contains_join_request;
    if(contains_join_request) {
        s << joinReplicaID;
        s << htole((uint32_t)jreq_agg.size()) << jreq_agg;
    }
}

void Block::unserialize(DataStream &s, HotStuffCore *hsc) {
    uint32_t n;
    uint8_t flag;
    s >> n;
    n = letoh(n);
    parent_hashes.resize(n);
    for (auto &hash: parent_hashes)
        s >> hash;
    s >> n;
    n = letoh(n);
    cmds.resize(n);
    for (auto &cmd: cmds)
        s >> cmd;
//    for (auto &cmd: cmds)
//        cmd = hsc->parse_cmd(s);
    s >> flag;
    if (flag)
    {
        qc = hsc->parse_quorum_cert(s);
        s >> qc_ref_hash;
    } else qc = nullptr;
    s >> n;
    n = letoh(n);
    if (n == 0)
        extra.clear();
    else
    {
        auto base = s.get_data_inplace(n);
        extra = bytearray_t(base, base + n);
    }
    s >> view;
    s >> contains_join_request;
    if (contains_join_request) {
        s >> joinReplicaID;
        s >> n;
        n = letoh(n);
        auto base = s.get_data_inplace(n);
        jreq_agg = bytearray_t(base, base + n);
    }

    this->hash = _get_hash();
}

bool Block::verify(const ReplicaConfig &config) const {
    if (qc && (!qc->verify(config) ||
                qc->get_obj_hash() != Vote::proof_obj_hash(qc_ref_hash))) return false;
    return true;
}

promise_t Block::verify(const ReplicaConfig &config, VeriPool &vpool) const {
    return (qc ?
        (qc->get_obj_hash() != Vote::proof_obj_hash(qc_ref_hash) ?
            promise_t([](promise_t &pm) { pm.resolve(false); }) :
            qc->verify(config, vpool)) :
    promise_t([](promise_t &pm) { pm.resolve(true); }));
}

uint256_t Block::_get_hash() {
    DataStream s;
    s << htole((uint32_t)parent_hashes.size());
    for (const auto &hash: parent_hashes)
        s << hash;
    s << htole((uint32_t)cmds.size());
    for (auto cmd: cmds)
        s << cmd;
    if (qc)
        s << (uint8_t)1 << qc_ref_hash;
    else
        s << (uint8_t)0;
    s << htole((uint32_t)extra.size()) << extra;
    return s.get_hash();
}

}
