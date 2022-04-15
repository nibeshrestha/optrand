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

#ifndef _HOTSTUFF_CONSENSUS_H
#define _HOTSTUFF_CONSENSUS_H

#include <cassert>
#include <set>
#include <unordered_map>

#include "hotstuff/promise.hpp"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/crypto.h"
#include "erasure.h"
#include "merklecpp.h"

#include "crypto2/pvss/Aggregation.hpp"
#include "crypto2/pvss/Beacon.hpp"
#include "crypto2/pvss/Decryption.hpp"
#include "crypto2/pvss/Factory.hpp"
#include "crypto2/pvss/pvss.hpp"
#include "crypto2/pvss/Utils.hpp"

#include "crypto2/pvss/Serialization.hpp"

namespace hotstuff {

struct Proposal;
struct Vote;
struct Status;
struct Blame;
struct BlameNotify;
struct Finality;
struct QC;
struct Ack;
struct Share;
struct Echo;
struct Beacon;
struct PVSSTranscript;

/** Abstraction for HotStuff protocol state machine (without network implementation). */
class HotStuffCore {
    block_t b0;                                  /** the genesis block */
    /* === state variables === */
    /** block containing the QC for the highest block having one */
    std::pair<block_t, quorum_cert_bt> hqc;   /**< highest QC */
    block_t b_exec;                            /**< last executed block */
    uint32_t vheight;          /**< height of the block last voted for */
    uint32_t nheight;          /**< height of the block last notified for */
    uint32_t view;             /**< the current view number */
    /* Q: does the proposer retry the same block in a new view? */
    /* === only valid for the current view === */
    bool progress; /**< whether heard a proposal in the current view: this->view */
    bool view_trans; /**< whether the replica is in-between the views */
    std::unordered_map<uint32_t, std::unordered_set<block_t>> proposals;
    std::unordered_map<block_t, bool> finished_propose;
    quorum_cert_bt blame_qc;
    std::unordered_set<ReplicaID> blamed;

    /* === auxilliary variables === */
    privkey_bt priv_key;            /**< private key for signing votes */
    std::set<block_t, BlockHeightCmp> tails;   /**< set of tail blocks */
    ReplicaConfig config;                   /**< replica configuration */
    /* === async event queues === */
    std::unordered_map<block_t, promise_t> qc_waiting;
    promise_t propose_waiting;
    promise_t receive_proposal_waiting;
    promise_t hqc_update_waiting;
    promise_t view_change_waiting;
    std::unordered_map<uint32_t, promise_t> view_waiting;
    std::unordered_map<uint32_t, promise_t> view_qc_waiting;
    std::unordered_map<uint32_t, promise_t> view_proposal_waiting;

    /* == feature switches == */
    /** always vote negatively, useful for some PaceMakers */
    bool vote_disabled;

    block_t get_delivered_blk(const uint256_t &blk_hash);
    void sanity_check_delivered(const block_t &blk);
    void check_commit(const block_t &_hqc);
    void update_hqc(const block_t &_hqc, const quorum_cert_bt &qc);
    void on_hqc_update();
    void on_qc_finish(const block_t &blk);
    void on_propose_(const Proposal &prop);
    void on_receive_proposal_(const Proposal &prop);
    void on_view_change();
    void on_receive_qc_(uint32_t _view);
    void on_receive_view_proposal_(uint32_t _view);

    void _vote(const block_t &blk, ReplicaID dest);
    void _ack(const block_t &blk);
    void _deliver_proposal(const Proposal &prop);
    void _deliver_cert(const quorum_cert_bt &qc);
    void _broadcast_share(uint32_t view);
    void _try_enter_view();

    void _update_agg_queue(uint32_t view);

    uint32_t last_propose_delivered_view;
    uint32_t last_propose_decoded_view;
    uint32_t last_cert_decoded_view;
    uint32_t last_view_proposal_received;
    uint32_t last_view_cert_received;
    uint32_t last_view_shares_received;
    uint32_t last_cert_delivered_view;
    uint32_t last_proposed_view;
    uint32_t last_view_beacon_received;
    /* Erasure Coded Proposal Chunks by view */
    std::unordered_map<uint32_t, std::unordered_map<ReplicaID, chunk_t>> prop_chunks;
    std::unordered_map<uint32_t, std::unordered_map<ReplicaID, chunk_t>> qc_chunks;

