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

#ifndef _HOTSTUFF_CORE_H
#define _HOTSTUFF_CORE_H

#include <queue>
#include <unordered_map>
#include <unordered_set>


#include "salticidae/util.h"
#include "salticidae/network.h"
#include "salticidae/msg.h"
#include "hotstuff/util.h"
#include "hotstuff/consensus.h"

namespace hotstuff {

using salticidae::PeerNetwork;
using salticidae::ElapsedTime;
using salticidae::_1;
using salticidae::_2;

const double ent_waiting_timeout = 10;
const double double_inf = 1e10;

/** Network message format for HotStuff. */
struct MsgPropose {
    static const opcode_t opcode = 0x0;
    DataStream serialized;
    Proposal proposal;
    MsgPropose(const Proposal &);
    /** Only move the data to serialized, do not parse immediately. */
    MsgPropose(DataStream &&s): serialized(std::move(s)) {}
    /** Parse the serialized data to blks now, with `hsc->storage`. */
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgVote {
    static const opcode_t opcode = 0x1;
    DataStream serialized;
    Vote vote;
    MsgVote(const Vote &);
    MsgVote(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgStatus {
    static const opcode_t opcode = 0x4;
    DataStream serialized;
    Status status;
    MsgStatus(const Status &);
    MsgStatus(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgReqBlock {
    static const opcode_t opcode = 0x2;
    DataStream serialized;
    std::vector<uint256_t> blk_hashes;
    MsgReqBlock() = default;
    MsgReqBlock(const std::vector<uint256_t> &blk_hashes);
    MsgReqBlock(DataStream &&s);
};


struct MsgRespBlock {
    static const opcode_t opcode = 0x3;
    DataStream serialized;
    std::vector<block_t> blks;
    MsgRespBlock(const std::vector<block_t> &blks);
    MsgRespBlock(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgQC {
    static const opcode_t opcode = 0x7;
    DataStream serialized;
    QC qc;
    MsgQC(const QC &);
    MsgQC(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgAck {
    static const opcode_t opcode = 0x8;
    DataStream serialized;
    Ack ack;
    MsgAck(const Ack &);
    MsgAck(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgShare {
    static const opcode_t opcode = 0x9;
    DataStream serialized;
    Share share;
    MsgShare(const Share &);
    MsgShare(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgBeacon {
    static const opcode_t opcode = 0x10;
    DataStream serialized;
    Beacon beacon;
    MsgBeacon(const Beacon &);
    MsgBeacon(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};


struct MsgEcho {
    static const opcode_t opcode = 0x5;
    DataStream serialized;
    Echo echo;
    MsgEcho(const Echo &);
    MsgEcho(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgEcho2 {
    static const opcode_t opcode = 0x6;
    DataStream serialized;
    Echo2 echo2;
    MsgEcho2(const Echo2 &);
    MsgEcho2(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgPVSSTranscript {
    static const opcode_t opcode = 0x11;
    DataStream serialized;
    PVSSTranscript ptrans;
    MsgPVSSTranscript(const PVSSTranscript &);
    MsgPVSSTranscript(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgNewStateReq {
    static const opcode_t opcode = 0x12;
    DataStream serialized;
    NewStateReq newStateReq;
    MsgNewStateReq(const NewStateReq &);
    MsgNewStateReq(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgNewStateResp {
    static const opcode_t opcode = 0x13;
    DataStream serialized;
    NewStateResp newStateResp;
    MsgNewStateResp(const NewStateResp &);
    MsgNewStateResp(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

struct MsgJoin {
    static const opcode_t opcode = 0x14;
    DataStream serialized;
    Join join;
    MsgJoin(const Join &);
    MsgJoin(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};


struct MsgJoinSuccess {
    static const opcode_t opcode = 0x15;
    DataStream serialized;
    JoinSuccess joinSuccess;
    MsgJoinSuccess(const JoinSuccess &);
    MsgJoinSuccess(DataStream &&s): serialized(std::move(s)) {}
    void postponed_parse(HotStuffCore *hsc);
};

using promise::promise_t;

class HotStuffBase;
using pacemaker_bt = BoxObj<class PaceMaker>;

template<EntityType ent_type>
class FetchContext: public promise_t {
    TimerEvent timeout;
    HotStuffBase *hs;
    MsgReqBlock fetch_msg;
    const uint256_t ent_hash;
    std::unordered_set<NetAddr> replica_ids;
    inline void timeout_cb(TimerEvent &);
    public:
    FetchContext(const FetchContext &) = delete;
    FetchContext &operator=(const FetchContext &) = delete;
    FetchContext(FetchContext &&other);

    FetchContext(const uint256_t &ent_hash, HotStuffBase *hs);
    ~FetchContext() {}

    inline void send(const NetAddr &replica_id);
    inline void reset_timeout();
    inline void add_replica(const NetAddr &replica_id, bool fetch_now = true);
};

class BlockDeliveryContext: public promise_t {
    public:
    ElapsedTime elapsed;
    BlockDeliveryContext &operator=(const BlockDeliveryContext &) = delete;
    BlockDeliveryContext(const BlockDeliveryContext &other):
        promise_t(static_cast<const promise_t &>(other)),
        elapsed(other.elapsed) {}
    BlockDeliveryContext(BlockDeliveryContext &&other):
        promise_t(static_cast<const promise_t &>(other)),
        elapsed(std::move(other.elapsed)) {}
    template<typename Func>
    BlockDeliveryContext(Func callback): promise_t(callback) {
        elapsed.start();
    }
};


/** HotStuff protocol (with network implementation). */
class HotStuffBase: public HotStuffCore {
    using BlockFetchContext = FetchContext<ENT_TYPE_BLK>;
    using CmdFetchContext = FetchContext<ENT_TYPE_CMD>;

    friend BlockFetchContext;
    friend CmdFetchContext;

    public:
    using Net = PeerNetwork<opcode_t>;
    using commit_cb_t = std::function<void(const Finality &)>;

    protected:
    /** the binding address in replica network */
    NetAddr listen_addr;
    /** the block size */
    size_t blk_size;
    /** libevent handle */
    EventContext ec;
    salticidae::ThreadCall tcall;
    VeriPool vpool;
    std::vector<NetAddr> peers;
    std::vector<NetAddr> active_peers;
    std::unordered_map<uint32_t, TimerEvent> commit_timers;
    TimerEvent propose_timer;
    TimerEvent viewtrans_timer;
    TimerEvent view_timer;

    uint32_t nactive_replicas;
    bool is_disabled;

    private:
    /** whether libevent handle is owned by itself */
    bool ec_loop;
    /** network stack */
    Net pn;
    std::unordered_set<uint256_t> valid_tls_certs;
#ifdef HOTSTUFF_BLK_PROFILE
    BlockProfiler blk_profiler;
#endif
    pacemaker_bt pmaker;
    /* queues for async tasks */
    std::unordered_map<const uint256_t, BlockFetchContext> blk_fetch_waiting;
    std::unordered_map<const uint256_t, BlockDeliveryContext> blk_delivery_waiting;
    std::unordered_map<const uint256_t, commit_cb_t> decision_waiting;
    using cmd_queue_t = salticidae::MPSCQueueEventDriven<std::pair<uint256_t, commit_cb_t>>;
    cmd_queue_t cmd_pending;
    std::queue<uint256_t> cmd_pending_buffer;

    /* statistics */
    uint64_t fetched;
    uint64_t delivered;
    mutable uint64_t nsent;
    mutable uint64_t nrecv;

    mutable uint32_t part_parent_size;
    mutable uint32_t part_fetched;
    mutable uint32_t part_delivered;
    mutable uint32_t part_decided;
    mutable uint32_t part_gened;
    mutable double part_delivery_time;
    mutable double part_delivery_time_min;
    mutable double part_delivery_time_max;
    mutable std::unordered_map<const NetAddr, uint32_t> part_fetched_replica;

#ifdef SYNCHS_LATBREAKDOWN
    struct CmdLatStat {
        double proposed;
        double committed;
        ElapsedTime et;
        void on_init() { et.start(); }
        void on_propose() {
            et.stop();
            proposed = et.elapsed_sec;
            et.start();
        }
        void on_commit() {
            et.stop();
            committed = et.elapsed_sec;
        }
    };
    std::unordered_map<const uint256_t, CmdLatStat> cmd_lats;
    mutable double part_lat_proposed;
    mutable double part_lat_committed;
#endif

    void on_fetch_cmd(const command_t &cmd);
    void on_fetch_blk(const block_t &blk);
    void on_deliver_blk(const block_t &blk);

    /** deliver consensus message: <propose> */
    inline void propose_handler(MsgPropose &&, const Net::conn_t &);
    /** deliver consensus message: <vote> */
    inline void vote_handler(MsgVote &&, const Net::conn_t &);
    inline void status_handler(MsgStatus &&, const Net::conn_t &);
    inline void pvss_transcript_handler(MsgPVSSTranscript &&, const Net::conn_t &);
    inline void new_state_req_handler(MsgNewStateReq &&, const Net::conn_t &);
    inline void new_state_resp_handler(MsgNewStateResp &&, const Net::conn_t &);
    inline void join_handler(MsgJoin &&, const Net::conn_t &);
    inline void join_success_handler(MsgJoinSuccess &&msg, const Net::conn_t &conn);

    /** fetches full block data */
    inline void req_blk_handler(MsgReqBlock &&, const Net::conn_t &);
    /** receives a block */
    inline void resp_blk_handler(MsgRespBlock &&, const Net::conn_t &);

    inline void qc_handler(MsgQC &&, const Net::conn_t &);
    inline void ack_handler(MsgAck &&, const Net::conn_t &);
    inline void share_handler(MsgShare &&, const Net::conn_t &);
    inline void beacon_handler(MsgBeacon &&, const Net::conn_t &);
    inline void echo_handler(MsgEcho &&, const Net::conn_t &);
    inline void echo2_handler(MsgEcho2 &&, const Net::conn_t &);

    inline bool conn_handler(const salticidae::ConnPool::conn_t &, bool);
    template<typename T, typename M>
    void _do_broadcast(const T &t) {
        //M m(t);
        pn.multicast_msg(M(t), active_peers);
        //for (const auto &replica: peers)
        //    pn.send_msg(m, replica);
    }

    void do_broadcast_proposal(const Proposal &prop) override {
        _do_broadcast<Proposal, MsgPropose>(prop);
    }

    void do_broadcast_vote(const Vote &vote) override {
#ifdef SYNCHS_NOVOTEBROADCAST
        pmaker->beat_resp(0)
                .then([this, vote](ReplicaID proposer) {
            if (proposer == get_id())
            {
                throw HotStuffError("unreachable line");
                //on_receive_vote(vote);
            }
            else
                pn.send_msg(MsgVote(vote), get_config().get_addr(proposer));
        });
#else
        _do_broadcast<Vote, MsgVote>(vote);
#endif

    }


    void do_status(const Status &status) override;

    void set_commit_timer(const block_t &blk, double t_sec) override;
    void stop_commit_timer(uint32_t height) override;
    void stop_commit_timer_all() override;
    void set_propose_timer(double t_sec) override;
    void stop_propose_timer() override;
    void set_viewtrans_timer(double t_sec) override;
    void stop_viewtrans_timer() override;
    void set_view_timer(double t_sec) override;
    void stop_view_timer() override;

    void do_decide(Finality &&) override;
    void do_consensus(const block_t &blk) override;

    void block_fetched(const block_t &blk, ReplicaID replicaId) override;

    void do_broadcast_qc(const QC &qc) override {
        _do_broadcast<QC, MsgQC>(qc);
    }

    void enter_view(uint32_t _view) override;

    void do_vote(const Vote &vote, ReplicaID dest) override {
        pn.send_msg(MsgVote(vote), get_config().get_addr(dest));
     }

    void do_broadcast_ack(const Ack &ack) override {
        _do_broadcast<Ack, MsgAck>(ack);
    }

    void do_broadcast_share(const Share &share) override {
        _do_broadcast<Share, MsgShare>(share);
    }

    void do_broadcast_echo(const Echo &echo) override {
        _do_broadcast<Echo, MsgEcho>(echo);
    }

    void do_broadcast_beacon(const Beacon &beacon) override {
        _do_broadcast<Beacon, MsgBeacon>(beacon);
    }

    void do_echo(const Echo &echo, ReplicaID dest) override {
        pn.send_msg(MsgEcho(echo), get_config().get_addr(dest));
    }

    void do_broadcast_echo2(const Echo2 &echo2) override {
        _do_broadcast<Echo2, MsgEcho2>(echo2);
    }

    void do_echo2(const Echo2 &echo2, ReplicaID dest) override {
        pn.send_msg(MsgEcho2(echo2), get_config().get_addr(dest));
    }

    void do_send_pvss_transcript(const PVSSTranscript &pvss_transcript, ReplicaID dest) override {
        pn.send_msg(MsgPVSSTranscript(pvss_transcript), get_config().get_addr(dest));
    }

    void do_broadcast_new_state_req(const NewStateReq &newStateReq) override {
        _do_broadcast<NewStateReq, MsgNewStateReq>(newStateReq);
    }

    void do_send_new_state(const NewStateResp &newStateResp, ReplicaID dest) override{
        pn.send_msg(MsgNewStateResp(newStateResp), get_config().get_addr(dest));
    }

    void do_broadcast_join(const Join &join) override{
        _do_broadcast<Join, MsgJoin>(join);
     }

     void do_send_join_success(const JoinSuccess &joinSuccess, ReplicaID dest) override {
        pn.send_msg(MsgJoinSuccess(joinSuccess), get_config().get_addr(dest));
     }

    void do_propose() override;

    void schedule_propose(double t_sec) override;

    void delete_peer(const NetAddr &addr) override {
        active_peers.erase(std::remove(active_peers.begin(), active_peers.end(), addr), active_peers.end());
    }

    void refresh_active_peers(const std::vector<NetAddr> &_peers) override;
    void add_active_peer(const NetAddr &addr) override;

    protected:

    /** Called to replicate the execution of a command, the application should
     * implement this to make transition for the application state. */
    virtual void state_machine_execute(const Finality &) = 0;

    public:
    HotStuffBase(uint32_t blk_size,
            ReplicaID rid,
            uint32_t nactive_replicas,
            privkey_bt &&priv_key,
            NetAddr listen_addr,
            pacemaker_bt pmaker,
            const optrand_crypto::Context &pvss_ctx,
            const string setup_dat_file,
            EventContext ec,
            size_t nworker,
            const Net::Config &netconfig);

    ~HotStuffBase();

    /* the API for HotStuffBase */

    /* Submit the command to be decided. */
    void exec_command(uint256_t cmd_hash, commit_cb_t callback);
    void start(std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&replicas,
                double delta, bool ec_loop = false);

    void do_start_join();
    void do_stop_replica();

    size_t size() const { return peers.size(); }
    const auto &get_decision_waiting() const { return decision_waiting; }
    const auto &get_blk_size() const { return blk_size; }
    const auto &get_cmd_pending_size() const { return cmd_pending_buffer.size(); }
    ThreadCall &get_tcall() { return tcall; }
    PaceMaker *get_pace_maker() { return pmaker.get(); }
    void print_stat() const;
    virtual void do_elected() {}

#ifdef SYNCHS_AUTOCLI
    virtual void do_demand_commands(size_t) {}
#endif

    /* Helper functions */
    /** Returns a promise resolved (with command_t cmd) when Command is fetched. */
    promise_t async_fetch_cmd(const uint256_t &cmd_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is fetched. */
    promise_t async_fetch_blk(const uint256_t &blk_hash, const NetAddr *replica_id, bool fetch_now = true);
    /** Returns a promise resolved (with block_t blk) when Block is delivered (i.e. prefix is fetched). */
    promise_t async_deliver_blk(const uint256_t &blk_hash,  const NetAddr &replica_id);

    promise_t pm_qc_manual;
};

/** HotStuff protocol (templated by cryptographic implementation). */
template<typename PrivKeyType = PrivKeyDummy,
        typename PubKeyType = PubKeyDummy,
        typename PartCertType = PartCertDummy,
        typename QuorumCertType = QuorumCertDummy>
class HotStuff: public HotStuffBase {
    using HotStuffBase::HotStuffBase;
    protected:

    part_cert_bt create_part_cert(const PrivKey &priv_key, const uint256_t &blk_hash, const uint32_t &view) override {
        HOTSTUFF_LOG_DEBUG("create part cert with priv=%s, blk_hash=%s",
                            get_hex10(priv_key).c_str(), get_hex10(blk_hash).c_str());
        return new PartCertType(
                    static_cast<const PrivKeyType &>(priv_key),
                    blk_hash, view);
    }

    part_cert_bt parse_part_cert(DataStream &s) override {
        PartCert *pc = new PartCertType();
        s >> *pc;
        return pc;
    }

    quorum_cert_bt create_quorum_cert(const uint256_t &blk_hash, const uint32_t & view) override {
        return new QuorumCertType(get_config(), blk_hash, view);
    }

    quorum_cert_bt parse_quorum_cert(DataStream &s) override {
        QuorumCert *qc = new QuorumCertType();
        s >> *qc;
        return qc;
    }

    public:
    HotStuff(uint32_t blk_size,
            ReplicaID rid,
            uint32_t nactive_replicas,
            const bytearray_t &raw_privkey,
            NetAddr listen_addr,
            pacemaker_bt pmaker,
            const optrand_crypto::Context &pvss_ctx,
            const string setup_dat_file,
            EventContext ec = EventContext(),
            size_t nworker = 4,
            const Net::Config &netconfig = Net::Config()):
        HotStuffBase(blk_size,
                    rid,
                    nactive_replicas,
                    new PrivKeyType(raw_privkey),
                    listen_addr,
                    std::move(pmaker),
                    pvss_ctx,
                    setup_dat_file,
                    ec,
                    nworker,
                    netconfig) {

    }

    void start(const std::vector<std::tuple<NetAddr, bytearray_t, bytearray_t>> &replicas,
                double delta, bool ec_loop = false) {
        std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> reps;
        for (auto &r: replicas)
            reps.push_back(
                std::make_tuple(
                    std::get<0>(r),
                    new PubKeyType(std::get<1>(r)),
                    uint256_t(std::get<2>(r))
                ));
        HotStuffBase::start(std::move(reps), delta, ec_loop);
    }
};

using HotStuffNoSig = HotStuff<>;
using HotStuffSecp256k1 = HotStuff<PrivKeySecp256k1, PubKeySecp256k1,
                                    PartCertSecp256k1, QuorumCertSecp256k1>;

template<EntityType ent_type>
FetchContext<ent_type>::FetchContext(FetchContext && other):
        promise_t(static_cast<const promise_t &>(other)),
        hs(other.hs),
        fetch_msg(std::move(other.fetch_msg)),
        ent_hash(other.ent_hash),
        replica_ids(std::move(other.replica_ids)) {
    other.timeout.del();
    timeout = TimerEvent(hs->ec,
            std::bind(&FetchContext::timeout_cb, this, _1));
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_CMD>::timeout_cb(TimerEvent &) {
    HOTSTUFF_LOG_WARN("cmd fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<>
inline void FetchContext<ENT_TYPE_BLK>::timeout_cb(TimerEvent &) {
    HOTSTUFF_LOG_WARN("block fetching %.10s timeout", get_hex(ent_hash).c_str());
    for (const auto &replica_id: replica_ids)
        send(replica_id);
    reset_timeout();
}

template<EntityType ent_type>
FetchContext<ent_type>::FetchContext(
                                const uint256_t &ent_hash, HotStuffBase *hs):
            promise_t([](promise_t){}),
            hs(hs), ent_hash(ent_hash) {
    fetch_msg = std::vector<uint256_t>{ent_hash};

    timeout = TimerEvent(hs->ec,
            std::bind(&FetchContext::timeout_cb, this, _1));
    reset_timeout();
}

template<EntityType ent_type>
void FetchContext<ent_type>::send(const NetAddr &replica_id) {
    hs->part_fetched_replica[replica_id]++;
    hs->pn.send_msg(fetch_msg, replica_id);
}

template<EntityType ent_type>
void FetchContext<ent_type>::reset_timeout() {
    timeout.add(salticidae::gen_rand_timeout(ent_waiting_timeout));
}

template<EntityType ent_type>
void FetchContext<ent_type>::add_replica(const NetAddr &replica_id, bool fetch_now) {
    if (replica_ids.empty() && fetch_now)
        send(replica_id);
    replica_ids.insert(replica_id);
}

}

#endif
