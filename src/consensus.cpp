/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
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

#include <cassert>
#include <stack>
#include <cmath>
#include <algorithm>
#include <fstream>

#include "hotstuff/util.h"
#include "hotstuff/consensus.h"

#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN
#define LOG_PROTO HOTSTUFF_LOG_PROTO

namespace hotstuff {

/* The core logic of HotStuff, is fairly simple :). */
/*** begin HotStuff protocol logic ***/
HotStuffCore::HotStuffCore(ReplicaID id,
                            privkey_bt &&priv_key,
                            const optrand_crypto::Context &pvss_ctx,
                            const std::string setup_dat_file):
        b0(new Block(true, 1)),
        b_exec(b0),
        vheight(0),
        view(0),
        view_trans(false),
        blame_qc(nullptr),
        priv_key(std::move(priv_key)),
        tails{b0},
        vote_disabled(false),
        id(id),
        pvss_context(pvss_ctx),
        storage(new EntityStorage()) {

        storage->add_blk(b0);
        std::ifstream dat_stream;
        dat_stream.open(setup_dat_file);
        if(dat_stream.fail()) {
            throw std::runtime_error("PVSS Setup File Error!");
        }

        std::vector<optrand_crypto::pvss_aggregate_t> agg_vec;
        optrand_crypto::deserializeVector(dat_stream, agg_vec);
        dat_stream.close();

        for(uint32_t i = 0; i < agg_vec.size(); i++) {
            agg_queue[i] = agg_vec[i];

            // Replica 0 is the proposer of view 1
            view_agg_transcripts[i+1] = agg_vec[i];
        }

    }

void HotStuffCore::sanity_check_delivered(const block_t &blk) {
    if (!blk->delivered)
        throw std::runtime_error("block not delivered");
}

block_t HotStuffCore::get_delivered_blk(const uint256_t &blk_hash) {
    block_t blk = storage->find_blk(blk_hash);
    if (blk == nullptr || !blk->delivered)
        throw std::runtime_error("block not delivered");
    return std::move(blk);
}

bool HotStuffCore::on_deliver_blk(const block_t &blk) {
    if (blk->delivered)
    {
        LOG_WARN("attempt to deliver a block twice");
        return false;
    }
    blk->parents.clear();
    for (const auto &hash: blk->parent_hashes)
        blk->parents.push_back(get_delivered_blk(hash));
    blk->height = blk->parents[0]->height + 1;

    if (blk->qc)
    {
        block_t _blk = storage->find_blk(blk->qc_ref_hash);
        if (_blk == nullptr)
            throw std::runtime_error("block referred by qc not fetched");
        blk->qc_ref = std::move(_blk);
    } // otherwise blk->qc_ref remains null

    for (auto pblk: blk->parents) tails.erase(pblk);
    tails.insert(blk);

    blk->delivered = true;
    LOG_DEBUG("deliver %s", std::string(*blk).c_str());
    return true;
}

void HotStuffCore::update_hqc(const block_t &_hqc, const quorum_cert_bt &qc) {
    assert(qc->get_obj_hash() == _hqc->get_hash());
    if (qc->get_view() > hqc.second->get_view())
    {
        hqc = std::make_pair(_hqc, qc->clone());
        on_hqc_update();
    }
}

void HotStuffCore::check_commit(const block_t &blk) {
    std::vector<block_t> commit_queue;
    block_t b;
    for (b = blk; b->height > b_exec->height; b = b->parents[0])
    { /* TODO: also commit the uncles/aunts */
        commit_queue.push_back(b);
    }
    if (b != b_exec && b->decision != 1)
        throw std::runtime_error("safety breached :( " +
                                std::string(*blk) + " " +
                                std::string(*b_exec));
    for (auto it = commit_queue.rbegin(); it != commit_queue.rend(); it++)
    {
        const block_t &blk = *it;
        blk->decision = 1;
        do_consensus(blk);
        LOG_PROTO("commit %s", std::string(*blk).c_str());
//        for (size_t i = 0; i < blk->cmds.size(); i++)
//            do_decide(Finality(id, 1, i, blk->height,
//                                blk->cmds[i], blk->get_hash()));
    }
    b_exec = blk;
}

// 2. Responsive Vote
void HotStuffCore::_vote(const block_t &blk, ReplicaID proposer) {
    const auto &blk_hash = blk->get_hash();
    LOG_PROTO("vote for %s", get_hex10(blk_hash).c_str());
    Vote vote(id, blk_hash,
            create_part_cert(
                *priv_key, blk_hash, view), view, this);

    if (proposer == id)
        on_receive_vote(vote);

    if (proposer != id)
        do_vote(vote, proposer);
}


block_t HotStuffCore::on_propose(const std::vector<uint256_t> &cmds,
                            const std::vector<block_t> &parents,
                            bytearray_t &&extra) {
    if (view_trans)
    {
        LOG_WARN("PaceMaker tries to propose during view transition");
        return nullptr;
    }
    if (parents.empty())
        throw std::runtime_error("empty parents");
    for (const auto &_: parents) tails.erase(_);

    auto pvss_agg = view_agg_transcripts[view];
    // Convert aggregate PVSS to bytearray
    std::stringstream ss;
    ss.str(std::string{});
    ss << pvss_agg;

    auto str = ss.str();
    bytearray_t agg_bytes(str.begin(), str.end());

    /* create the new block */
    block_t bnew = storage->add_blk(
        new Block(parents, cmds,
            hqc.second->clone(), std::move(agg_bytes),
            parents[0]->height + 1,
            hqc.first,
            nullptr
        ));

    bnew-> view = view;

    const uint256_t bnew_hash = bnew->get_hash();
    bnew->self_qc = create_quorum_cert(bnew_hash, view);
    on_deliver_blk(bnew);
    Proposal prop(id, bnew, view, nullptr);
    LOG_PROTO("propose %s", std::string(*bnew).c_str());
    /* self-vote */
    if (bnew->height <= vheight)
        throw std::runtime_error("new block should be higher than vheight");
    vheight = bnew->height;
    finished_propose[bnew] = true;
    _vote(bnew, id);
    on_propose_(prop);

    last_proposed_view = view;

    /* broadcast to other replicas */
    do_broadcast_proposal(prop);
    on_receive_view_proposal_(view);
    return bnew;
}

void HotStuffCore::on_receive_proposal(const Proposal &prop) {
    if (view_trans) return;
    LOG_PROTO("got %s", std::string(prop).c_str());

    block_t bnew = prop.blk;
    if (finished_propose[bnew]) {
        LOG_PROTO("proposal finished %s", std::string(prop).c_str());
        return;
    }

    sanity_check_delivered(bnew);
    if (bnew->qc_ref)
        update_hqc(bnew->qc_ref, bnew->qc);
    bool opinion = false;
    auto &pslot = proposals[bnew->height];
    if (pslot.size() <= 1)
    {
        pslot.insert(bnew);
        if (pslot.size() > 1)
        {
            // TODO: put equivocating blocks in the Blame msg
            LOG_INFO("conflicting proposal detected, start blaming");
            //_blame();
        }
        else opinion = true;
    }
    // opinion = false if equivocating

    if (opinion)
    {
        block_t pref = hqc.first;
        block_t b;
        for (b = bnew;
            b->height > pref->height;
            b = b->parents[0]);
        if (b == pref) /* on the same branch */
            vheight = bnew->height;
        else
            opinion = false;
    }
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    if (bnew->qc_ref)
        on_qc_finish(bnew->qc_ref);

    std::string str(bnew->extra.begin(), bnew->extra.end());
    std::stringstream ss;
    ss.str(str);

    optrand_crypto::pvss_aggregate_t agg;

    ss >> agg;

    if(!pvss_context.verify_aggregation(agg)) {
        LOG_WARN("PVSS verification failed on_receive_proposal View: %d", view);
        return;
    }

    bnew->view = view;
    view_agg_transcripts[view] = agg;

    finished_propose[bnew] = true;

    on_receive_proposal_(prop);
    on_receive_view_proposal_(view);
    // check if the proposal extends the highest certified block
    if (opinion && !vote_disabled) _vote(bnew, prop.proposer);

    if(last_propose_delivered_view < view) _deliver_proposal(prop);

}

void HotStuffCore::on_receive_vote(const Vote &vote) {
    LOG_PROTO("got %s", std::string(vote).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    if (vote.view < view) return;
    block_t blk = get_delivered_blk(vote.blk_hash);
    assert(vote.cert);
    if (!finished_propose[blk])
    {
        // FIXME: fill voter as proposer as a quickfix here, may be inaccurate
        // for some PaceMakers
        //finished_propose[blk] = true;
//        on_receive_proposal(Proposal((view-1)%config.nreplicas, blk, view, nullptr));
    }
    size_t qsize = blk->voted.size();
    if (qsize >= config.nresponsive) return;
    if (!blk->voted.insert(vote.voter).second)
    {
        LOG_WARN("duplicate vote for %s from %d", get_hex10(vote.blk_hash).c_str(), vote.voter);
        return;
    }
    auto &qc = blk->self_qc;
    if (qc == nullptr)
    {
        qc = create_quorum_cert(blk->get_hash(), view);
    }
    qc->add_part(vote.voter, *vote.cert);
    if (qsize + 1 == config.nresponsive)
    {
        qc->compute();
        update_hqc(blk, qc);
        on_qc_finish(blk);
        do_broadcast_qc(QC(qc->clone(), this));
        _ack(blk);
    }
}

void HotStuffCore::on_receive_status(const Status &status) {
    LOG_PROTO("got %s", std::string(status).c_str());
    if(status.view < view || last_proposed_view >= view)
        return;

    size_t qsize = status_received[status.view].size();
    if (qsize >= config.nmajority) return;

    block_t blk = get_delivered_blk(status.qc->get_obj_hash());

    auto &blk_qc = blk->self_qc;
    blk_qc = status.qc->clone();
    update_hqc(blk, blk_qc);

    if (!status_received[status.view].insert(status.replicaID).second)
    {
        LOG_WARN("duplicate status for view %d from %d", status.view, status.replicaID);
        return;
    }

    bool is_hqc = get_hqc_qc()->get_view() + 1 == view;

    if (qsize + 1 == config.nmajority && is_hqc) do_propose();
}


void HotStuffCore::on_receive_qc(const quorum_cert_bt &qc){
    uint32_t _view = qc->get_view();
    LOG_PROTO("got QC view = %d", _view);
    if (_view < view || last_view_cert_received >= view) return;
    last_view_cert_received = _view;

    block_t blk = get_delivered_blk(qc->get_obj_hash());

    auto &blk_qc = blk->self_qc;
    blk_qc = qc->clone();
    update_hqc(blk, blk_qc);

    _deliver_cert(qc);
    //
    on_receive_qc_(view);
    _ack(blk);
    _try_enter_view();
}

void HotStuffCore::on_receive_ack(const Ack &ack) {
    LOG_PROTO("got %s", std::string(ack).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    if (ack.view < view) return;
    block_t blk = get_delivered_blk(ack.blk_hash);
    assert(ack.cert);
    if (!finished_propose[blk])
    {
//        on_receive_proposal(Proposal((view-1)%config.nreplicas, blk, view, nullptr));
    }
    size_t qsize = blk->acked.size();
    if (qsize >= config.nresponsive) return;
    if (!blk->acked.insert(ack.voter).second)
    {
//        LOG_WARN("duplicate ack for %s from %d", get_hex10(ack.blk_hash).c_str(), ack.voter);
        return;
    }
    auto &qc = blk->self_qc;

    if (qsize + 1 == config.nresponsive)
    {
        check_commit(blk);
        _broadcast_share(view);
    }
}

void HotStuffCore::_update_agg_queue(const uint32_t _view){
    if(_view < config.nmajority) return;
    auto to_update_view = _view - config.nmajority + 1;
    auto to_update_proposer = get_proposer(to_update_view);
    agg_queue[to_update_proposer] = view_agg_transcripts[to_update_view];

}

void HotStuffCore::on_receive_beacon(const Beacon &beacon){
    LOG_PROTO("got %s", std::string(beacon).c_str());
    if (beacon.view < view || last_view_beacon_received <= beacon.view) return;

    std::string str(beacon.bt.begin(), beacon.bt.end());
    std::stringstream ss;
    ss.str(str);

    optrand_crypto::beacon_t beacon1;
    ss >> beacon1;

    auto proposer = get_proposer(beacon.view);
    if(!pvss_context.verify_beacon(agg_queue[proposer], beacon1)){
        throw std::runtime_error("Beacon Verification failed.");
        return;
    }
    last_view_beacon_received = view;
    _try_enter_view();
}

void HotStuffCore::_ack(const block_t &blk){
    const auto &blk_hash = blk->get_hash();
    LOG_PROTO("ack for %s", get_hex10(blk_hash).c_str());
    Ack ack(id, blk_hash,create_part_cert(*priv_key, blk_hash, view), view, this);

    on_receive_ack(ack);
    do_broadcast_ack(ack);
}


void HotStuffCore::_broadcast_share(const uint32_t _view){
    ReplicaID proposer = get_proposer(_view);

    auto agg = agg_queue[proposer];
    auto decryption = pvss_context.decrypt(agg);

    std::stringstream ss;
    ss.str(std::string{});
    ss << decryption;

    auto str = ss.str();
    bytearray_t dec_bytes(str.begin(), str.end());

//    DataStream p;
//    p << view;

//    Share share(id, create_part_cert(*priv_key, p.get_hash(), view), view, std::move(dec_bytes), this);
    Share share(id, view, std::move(dec_bytes), this);

    on_receive_share(share);
    do_broadcast_share(share);

}

void HotStuffCore::on_receive_share(const Share &share){
    LOG_PROTO("got %s", std::string(share).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    if(share.view < view) return;
    size_t qsize = view_shares[share.view].size();

    if (qsize >= config.nmajority) return;

    std::string str(share.bt.begin(), share.bt.end());
    std::stringstream ss;
    ss.str(str);

    optrand_crypto::decryption_t dec_share;

    ss >> dec_share;

    ReplicaID proposer = get_proposer(share.view);

    if(!pvss_context.verify_decryption(agg_queue[proposer], dec_share)){
        throw std::runtime_error("Decryption Verification failed in View");
    }
    view_shares[share.view].push_back(dec_share);

    if (qsize + 1 == config.nmajority){
        //Todo: reconstruct the secret and broadcast it.
        auto beacon = pvss_context.reconstruct(view_shares[share.view]);

        if(!pvss_context.verify_beacon(agg_queue[proposer], beacon)){
            throw std::runtime_error("Beacon Verification failed.");
            return;
        }

        std::stringstream ss2;
        ss2.str(std::string{});
        ss2 << beacon;

        auto str = ss2.str();
        bytearray_t beacon_bytes(str.begin(), str.end());

        Beacon beacon1(id, view, std::move(beacon_bytes), this);
        do_broadcast_beacon(beacon1);
        // Not a warning; just using LOG_WARN to print beacon output.
        LOG_WARN("beacon view %d", view);
        last_view_shares_received = view;
        last_view_beacon_received = view;
        _try_enter_view();
    }
}

void HotStuffCore::_try_enter_view() {
    if(last_view_cert_received == view && (last_view_shares_received == view || last_view_beacon_received == view)) {
        _update_agg_queue(view);
        view += 1;
        enter_view(view);
        on_enter_view(view);
    }
}

void HotStuffCore::_deliver_proposal(const Proposal &prop) {
    last_propose_delivered_view = prop.view;
    DataStream s;
    s << prop;
    chunkarray_t chunk_array = Erasure::encode((int)config.nreconthres,
            (int)(config.nreplicas - config.nreconthres), 8, s);

    merkle::Tree tree;
    for(int i = 0; i < config.nreplicas; i++) {
        tree.insert(chunk_array[i]->get_data());
    }

    auto root = tree.root();
    bytearray_t bt;
    root.serialise(bt);
    uint256_t hash(bt);

    for(int i = 0; i < config.nreplicas; i++) {
        auto path = tree.path(i);
        bytearray_t patharr;
        path->serialise(patharr);
        if (i != id) {
            Echo echo(id, (uint32_t)i, prop.view, (uint32_t)MessageType::PROPOSAL, hash, patharr, chunk_array[i],
                    create_part_cert(*priv_key, hash, view), this);
            do_echo(echo, (ReplicaID)i);
        }else{
            Echo echo(id, (uint32_t)i, prop.view, (uint32_t)MessageType::PROPOSAL, hash, patharr, chunk_array[i],
                      create_part_cert(*priv_key, hash, view), this);
            do_broadcast_echo(echo);
        }
    }
}

void HotStuffCore::_deliver_cert(const quorum_cert_bt &qc){
    uint32_t qc_view = qc->get_view();
    last_cert_delivered_view = qc_view;

    DataStream s;
    s << *qc;
    chunkarray_t chunk_array = Erasure::encode((int)config.nreconthres,
                                               (int)(config.nreplicas - config.nreconthres), 8, s);

    merkle::Tree tree;
    for(int i = 0; i < config.nreplicas; i++) {
        tree.insert(chunk_array[i]->get_data());
    }

    auto root = tree.root();
    bytearray_t bt;
    root.serialise(bt);
    uint256_t hash(bt);

    for(int i = 0; i < config.nreplicas; i++) {
        auto path = tree.path(i);
        bytearray_t patharr;
        path->serialise(patharr);
        if (i != id) {
            Echo echo(id, (uint32_t)i, qc_view, (uint32_t)MessageType::CERT, hash, patharr, chunk_array[i],
                      create_part_cert(*priv_key, hash, view), this);
            do_echo2(echo, (ReplicaID)i);
        }else{
            Echo echo(id, (uint32_t)i, qc_view, (uint32_t)MessageType::CERT, hash, patharr, chunk_array[i],
                      create_part_cert(*priv_key, hash, view), this);
            do_broadcast_echo2(echo);
        }
    }
}

void HotStuffCore::on_receive_proposal_echo(const Echo &echo){
    LOG_PROTO("got %s", std::string(echo).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    uint32_t _view = echo.view;
    if(_view < view || last_propose_decoded_view >= view) return;
    if(!echo.verify()) return;
    
    size_t qsize = prop_chunks[_view].size();
    if (qsize > config.nreconthres) return;

    if (!prop_chunks[_view][echo.idx]) {
        bytearray_t bt(echo.merkle_root);
        merkle::Hash root(bt);
        merkle::Path path(echo.merkle_proof);

        if (path.verify(root)) {
            prop_chunks[_view][echo.idx] = echo.chunk;
            qsize++;
        }
    }

    unsigned long chunksize = echo.chunk->get_data().size();

    if(qsize == config.nreconthres) {
        last_propose_decoded_view = view;
        chunkarray_t arr;
        intarray_t erasures;

        for(int i=0; i < (int) config.nreplicas; i++){
            if (prop_chunks[_view][i]){
                arr.push_back(prop_chunks[_view][i]);
            }else{
                arr.push_back(new Chunk(echo.chunk->get_size(), bytearray_t (chunksize)));
                erasures.push_back(i);
            }
        }
        erasures.push_back(-1);

        DataStream d;
        Erasure::decode((int)config.nreconthres, (int)(config.nreplicas - config.nreconthres), 8, arr, erasures, d);
        prop_chunks.erase(_view);

        Proposal prop;
        prop.hsc = this;
        d >> prop;
        if(!prop.blk->delivered)
            on_deliver_blk(prop.blk);

        on_receive_proposal(prop);
    }
}

void HotStuffCore::on_receive_cert_echo(const Echo &echo){
    LOG_PROTO("got %s", std::string(echo).c_str());
    LOG_PROTO("now state: %s", std::string(*this).c_str());
    uint32_t _view = echo.view;
    if(_view < view || last_cert_decoded_view >= view) return;
    if(!echo.verify()) return;

    size_t qsize = qc_chunks[_view].size();
    if (qsize > config.nreconthres) return;

    if (!qc_chunks[_view][echo.idx]) {
        bytearray_t bt(echo.merkle_root);
        merkle::Hash root(bt);
        merkle::Path path(echo.merkle_proof);

        if (path.verify(root)) {
            qc_chunks[_view][echo.idx] = echo.chunk;
            qsize++;
        }
    }

    unsigned long chunksize = echo.chunk->get_data().size();

    if(qsize == config.nreconthres) {
        last_cert_decoded_view = view;
        chunkarray_t arr;
        intarray_t erasures;

        for(int i=0; i < (int) config.nreplicas; i++){
            if (qc_chunks[_view][i]){
                arr.push_back(qc_chunks[_view][i]);
            }else{
                arr.push_back(new Chunk(echo.chunk->get_size(), bytearray_t (chunksize)));
                erasures.push_back(i);
            }
        }
        erasures.push_back(-1);

        DataStream d;
        Erasure::decode((int)config.nreconthres, (int)(config.nreplicas - config.nreconthres), 8, arr, erasures, d);
        quorum_cert_bt qc = parse_quorum_cert(d);

        on_receive_qc(qc);
        qc_chunks.erase(_view);
    }
}

void HotStuffCore::on_receive_pvss_transcript(const PVSSTranscript &ptrans){
    LOG_PROTO("got %s", std::string(ptrans).c_str());
    size_t qsize = transcript_ids[ptrans.for_view].size();

    if(qsize >= config.nmajority) return;

    std::string str(ptrans.pvss_transcript.begin(), ptrans.pvss_transcript.end());
    std::stringstream ss;
    ss.str(str);

    optrand_crypto::pvss_sharing_t pvss_recv;

    ss >> pvss_recv;

    if(!pvss_context.verify_sharing(pvss_recv)){
        throw std::runtime_error("PVSS Verification failed in status");
    }

    view_transcripts[ptrans.for_view].push_back(pvss_recv);
    transcript_ids[ptrans.for_view].push_back((size_t) ptrans.replicaID);

    if (qsize + 1 == config.nmajority) {
        auto agg = pvss_context.aggregate(view_transcripts[ptrans.for_view], transcript_ids[ptrans.for_view]);
        if(!pvss_context.verify_aggregation(agg)){
            throw std::runtime_error("Aggregation Verification failed in status");
        }
        view_agg_transcripts[ptrans.for_view] = agg;
    }
}

void HotStuffCore::on_commit_timeout(const block_t &blk) { check_commit(blk); }

void HotStuffCore::on_propose_timeout() {
    //Todo: Add logic to propose.
    size_t qsize = status_received[view].size();

    if (qsize < config.nmajority){
        LOG_WARN("Insufficient status messages; Timing error");
    }

    do_propose();
}

void HotStuffCore::on_viewtrans_timeout() {
    // view change
    view++;
    view_trans = false;
    proposals.clear();
    blame_qc = create_quorum_cert(Blame::proof_obj_hash(view), view);
    blamed.clear();
//    set_blame_timer(3 * config.delta);
    on_view_change(); // notify the PaceMaker of the view change
    LOG_INFO("entering view %d", view);
    // send the highest certified block

}

/*** end HotStuff protocol logic ***/
void HotStuffCore::on_init(uint32_t nfaulty, double delta) {
    config.nmajority = config.nreplicas - nfaulty;
    config.nresponsive = (size_t) floor(3*config.nreplicas/4.0) + 1;
    config.nreconthres = (size_t) floor(config.nreplicas/4.0) + 1;
    config.delta = delta;
    view = 0;
    blame_qc = create_quorum_cert(Blame::proof_obj_hash(view), view);
    b0->qc = create_quorum_cert(b0->get_hash(), view);
    b0->qc->compute();
    b0->self_qc = b0->qc->clone();
    b0->qc_ref = b0;
    hqc = std::make_pair(b0, b0->qc->clone());
    view = 1;
    last_propose_delivered_view = 0;
    last_propose_decoded_view = 0;
    last_cert_decoded_view = 0;
    last_view_proposal_received = 0;
    last_view_cert_received = 0;
    last_view_shares_received = 0;
    last_cert_delivered_view = 0;
    last_proposed_view = 0;
    last_view_beacon_received = 0;

}

void HotStuffCore::prune(uint32_t staleness) {
    block_t start;
    /* skip the blocks */
    for (start = b_exec; staleness; staleness--, start = start->parents[0])
        if (!start->parents.size()) return;
    std::stack<block_t> s;
    start->qc_ref = nullptr;
    s.push(start);
    while (!s.empty())
    {
        auto &blk = s.top();
        if (blk->parents.empty())
        {
            storage->try_release_blk(blk);
            s.pop();
            continue;
        }
        blk->qc_ref = nullptr;
        s.push(blk->parents.back());
        blk->parents.pop_back();
    }
}

void HotStuffCore::add_replica(ReplicaID rid, const NetAddr &addr,
                                pubkey_bt &&pub_key) {
    config.add_replica(rid, 
            ReplicaInfo(rid, addr, std::move(pub_key)));
    b0->voted.insert(rid);
}

promise_t HotStuffCore::async_qc_finish(const block_t &blk) {
    if (blk->voted.size() >= config.nmajority)
        return promise_t([](promise_t &pm) {
            pm.resolve();
        });
    auto it = qc_waiting.find(blk);
    if (it == qc_waiting.end())
        it = qc_waiting.insert(std::make_pair(blk, promise_t())).first;
    return it->second;
}

void HotStuffCore::on_qc_finish(const block_t &blk) {
    auto it = qc_waiting.find(blk);
    if (it != qc_waiting.end())
    {
        it->second.resolve();
        qc_waiting.erase(it);
    }
}

promise_t HotStuffCore::async_wait_proposal() {
    return propose_waiting.then([](const Proposal &prop) {
        return prop;
    });
}

promise_t HotStuffCore::async_wait_receive_proposal() {
    return receive_proposal_waiting.then([](const Proposal &prop) {
        return prop;
    });
}

promise_t HotStuffCore::async_hqc_update() {
    return hqc_update_waiting.then([this]() {
        return hqc.first;
    });
}

promise_t HotStuffCore::async_wait_view_change() {
    return view_change_waiting.then([this]() { return view; });
}


promise_t HotStuffCore::async_wait_deliver_proposal(const uint32_t _view) {
    if (last_view_proposal_received >= _view)
        return promise_t([](promise_t &pm) {
            pm.resolve();
        });
    auto it = view_proposal_waiting.find(_view);
    if (it == view_proposal_waiting.end())
        it = view_proposal_waiting.insert(std::make_pair(_view, promise_t())).first;
    return it->second;
}

promise_t HotStuffCore::async_wait_enter_view(const uint32_t _view) {
    if (_view <= view)
        return promise_t([](promise_t &pm) {
            pm.resolve();
        });
    auto it = view_waiting.find(_view);
    if (it == view_waiting.end())
        it = view_waiting.insert(std::make_pair(_view, promise_t())).first;
    return it->second;
}

promise_t HotStuffCore::async_wait_view_qc(const uint32_t _view) {
    if (last_view_cert_received >= view)
        return promise_t([](promise_t &pm) {
            pm.resolve();
        });
    auto it = view_qc_waiting.find(_view);
    if (it == view_qc_waiting.end())
        it = view_qc_waiting.insert(std::make_pair(_view, promise_t())).first;
    return it->second;
}


void HotStuffCore::on_propose_(const Proposal &prop) {
    auto t = std::move(propose_waiting);
    propose_waiting = promise_t();
    t.resolve(prop);
}

void HotStuffCore::on_receive_proposal_(const Proposal &prop) {
    auto t = std::move(receive_proposal_waiting);
    receive_proposal_waiting = promise_t();
    t.resolve(prop);
}

void HotStuffCore::on_receive_view_proposal_(const uint32_t view){
    last_view_proposal_received = view;
    view_proposal_waiting[view].resolve();
}


void HotStuffCore::on_hqc_update() {
    auto t = std::move(hqc_update_waiting);
    hqc_update_waiting = promise_t();
    t.resolve();
}

void HotStuffCore::on_view_change() {
    auto t = std::move(view_change_waiting);
    view_change_waiting = promise_t();
    t.resolve();
}

void HotStuffCore::on_enter_view(const uint32_t _view) {
    view_waiting[_view].resolve();

    Status status(id, hqc.second->clone(), view, this);
    do_status(status);

    auto nreplicas = config.nreplicas;
    auto dest = (id + _view) % nreplicas;
    int mul = (_view - 1) / nreplicas;
    auto for_view =  (2 + mul ) * nreplicas + dest + 1;

    // PVSS sharing
    auto sharing = pvss_context.create_sharing();
    if (dest == id) {
        view_transcripts[for_view].push_back(sharing);
        transcript_ids[for_view].push_back(id);
    }else {
        std::stringstream ss;
        ss.str(std::string{});
        ss << sharing;
        auto str = ss.str();
        bytearray_t transcript(str.begin(), str.end());

        PVSSTranscript ptrans(id, for_view, std::move(transcript));
        do_send_pvss_transcript(ptrans, dest);
    }
    schedule_propose(2*config.delta);
}

void HotStuffCore::on_receive_qc_(const uint32_t _view){
    view_qc_waiting[_view].resolve();
}

HotStuffCore::operator std::string () const {
    DataStream s;
    s << "<hotstuff "
      << "hqc=" << get_hex10(hqc.first->get_hash()) << " "
      << "hqc.height=" << std::to_string(hqc.first->height) << " "
      << "b_exec=" << get_hex10(b_exec->get_hash()) << " "
      << "vheight=" << std::to_string(vheight) << " "
      << "view=" << std::to_string(view) << " "
      << "tails=" << std::to_string(tails.size()) << ">";
    return std::move(s);
}

}