    std::unordered_map<uint32_t, std::unordered_set<ReplicaID>> status_received;

    // PVSS transcripts
    optrand_crypto::Context pvss_context;

    std::unordered_map<uint32_t, std::vector<optrand_crypto::pvss_sharing_t>> view_transcripts;
    std::unordered_map<uint32_t, std::vector<size_t>> transcript_ids;

    std::unordered_map<uint32_t, std::vector<optrand_crypto::decryption_t>> view_shares;

    // A queue of aggregated transcripts per party
    std::unordered_map<ReplicaID, optrand_crypto::pvss_aggregate_t> agg_queue;
    std::unordered_map<uint32_t, optrand_crypto::pvss_aggregate_t> view_agg_transcripts;


protected:
    ReplicaID id;                  /**< identity of the replica itself */

    public:
    BoxObj<EntityStorage> storage;

    HotStuffCore(ReplicaID id, privkey_bt &&priv_key, const optrand_crypto::Context &pvss_ctx, const std::string setup_dat_file);
    virtual ~HotStuffCore() {
        b0->qc_ref = nullptr;
    }

    /* Inputs of the state machine triggered by external events, should called
     * by the class user, with proper invariants. */

    /** Call to initialize the protocol, should be called once before all other
     * functions. */
    void on_init(uint32_t nfaulty, double delta);

    /* TODO: better name for "delivery" ? */
    /** Call to inform the state machine that a block is ready to be handled.
     * A block is only delivered if itself is fetched, the block for the
     * contained qc is fetched and all parents are delivered. The user should
     * always ensure this invariant. The invalid blocks will be dropped by this
     * function.
     * @return true if valid */
    bool on_deliver_blk(const block_t &blk);

    /** Call upon the delivery of a proposal message.
     * The block mentioned in the message should be already delivered. */
    void on_receive_proposal(const Proposal &prop);

    /** Call upon the delivery of a vote message.
     * The block mentioned in the message should be already delivered. */
    void on_receive_vote(const Vote &vote);
    void on_receive_status(const Status &status);
    void on_commit_timeout(const block_t &blk);
    void on_propose_timeout();
    void on_viewtrans_timeout();
    void on_enter_view(uint32_t view);

    void on_receive_qc(const quorum_cert_bt &qc);
    void on_receive_ack(const Ack &ack);
    void on_receive_share(const Share &share);
    void on_receive_proposal_echo(const Echo &echo);
    void on_receive_cert_echo(const Echo &echo);
    void on_receive_beacon(const Beacon &beacon);
    void on_receive_pvss_transcript(const PVSSTranscript &pvss_transcript);


    /** Call to submit new commands to be decided (executed). "Parents" must
     * contain at least one block, and the first block is the actual parent,
     * while the others are uncles/aunts */
    block_t on_propose(const std::vector<uint256_t> &cmds,
                    const std::vector<block_t> &parents,
                    bytearray_t &&extra = bytearray_t());


    /* Functions required to construct concrete instances for abstract classes.
     * */

    /* Outputs of the state machine triggering external events.  The virtual
     * functions should be implemented by the user to specify the behavior upon
     * the events. */
    protected:
    /** Called by HotStuffCore upon the decision being made for cmd. */
    virtual void do_decide(Finality &&fin) = 0;
    virtual void do_consensus(const block_t &blk) = 0;
    /** Called by HotStuffCore upon broadcasting a new proposal.
     * The user should send the proposal message to all replicas except for
     * itself. */
    virtual void do_broadcast_proposal(const Proposal &prop) = 0;
    virtual void do_broadcast_vote(const Vote &vote) = 0;
    virtual void set_commit_timer(const block_t &blk, double t_sec) = 0;
    virtual void set_propose_timer(double t_sec) = 0;
    virtual void stop_commit_timer(uint32_t height) = 0;
    virtual void stop_commit_timer_all() = 0;
    virtual void stop_propose_timer() = 0;
    virtual void set_viewtrans_timer(double t_sec) = 0;
    virtual void stop_viewtrans_timer() = 0;

