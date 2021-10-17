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
                            privkey_bt &&priv_key):
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
        storage(new EntityStorage()) {
    storage->add_blk(b0);
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
    if (_hqc->height > hqc.first->height)
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
        for (size_t i = 0; i < blk->cmds.size(); i++)
            do_decide(Finality(id, 1, i, blk->height,
                                blk->cmds[i], blk->get_hash()));
    }
    b_exec = blk;
}

// 2. Vote
void HotStuffCore::_vote(const block_t &blk) {
    const auto &blk_hash = blk->get_hash();
    LOG_PROTO("vote for %s", get_hex10(blk_hash).c_str());
    Vote vote(id, blk_hash,
            create_part_cert(
                *priv_key, blk_hash, view), view, this);
#ifndef SYNCHS_NOVOTEBROADCAST
    on_receive_vote(vote);
#endif
    do_broadcast_vote(vote);
//    set_commit_timer(blk, 2 * config.delta);
    //set_blame_timer(3 * config.delta);
}

// 3. Blame
void HotStuffCore::_blame() {
    stop_blame_timer();
    Blame blame(id, view,
            create_part_cert(
                *priv_key,
                Blame::proof_obj_hash(view), view), this);
    on_receive_blame(blame);
    do_broadcast_blame(blame);
}

// i. New-view
void HotStuffCore::_new_view() {
    LOG_INFO("preparing new-view");
    blame_qc->compute();
    BlameNotify bn(view,
        hqc.first->get_hash(),
        hqc.second->clone(),
        blame_qc->clone(), this);
    view_trans = true;
    on_view_trans();
    on_receive_blamenotify(bn);
    do_broadcast_blamenotify(bn);
    stop_commit_timer_all();
    set_viewtrans_timer(2 * config.delta);
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
    /* create the new block */
    block_t bnew = storage->add_blk(
        new Block(parents, cmds,
            hqc.second->clone(), std::move(extra),
            parents[0]->height + 1,
            hqc.first,
            nullptr
        ));
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
    _vote(bnew);
    on_propose_(prop);
    /* boradcast to other replicas */
    do_broadcast_proposal(prop);
    return bnew;
}

void HotStuffCore::on_receive_proposal(const Proposal &prop) {
    if (view_trans) return;
    LOG_PROTO("got %s", std::string(prop).c_str());
    block_t bnew = prop.blk;
    if (finished_propose[bnew]) return;
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
    finished_propose[bnew] = true;
    on_receive_proposal_(prop);
    // check if the proposal extends the highest certified block
    if (opinion && !vote_disabled) _vote(bnew);

    update_proposed_cmds(prop.blk);
    early_propose(view, prop.blk);
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
        on_receive_proposal(Proposal(vote.voter, blk, view, nullptr));
    }
    size_t qsize = blk->voted.size();
    if (qsize >= config.nmajority) return;
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
    if (qsize + 1 == config.nmajority)
    {
        set_commit_timer(blk, 2 * config.delta);
        qc->compute();
        update_hqc(blk, qc);
        view += 1;
        on_qc_finish(blk);
        enter_view(view);
        on_enter_view(view);
        do_broadcast_qc(QC(qc->clone(), this));
    }
}

void HotStuffCore::on_receive_notify(const Notify &notify) {
    block_t blk = get_delivered_blk(notify.blk_hash);
    update_hqc(blk, notify.qc);
}

void HotStuffCore::on_receive_blame(const Blame &blame) {
    if (view_trans) return; // already in view transition
    size_t qsize = blamed.size();
    if (qsize >= config.nmajority) return;
    if (!blamed.insert(blame.blamer).second)
    {
        LOG_WARN("duplicate blame from %d", blame.blamer);
        return;
    }
    assert(blame_qc);
    blame_qc->add_part(blame.blamer, *blame.cert);
    if (++qsize == config.nmajority)
        _new_view();
}

void HotStuffCore::on_receive_blamenotify(const BlameNotify &bn) {
    if (view_trans) return;
    blame_qc = bn.qc->clone();
    _new_view();
}

void HotStuffCore::on_receive_qc(const quorum_cert_bt &qc){
    uint32_t _view = qc->get_view();
    if (_view < view) return;
    LOG_PROTO("got QC view = %d", _view);

    block_t blk = get_delivered_blk(qc->get_obj_hash());
    size_t qsize = blk->voted.size();
    if (qsize >= config.nmajority) return;

    auto &_qc = blk->self_qc;
    _qc = qc->clone();
    set_commit_timer(blk, 2*config.delta);
    on_qc_finish(blk);

    view = _view + 1;
    do_broadcast_qc(QC(qc->clone(), this));
    enter_view(view);
    on_enter_view(view);
}

void HotStuffCore::on_commit_timeout(const block_t &blk) { check_commit(blk); }

void HotStuffCore::on_blame_timeout() {
    LOG_INFO("no progress, start blaming");
    _blame();
}

void HotStuffCore::on_viewtrans_timeout() {
    // view change
    view++;
    view_trans = false;
    proposals.clear();
    blame_qc = create_quorum_cert(Blame::proof_obj_hash(view), view);
    blamed.clear();
    set_blame_timer(3 * config.delta);
    on_view_change(); // notify the PaceMaker of the view change
    LOG_INFO("entering view %d", view);
    // send the highest certified block
    Notify notify(hqc.first->get_hash(), hqc.second->clone(), this);
    do_notify(notify);
}

/*** end HotStuff protocol logic ***/
void HotStuffCore::on_init(uint32_t nfaulty, double delta) {
    config.nmajority = config.nreplicas - nfaulty;
    config.delta = delta;
    blame_qc = create_quorum_cert(Blame::proof_obj_hash(view), view);
    b0->qc = create_quorum_cert(b0->get_hash(), view);
    b0->qc->compute();
    b0->self_qc = b0->qc->clone();
    b0->qc_ref = b0;
    hqc = std::make_pair(b0, b0->qc->clone());
    view = 1;
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

promise_t HotStuffCore::async_wait_view_trans() {
    return view_trans_waiting;
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

void HotStuffCore::on_view_trans() {
    auto t = std::move(view_trans_waiting);
    view_trans_waiting = promise_t();
    t.resolve();
}

void HotStuffCore::on_enter_view(const uint32_t _view) {
    view_waiting[_view].resolve();
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
