/**
 * Copyright (c) 2013, Willem-Hendrik Thiart
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * @file
 * @author Willem Thiart himself@willemthiart.com
 */

#pragma once
#include <vector>
#include <chrono>
#include <functional>
#include <bmcl/Option.h>

namespace raft
{

enum class Error : uint8_t
{
    Shutdown,
    NotLeader,
    OneVotingChangeOnly,
    NodeUnknown,
    NothingToApply,
};

enum class ReqVoteState
{
    Granted         = 1,
    NotGranted     = 0,
    UnknownNode    = -1,
};

enum class State
{
    Follower,
    Candidate,
    Leader
} ;

enum class EntryState
{
    Invalidated = -1,
    NotCommitted = 0,
    Committed = 1,
};

enum class LotType
{
    Normal,
    AddNonVotingNode,
    AddNode,
    DemoteNode,
    RemoveNode,
};

enum class NodeStatus
{
    Disconnected,
    Connected,
    Connecting,
    Disconnecting,
};

enum class NodeId : std::size_t {};

struct LogEntryData
{
    LogEntryData() : buf(nullptr), len(0) {}
    LogEntryData(void* buf, std::size_t len) : buf(buf), len(len) {}
    void* buf;
    std::size_t len;
};

/** Entry that is stored in the server's entry log. */
struct LogEntry
{
    LogEntry(std::size_t term, std::size_t id, LogEntryData data = LogEntryData{}) : term(term), id(id), type(LotType::Normal), data(data) {}
    LogEntry(std::size_t term, std::size_t id, LotType type, NodeId node, LogEntryData data = LogEntryData{})
        : term(term), id(id), type(type), node(node), data(data) {}
    std::size_t term;           /**< the entry's term at the point it was created */
    std::size_t id;             /**< the entry's unique ID */
    LotType type;             /**< type of entry */
    bmcl::Option<NodeId> node; /**< node id if this id cfg change entry */
    LogEntryData data;

    inline bool is_voting_cfg_change() const
    {
        return LotType::AddNode == type || LotType::DemoteNode == type;
    }

    inline bool is_cfg_change() const
    {
        return (
            LotType::AddNode == type ||
            LotType::AddNonVotingNode == type ||
            LotType::DemoteNode == type ||
            LotType::RemoveNode == type);
    }

};

/** Message sent from client to server.
 * The client sends this message to a server with the intention of having it
 * applied to the FSM. */
using MsgAddEntryReq = LogEntry;

/** Entry message response.
 * Indicates to client if entry was committed or not. */
struct MsgAddEntryRep
{
    MsgAddEntryRep(std::size_t term, std::size_t id, std::size_t idx) : term(term), id(id), idx(idx){}
    std::size_t term;   /**< the entry's term */
    std::size_t id;     /**< the entry's unique ID */
    std::size_t idx;    /**< the entry's index */
};

/** Vote request message.
 * Sent to nodes when a server wants to become leader.
 * This message could force a leader/candidate to become a follower. */
struct MsgVoteReq
{
    MsgVoteReq(std::size_t term, std::size_t last_log_idx, std::size_t last_log_term)
        :term(term), last_log_idx(last_log_idx), last_log_term(last_log_term)
    {
    }
    std::size_t term;               /**< currentTerm, to force other leader/candidate to step down */
    std::size_t last_log_idx;       /**< index of candidate's last log entry */
    std::size_t last_log_term;      /**< term of candidate's last log entry */
};

/** Vote request response message.
 * Indicates if node has accepted the server's vote request. */
struct MsgVoteRep
{
    MsgVoteRep(std::size_t term, ReqVoteState vote) : term(term), vote_granted(vote) {}
    std::size_t term;                   /**< currentTerm, for candidate to update itself */
    ReqVoteState vote_granted;     /**< true means candidate received vote */
};

/** Appendentries message.
 * This message is used to tell nodes if it's safe to apply entries to the FSM.
 * Can be sent without any entries as a keep alive message.
 * This message could force a leader/candidate to become a follower. */
struct MsgAppendEntriesReq
{
    MsgAppendEntriesReq(std::size_t term) : term(term), prev_log_idx(0), prev_log_term(0), leader_commit(0), n_entries(0), entries(nullptr) {}
    MsgAppendEntriesReq(std::size_t term, std::size_t prev_log_idx, std::size_t prev_log_term, std::size_t leader_commit)
        : term(term), prev_log_idx(prev_log_idx), prev_log_term(prev_log_term), leader_commit(leader_commit), n_entries(0), entries(nullptr) {}
    std::size_t term;           /**< currentTerm, to force other leader/candidate to step down */
    std::size_t prev_log_idx;   /**< the index of the log just before the newest entry for the node who receives this message */
    std::size_t prev_log_term;  /**< the term of the log just before the newest entry for the node who receives this message */
    std::size_t leader_commit;  /**< the index of the entry that has been appended to the majority of the cluster. Entries up to this index will be applied to the FSM */
    std::size_t n_entries;      /**< number of entries within this message */
    const MsgAddEntryReq* entries; /**< array of entries within this message */
};

/** Appendentries response message.
 * Can be sent without any entries as a keep alive message.
 * This message could force a leader/candidate to become a follower. */
struct MsgAppendEntriesRep
{
    MsgAppendEntriesRep(std::size_t term, bool success, std::size_t current_idx, std::size_t first_idx)
        : term(term), success(success), current_idx(current_idx), first_idx(first_idx) {}
    std::size_t term;           /**< currentTerm, to force other leader/candidate to step down */
    bool success;               /**< true if follower contained entry matching prevLogidx and prevLogTerm */