    virtual void do_broadcast_qc(const QC &qc) = 0;

    virtual void enter_view(uint32_t _view) = 0;
    virtual void do_vote(const Vote &vote, ReplicaID dest) = 0;
    virtual void do_broadcast_ack(const Ack &ack) = 0;
    virtual void do_broadcast_share(const Share &share) = 0;
    virtual void do_broadcast_beacon(const Beacon &beacon) = 0;
    virtual void do_status(const Status &status) = 0;
    virtual void do_broadcast_echo(const Echo &echo) = 0;
    virtual void do_echo(const Echo &echo, ReplicaID dest) = 0;
    virtual void do_broadcast_echo2(const Echo &echo) = 0;
    virtual void do_echo2(const Echo &echo, ReplicaID dest) = 0;
    virtual void do_propose() = 0;
    virtual void do_send_pvss_transcript(const PVSSTranscript &pvss_transcript, ReplicaID dest) = 0;


    virtual void schedule_propose(double t_sec) = 0;
    virtual void block_fetched(const block_t &blk, ReplicaID replicaId) = 0;

    /* The user plugs in the detailed instances for those
     * polymorphic data types. */
    public:
    /** Create a partial certificate that proves the vote for a block. */
    virtual part_cert_bt create_part_cert(const PrivKey &priv_key, const uint256_t &blk_hash, const uint32_t &view) = 0;
    /** Create a partial certificate from its seralized form. */
    virtual part_cert_bt parse_part_cert(DataStream &s) = 0;
    /** Create a quorum certificate that proves 2f+1 votes for a block. */
    virtual quorum_cert_bt create_quorum_cert(const uint256_t &blk_hash, const uint32_t &view) = 0;
    /** Create a quorum certificate from its serialized form. */
    virtual quorum_cert_bt parse_quorum_cert(DataStream &s) = 0;
    /** Create a command object from its serialized form. */
    //virtual command_t parse_cmd(DataStream &s) = 0;

    public:
    /** Add a replica to the current configuration. This should only be called
     * before running HotStuffCore protocol. */
    void add_replica(ReplicaID rid, const NetAddr &addr, pubkey_bt &&pub_key);
    /** Try to prune blocks lower than last committed height - staleness. */
    void prune(uint32_t staleness);

    /* PaceMaker can use these functions to monitor the core protocol state
     * transition */
    /** Get a promise resolved when the block gets a QC. */
    promise_t async_qc_finish(const block_t &blk);
    /** Get a promise resolved when a new block is proposed. */
    promise_t async_wait_proposal();
    /** Get a promise resolved when a new proposal is received. */
    promise_t async_wait_receive_proposal();
    /** Get a promise resolved when hqc is updated. */
    promise_t async_hqc_update();
    /** Get a promise resolved after a view change. */
    promise_t async_wait_view_change();
    /** Get a promise resolved before a view change. */


    promise_t async_wait_deliver_proposal(const uint32_t _view);
    promise_t async_wait_enter_view(const uint32_t _view);
    promise_t async_wait_view_qc(const uint32_t _view);

    /* Other useful functions */
    const block_t &get_genesis() { return b0; }
    const block_t &get_hqc() { return hqc.first; }
    const quorum_cert_bt &get_hqc_qc() {return hqc.second; }
    const ReplicaConfig &get_config() { return config; }
    ReplicaID get_id() const { return id; }
    const std::set<block_t, BlockHeightCmp> get_tails() const { return tails; }
    uint32_t get_view() const { return view; }
    operator std::string () const;
    void set_vote_disabled(bool f) { vote_disabled = f; }

    uint32_t get_last_proposed_view() {return last_proposed_view;}

private:

