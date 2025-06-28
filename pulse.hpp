#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

namespace pulse {

using NodeID = std::string;
using Path = std::vector<NodeID>;
using AcceptCallback = std::function<void(const Path &path)>;

enum class Mode { IDLE, PROPOSAL, EXECUTION };

/**
 * @struct Endpoint
 * @brief Represents a peer's network address (IP and UDP port).
 */
struct Endpoint {
  std::string ip; ///< IPv4 or IPv6 address
  uint16_t port;  ///< UDP port number
};

/**
 * @class Pulse
 * @brief Heartbeat-based, peer-to-peer reservation coordinator for two robots.
 *
 * Each robot cycles through three modes:
 * - IDLE:      always accept incoming proposals, vacate conflicting nodes if
 * necessary
 * - PROPOSAL:  negotiating to reserve a path; waits for ACK/REJ
 * - EXECUTION: reserved path is locked; highest priority until abort or finish
 */
class Pulse {
public:
  /**
   * @brief Construct a Pulse coordinator.
   * @param robot_id            Unique integer ID for this robot.
   * @param hb_interval_ms      Heartbeat interval in milliseconds.
   * @param proposal_timeout_ms Timeout for each proposal round in
   * milliseconds.
   * @param local_port          UDP port to bind for sending/receiving
   * heartbeats.
   * @param peer                Endpoint of the peer robot.
   */
  Pulse(int robot_id, int hb_interval_ms, int proposal_timeout_ms,
        int local_port, const Endpoint &peer);

  /**
   * @brief Destructor; stops networking threads if running.
   */
  ~Pulse();

  /**
   * @brief Start networking threads (sender & receiver).
   *        Blocks until stop() is called or on fatal error.
   */
  void run();

  /**
   * @brief Stop all threads and cleanly shut down the coordinator.
   */
  void stop();

  /**
   * @brief Update your current physical node (sent every heartbeat).
   * @param n The node ID you are currently at.
   */
  void set_current_node(const NodeID &n);

  /**
   * @brief Set the intended path to reserve once granted.
   * @param p Sequence of NodeIDs to lock & traverse.
   */
  void set_path(const Path &p);

  /**
   * @brief Propose the path set via set_path().
   * @return true if proposal succeeded (entered EXECUTION), false on
   * rejection/timeout.
   */
  bool propose();

  /**
   * @brief Abort any in-flight proposal or active execution, return to IDLE.
   */
  void abort_execution();

  /**
   * @brief Callback to call on accepted proposal in IDLE mode
   *
   * @param cb Callback to call
   */
  void set_accept_callback(AcceptCallback cb);

  /**
   * @brief Get the peer node
   *
   * @return Reserved node of the peer
   */
  NodeID get_peer_node() const;

  /**
   * @brief Get the peer path
   *
   * @return Reserved path of the peer
   */
  Path get_peer_path() const;

protected:
  // —heartbeat send/receive—
  void send_heartbeat_();
  void handle_heartbeat_(const nlohmann::json &msg);

private:
  int robot_id_;
  int hb_interval_ms_;
  int proposal_timeout_ms_;
  int local_port_;
  Endpoint peer_;

  Mode mode_;

  // Networking
  int sockfd_;                   ///< UDP socket file descriptor
  struct sockaddr_in peer_addr_; ///< Peer address struct
  AcceptCallback accept_callback_;

  // Local & peer state
  NodeID current_node_; ///< Last known own node
  Path local_path_;     ///< Intended or executing path
  NodeID peer_node_;    ///< Last known peer node
  Path peer_path_;      ///< Peer’s executing path

  // Proposal metadata
  std::string proposal_id_; ///< Unique ID for current proposal
  uint32_t token_;          ///< Randomized priority token
  struct Proposal {         ///< Structured proposal info
    std::string id;
    uint32_t token;
    Path path;
  } my_prop_, peer_prop_;

  // Proposal responses
  bool ack_received_; ///< True if our proposal got an ACK
  bool rej_received_; ///< True if our proposal got a REJ

  // Heartbeat sequencing
  uint64_t seq_num_;       ///< Outbound heartbeat sequence counter
  uint64_t last_seen_seq_; ///< Last inbound sequence from peer

  // Piggy-back reply flags
  bool pending_response_; ///< Whether to include response in next heartbeat
  std::string pending_response_to_; ///< Proposal ID we are responding to
  bool pending_response_is_ack_;    ///< ACK (true) vs REJ (false)

  // Threading & synchronization
  std::mutex state_mtx_;                ///< Guards shared state
  std::condition_variable cv_proposal_; ///< Signals proposal outcome
  std::thread thread_send_;             ///< Heartbeat sender thread
  std::thread thread_recv_;             ///< Heartbeat receiver thread
  std::atomic<bool> stop_flag_;         ///< Signals threads to exit
};

} // namespace pulse