    /* Non-Raft fields follow: */
    /* Having the following fields allows us to do less book keeping in regards to full fledged RPC */

    std::size_t current_idx;    /**< This is the highest log IDX we've received and appended to our log */
    std::size_t first_idx;      /**< The first idx that we received within the appendentries message */
} ;

class ISender
{
public:
    /** Callback for sending request vote messages to all cluster's members */
    virtual bmcl::Option<Error> request_vote(const MsgVoteReq& msg) = 0;

    /** Callback for sending appendentries messages */
    virtual bmcl::Option<Error> append_entries(const NodeId& node, const MsgAppendEntriesReq& msg) = 0;
};

class ISaver
{
public:
    /** Callback for finite state machine application
    * Return 0 on success.
    * Return RAFT_ERR_SHUTDOWN if you want the server to shutdown. */
    virtual bmcl::Option<Error> apply_log(const LogEntry& entry, std::size_t entry_idx) = 0;

    /** Callback for persisting vote data
    * For safety reasons this callback MUST flush the change to disk. */
    virtual bmcl::Option<Error> persist_vote(NodeId node) = 0;

    /** Callback for persisting term data
    * For safety reasons this callback MUST flush the change to disk. */
    virtual bmcl::Option<Error> persist_term(std::size_t node) = 0;

    /** Callback for adding an entry to the log
    * For safety reasons this callback MUST flush the change to disk.
    * Return 0 on success.
    * Return RAFT_ERR_SHUTDOWN if you want the server to shutdown. */
    virtual bmcl::Option<Error> push_back(const LogEntry& entry, std::size_t entry_idx) = 0;

    /** Callback for removing the oldest entry from the log
    * For safety reasons this callback MUST flush the change to disk.
    * @note If memory was malloc'd in log_offer then this should be the right
    *  time to free the memory. */
    virtual void pop_front(const LogEntry& entry, std::size_t entry_idx) = 0;

    /** Callback for removing the youngest entry from the log
    * For safety reasons this callback MUST flush the change to disk.
    * @note If memory was malloc'd in log_offer then this should be the right
    *  time to free the memory. */
    virtual void pop_back(const LogEntry& entry, std::size_t entry_idx) = 0;

    /** Callback for catching debugging log messages
    * This callback is optional */
    virtual void log(const NodeId node, const char *buf) = 0;
};


}