    ReplicaID get_proposer(uint32_t _view){
        // Nibesh: Duplicate of get_proposer function in pacemaker.
        return (_view -1) % config.nreplicas;
    }
};


enum ProofType {
    VOTE = 0x00,
    BLAME = 0x01
};

/** Abstraction for proposal messages. */
struct Proposal: public Serializable {
    ReplicaID proposer;
    /** block being proposed */
    block_t blk;

    uint32_t view;
    /** handle of the core object to allow polymorphism. The user should use
     * a pointer to the object of the class derived from HotStuffCore */
    HotStuffCore *hsc;

    Proposal(): blk(nullptr), hsc(nullptr) {}
    Proposal(ReplicaID proposer,
            const block_t &blk,
            const uint32_t &view,
            HotStuffCore *hsc):
        proposer(proposer),
        blk(blk), view(view), hsc(hsc) {}

    Proposal(const Proposal &other):
        proposer(other.proposer),
        blk(other.blk), view(other.view),
        hsc(other.hsc) {}

    void serialize(DataStream &s) const override {
        s << proposer
          << *blk << view;
    }

    inline void unserialize(DataStream &s) override;

    operator std::string () const {
        DataStream s;
        s << "<proposal "
          << "rid=" << std::to_string(proposer) << " "
          << "blk=" << get_hex10(blk->get_hash()) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }

};

/** Abstraction for vote messages. */
struct Vote: public Serializable {
    ReplicaID voter;
    /** block being voted */
    uint256_t blk_hash;
    /** proof of validity for the vote */
    part_cert_bt cert;

    uint32_t view;
    
    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    Vote(): cert(nullptr), hsc(nullptr) {}
    Vote(ReplicaID voter,
        const uint256_t &blk_hash,
        part_cert_bt &&cert,
        const uint32_t &view,
        HotStuffCore *hsc):
        voter(voter),
        blk_hash(blk_hash),
        cert(std::move(cert)),
        view(view), hsc(hsc) {}

    Vote(const Vote &other):
        voter(other.voter),
        blk_hash(other.blk_hash),
        cert(other.cert ? other.cert->clone() : nullptr),
        view(other.view),
        hsc(other.hsc) {}

    Vote(Vote &&other) = default;
    
    void serialize(DataStream &s) const override {
        s << voter << blk_hash << view << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> voter >> blk_hash >> view;
        cert = hsc->parse_part_cert(s);
    }

    static uint256_t proof_obj_hash(const uint256_t &blk_hash) {
//        DataStream p;
//        p << blk_hash;
        return blk_hash;
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter)) &&
                cert->get_obj_hash() == proof_obj_hash(blk_hash);
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == proof_obj_hash(blk_hash);
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<vote "
          << "rid=" << std::to_string(voter) << " "
          << "blk=" << get_hex10(blk_hash) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};

struct Status: public Serializable {
    ReplicaID replicaID;
    uint32_t view;
    quorum_cert_bt qc;

    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    Status(): qc(nullptr), hsc(nullptr) {}
    Status(ReplicaID replicaID, quorum_cert_bt &&qc,
           uint32_t view, HotStuffCore *hsc):
            replicaID(replicaID), qc(std::move(qc)), view(view), hsc(hsc) {}

    Status(const Status &other):
            replicaID(replicaID),
            qc(other.qc ? other.qc->clone() : nullptr),
            view(other.view),hsc(other.hsc) {}

    Status(Status &&other) = default;
    
    void serialize(DataStream &s) const override {
        s << view << replicaID;
        s << *qc;
    }

    void unserialize(DataStream &s) override {
        s >> view;
        s >> replicaID;

        qc = hsc->parse_quorum_cert(s);
    }

    bool verify() const {
        assert(hsc != nullptr);
        return qc->verify(hsc->get_config());
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);

        // Nibesh: quick hack to not check qc in view 1 as there is no qc in the first status message
        if(view == 1)
            return promise_t([](promise_t &pm){ pm.resolve(true); });

        return qc->verify(hsc->get_config(), vpool).then([this](bool result) {
            return result;
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<status "
          << "rid=" << std::to_string(replicaID) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};

struct PVSSTranscript: public Serializable {
    ReplicaID replicaID;
    uint32_t for_view;
    bytearray_t pvss_transcript;

    PVSSTranscript() {}
    PVSSTranscript(ReplicaID replicaID, uint32_t for_view, bytearray_t &&pvss_transcript):
    replicaID(replicaID), for_view(for_view), pvss_transcript(std::move(pvss_transcript)) {}

    PVSSTranscript(const PVSSTranscript &other):
            replicaID(other.replicaID),
            for_view(other.for_view), pvss_transcript(std::move(other.pvss_transcript)) {}

    PVSSTranscript(PVSSTranscript &&other) = default;

    void serialize(DataStream &s) const override {
        s << for_view << replicaID;
        s << htole((uint32_t)pvss_transcript.size()) << pvss_transcript;
    }

    void unserialize(DataStream &s) override {
        uint32_t n;
        s >> for_view;
        s >> replicaID;

        s >> n;
        n = letoh(n);
        auto base = s.get_data_inplace(n);
        pvss_transcript = bytearray_t(base, base + n);
    }

    operator std::string () const {
        DataStream s;
        s << "<pvss-transcript "
          << "rid=" << std::to_string(replicaID) << " "
          << "for_view=" << std::to_string(for_view) << ">";
        return std::move(s);
    }

};

struct Blame: public Serializable {
    ReplicaID blamer;
    uint32_t view;
    part_cert_bt cert;
    
    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    Blame(): cert(nullptr), hsc(nullptr) {}
    Blame(ReplicaID blamer,
        uint32_t view,
        part_cert_bt &&cert,
        HotStuffCore *hsc):
        blamer(blamer),
        view(view),
        cert(std::move(cert)), hsc(hsc) {}

    Blame(const Blame &other):
        blamer(other.blamer),
        view(other.view),
        cert(other.cert ? other.cert->clone() : nullptr),
        hsc(other.hsc) {}

    Blame(Blame &&other) = default;
    
    void serialize(DataStream &s) const override {
        s << blamer << view << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> blamer >> view;
        cert = hsc->parse_part_cert(s);
    }

    static uint256_t proof_obj_hash(uint32_t view) {
        DataStream p;
        p << (uint8_t)ProofType::BLAME << view;
        return p.get_hash();
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(blamer)) &&
                cert->get_obj_hash() == proof_obj_hash(view);
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(blamer), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == proof_obj_hash(view);
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<blame "
          << "rid=" << std::to_string(blamer) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};

struct BlameNotify: public Serializable {
    uint32_t view;
    uint256_t hqc_hash;
    quorum_cert_bt hqc_qc;
    quorum_cert_bt qc;
    
    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    BlameNotify(): hqc_qc(nullptr), qc(nullptr), hsc(nullptr) {}
    BlameNotify(uint32_t view,
                const uint256_t &hqc_hash,
                quorum_cert_bt &&hqc_qc,
                quorum_cert_bt &&qc,
                HotStuffCore *hsc):
        view(view),
        hqc_hash(hqc_hash),
        hqc_qc(std::move(hqc_qc)),
        qc(std::move(qc)), hsc(hsc) {}

    BlameNotify(const BlameNotify &other):
        view(other.view),
        hqc_hash(other.hqc_hash),
        hqc_qc(other.hqc_qc ? other.hqc_qc->clone() : nullptr),
        qc(other.qc ? other.qc->clone() : nullptr), hsc(other.hsc) {}

    BlameNotify(BlameNotify &&other) = default;
    
    void serialize(DataStream &s) const override {
        s << view << hqc_hash << *hqc_qc << *qc;
    }

    void unserialize(DataStream &s) override {
        s >> view >> hqc_hash;
        hqc_qc = hsc->parse_quorum_cert(s);
        qc = hsc->parse_quorum_cert(s);
    }

    bool verify() const {
        assert(hsc != nullptr);
        return qc->verify(hsc->get_config()) &&
            qc->get_obj_hash() == Blame::proof_obj_hash(view) &&
            hqc_qc->get_obj_hash() == Vote::proof_obj_hash(hqc_hash);
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        if (qc->get_obj_hash() != Blame::proof_obj_hash(view) ||
            hqc_qc->get_obj_hash() != Vote::proof_obj_hash(hqc_hash))
            return promise_t([](promise_t &){ return false; });
        return promise::all(std::vector<promise_t>{
            qc->verify(hsc->get_config(), vpool),
            hqc_qc->verify(hsc->get_config(), vpool),
        }).then([](const promise::values_t &values) {
            return promise::any_cast<bool>(values[0]) &&
                promise::any_cast<bool>(values[1]);
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<blame notify "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};

inline void Proposal::unserialize(DataStream &s) {
    assert(hsc != nullptr);
    s >> proposer;
    Block _blk;
    _blk.unserialize(s, hsc);
    blk = hsc->storage->add_blk(std::move(_blk), hsc->get_config());
    s >> view;
}

struct QC: public Serializable {
    quorum_cert_bt qc;
    /** handle of the core object to allow polymorphism. */
    HotStuffCore *hsc;

    QC(): qc(nullptr), hsc(nullptr) {}
    QC(quorum_cert_bt &&qc, HotStuffCore *hsc): hsc(hsc), qc(std::move(qc)) {}

    void serialize(DataStream &s) const override {
        s << *qc;
    }

    void unserialize(DataStream &s) override {
        qc = hsc->parse_quorum_cert(s);
    }

    bool verify() const {
        assert(hsc != nullptr);
        return qc->verify(hsc->get_config());
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);

        // skip verification of qc from old views.
        if (qc->get_view() < hsc->get_view())
            return promise_t([](promise_t &pm) { pm.resolve(true); });

        return qc->verify(hsc->get_config(), vpool).then([this](bool result) {
            return result;
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<qc>";
        return s;
    }
};

struct Finality: public Serializable {
    ReplicaID rid;
    int8_t decision;
    uint32_t cmd_idx;
    uint32_t cmd_height;
    uint256_t cmd_hash;
    uint256_t blk_hash;
    
    public:
    Finality() = default;
    Finality(ReplicaID rid,
            int8_t decision,
            uint32_t cmd_idx,
            uint32_t cmd_height,
            uint256_t cmd_hash,
            uint256_t blk_hash):
        rid(rid), decision(decision),
        cmd_idx(cmd_idx), cmd_height(cmd_height),
        cmd_hash(cmd_hash), blk_hash(blk_hash) {}

    void serialize(DataStream &s) const override {
        s << rid << decision
          << cmd_idx << cmd_height
          << cmd_hash;
        if (decision == 1) s << blk_hash;
    }

    void unserialize(DataStream &s) override {
        s >> rid >> decision
          >> cmd_idx >> cmd_height
          >> cmd_hash;
        if (decision == 1) s >> blk_hash;
    }

    operator std::string () const {
        DataStream s;
        s << "<fin "
          << "decision=" << std::to_string(decision) << " "
          << "cmd_idx=" << std::to_string(cmd_idx) << " "
          << "cmd_height=" << std::to_string(cmd_height) << " "
          << "cmd=" << get_hex10(cmd_hash) << " "
          << "blk=" << get_hex10(blk_hash) << ">";
        return std::move(s);
    }
};

/** Abstraction for ack messages. */
struct Ack: public Serializable {
    ReplicaID voter;
    /** block being acked */
    uint256_t blk_hash;
    /** proof of validity for the ack */
    part_cert_bt cert;

    uint32_t view;

    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

    Ack(): cert(nullptr), hsc(nullptr) {}
    Ack(ReplicaID voter,
         const uint256_t &blk_hash,
         part_cert_bt &&cert,
         const uint32_t &view,
         HotStuffCore *hsc):
            voter(voter),
            blk_hash(blk_hash),
            cert(std::move(cert)),
            view(view), hsc(hsc) {}

    Ack(const Ack &other):
            voter(other.voter),
            blk_hash(other.blk_hash),
            cert(other.cert ? other.cert->clone() : nullptr),
            view(other.view),
            hsc(other.hsc) {}

    Ack(Ack &&other) = default;

    void serialize(DataStream &s) const override {
        s << voter << blk_hash << view << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        s >> voter >> blk_hash >> view;
        cert = hsc->parse_part_cert(s);
    }

    static uint256_t proof_obj_hash(const uint256_t &blk_hash) {
//        DataStream p;
//        p << blk_hash;
        return blk_hash;
    }

    bool verify() const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter)) &&
               cert->get_obj_hash() == proof_obj_hash(blk_hash);
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(voter), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == proof_obj_hash(blk_hash);
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<ack "
          << "rid=" << std::to_string(voter) << " "
          << "blk=" << get_hex10(blk_hash) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};

//    std::string str(share.bt.begin(), share.bt.end());
//    std::stringstream ss;
//    ss.str(str);
//
//    optrand_crypto::decryption_t dec_share;
//
//    ss >> dec_share;
//
//    ReplicaID proposer = get_proposer(share.view);
//
//    if(!pvss_context.verify_decryption(agg_queue[proposer], dec_share)){
//    throw std::runtime_error("Decryption Verification failed in View");
//}


/** Abstraction for share messages to send secret shares. */
struct Share: public Serializable {
    ReplicaID replicaId;
    /** proof of validity for the share */
//    part_cert_bt cert;

    uint32_t view;

    // Decryption;
    bytearray_t bt;

    /** handle of the core object to allow polymorphism */
    HotStuffCore *hsc;

//    Share(): cert(nullptr), hsc(nullptr) {}
    Share():  hsc(nullptr) {}
    Share(ReplicaID replicaId,
//        part_cert_bt &&cert,
        const uint32_t &view,
        bytearray_t &&bt,
        HotStuffCore *hsc):
            replicaId(replicaId),
//            cert(std::move(cert)),
            view(view), bt(std::move(bt)), hsc(hsc) {}

    Share(const Share &other):
            replicaId(other.replicaId),
//            cert(other.cert ? other.cert->clone() : nullptr),
            view(other.view), bt(std::move(other.bt)),
            hsc(other.hsc) {}

    Share(Share &&other) = default;

    void serialize(DataStream &s) const override {
        s << replicaId << view;
        s << htole((uint32_t)bt.size()) << bt;
//        s << *cert;
    }

    void unserialize(DataStream &s) override {
        assert(hsc != nullptr);
        uint32_t n;
        s >> replicaId >> view;

        s >> n;
        n = letoh(n);
        auto base = s.get_data_inplace(n);
        bt = bytearray_t(base, base + n);

//        cert = hsc->parse_part_cert(s);
    }

    static uint256_t proof_obj_hash(const uint256_t &blk_hash) {
//        DataStream p;
//        p << blk_hash;
        return blk_hash;
    }

    bool verify() const {
        assert(hsc != nullptr);
        return true;
//        return cert->verify(hsc->get_config().get_pubkey(replicaId));
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return promise_t([](promise_t &pm){ pm.resolve(true); });
//        return cert->verify(hsc->get_config().get_pubkey(replicaId), vpool).then([this](bool result) {
//            return result;
//        });
    }

    operator std::string () const {
        DataStream s;
        s << "<share "
          << "rid=" << std::to_string(replicaId) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};


struct Echo: public Serializable {
    ReplicaID replicaID;
    uint32_t idx;
    uint32_t view;
    uint32_t mtype;
    uint256_t merkle_root;
    bytearray_t merkle_proof;

    part_cert_bt cert;
    /** chunk being proposed */
    chunk_t chunk;

    HotStuffCore *hsc;

    Echo(): chunk(nullptr), hsc(nullptr) {}
    Echo(ReplicaID replicaID,
         uint32_t idx,
         uint32_t view,
         uint32_t mtype,
         uint256_t merkle_root,
        bytearray_t merkle_proof,
         const chunk_t &chunk,
         part_cert_bt &&cert,
         HotStuffCore *hsc):
            replicaID(replicaID),
            idx(idx), view(view),
            mtype(mtype),
            merkle_root(merkle_root),
            merkle_proof(merkle_proof),
            chunk(chunk),
            cert(std::move(cert)),
            hsc(hsc){}

    Echo(const Echo &other):
            replicaID(other.replicaID),
            idx(other.idx),
            view(other.view),
            mtype(other.mtype),
            merkle_root(other.merkle_root),
            merkle_proof(other.merkle_proof),
            chunk(other.chunk),
            cert(other.cert ? other.cert->clone() : nullptr),
            hsc(other.hsc){}

    void serialize(DataStream &s) const override {
        s << replicaID << idx << view << mtype << merkle_root;
        s << htole((uint32_t)merkle_proof.size()) << merkle_proof;
        s << *chunk << *cert;
    }

    inline void unserialize(DataStream &s) override {
        s >> replicaID;
        s >> idx;
        s >> view;
        s >> mtype;
        s >> merkle_root;

        uint32_t n;
        s >> n;
        n = letoh(n);
        if (n == 0){
            merkle_proof.clear();
        }else{
            auto base = s.get_data_inplace(n);
            merkle_proof = bytearray_t(base, base+n);
        }
        Chunk _chunk;
        s >> _chunk;
        chunk = new Chunk(std::move(_chunk));
        cert = hsc->parse_part_cert(s);
    }


    bool verify() const {
        merkle::Hash root(merkle_root);
        merkle::Path path(merkle_proof);

        return path.verify(root);
    }

    promise_t verify(VeriPool &vpool) const {
        assert(hsc != nullptr);
        return cert->verify(hsc->get_config().get_pubkey(replicaID), vpool).then([this](bool result) {
            return result && cert->get_obj_hash() == merkle_root; //  &&verify();
        });
    }

    operator std::string () const {
        DataStream s;
        s << "<echo "
          << "rid=" << std::to_string(replicaID) << " "
          << "idx=" << std::to_string(idx) << " "
          << "view=" << std::to_string(view) << " "
          << "mtype=" << std::to_string(mtype) << " "
          << "hash=" << get_hex10(merkle_root) << ">";
        return std::move(s);
    }
};


/** Abstraction for Beacon messages to send beacon */
struct Beacon: public Serializable {
    ReplicaID replicaId;
    uint32_t view;
    // beacon;
    bytearray_t bt;

    HotStuffCore *hsc;

    Beacon()  {}
    Beacon(ReplicaID replicaId,
          const uint32_t &view,
          bytearray_t &&bt,
          HotStuffCore *hsc):
            replicaId(replicaId),
            view(view),
            bt(std::move(bt)),
            hsc(hsc){}

    Beacon(const Beacon &other,
            HotStuffCore *hsc):
            replicaId(other.replicaId),
            view(other.view), bt(std::move(other.bt)),
            hsc(other.hsc){}

    Beacon(Beacon &&other) = default;

    void serialize(DataStream &s) const override {
        s << replicaId << view;
        s << htole((uint32_t)bt.size()) << bt;
    }

    void unserialize(DataStream &s) override {
        uint32_t n;
        s >> replicaId >> view;

        s >> n;
        n = letoh(n);
        auto base = s.get_data_inplace(n);
        bt = bytearray_t(base, base + n);
    }

    static uint256_t proof_obj_hash(const uint256_t &blk_hash) {
//        DataStream p;
//        p << blk_hash;
        return blk_hash;
    }

    bool verify() const {
//        assert(hsc != nullptr);
//        return cert->verify(hsc->get_config().get_pubkey(replicaId));
        return true;
    }

    promise_t verify(VeriPool &vpool) const {
//        assert(hsc != nullptr);
        return promise_t([](promise_t &pm) { pm.resolve(true); });
//        return cert->verify(hsc->get_config().get_pubkey(replicaId), vpool).then([this](bool result) {
//            return result;
//        });
    }

    operator std::string () const {
        DataStream s;
        s << "<beacon "
          << "rid=" << std::to_string(replicaId) << " "
          << "view=" << std::to_string(view) << ">";
        return std::move(s);
    }
};



}

#endif
