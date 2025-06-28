#include "pulse.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>

namespace pulse {

// Helper: convert Mode enum to string
std::string mode_to_string(Mode m) {
  switch (m) {
  case Mode::IDLE:
    return "IDLE";
  case Mode::PROPOSAL:
    return "PROPOSAL";
  case Mode::EXECUTION:
    return "EXECUTION";
  }
  return "IDLE";
}

// Helper: convert string to Mode enum
Mode mode_from_string(const std::string &s) {
  if (s == "PROPOSAL")
    return Mode::PROPOSAL;
  if (s == "EXECUTION")
    return Mode::EXECUTION;
  return Mode::IDLE;
}

// Helper: check if two paths share any node
static bool paths_conflict(const Path &a, const Path &b) {
  std::unordered_set<NodeID> nodes(a.begin(), a.end());
  for (const auto &n : b) {
    if (nodes.count(n))
      return true;
  }
  return false;
}

Pulse::Pulse(int robot_id, int hb_interval_ms, int proposal_timeout_ms,
             int local_port, const Endpoint &peer)
    : robot_id_(robot_id), hb_interval_ms_(hb_interval_ms),
      proposal_timeout_ms_(proposal_timeout_ms), local_port_(local_port),
      peer_(peer), mode_(Mode::IDLE), seq_num_(0), token_(0),
      ack_received_(false), rej_received_(false), last_seen_seq_(0),
      stop_flag_(false), pending_response_(false) {
  // create UDP socket
  sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd_ < 0) {
    perror("socket");
    throw std::runtime_error("Failed to create socket");
  }
  // bind to local port
  struct sockaddr_in local_addr;
  std::memset(&local_addr, 0, sizeof(local_addr));
  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = INADDR_ANY;
  local_addr.sin_port = htons(local_port_);
  if (bind(sockfd_, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
    perror("bind");
    close(sockfd_);
    throw std::runtime_error("Failed to bind socket");
  }
  // setup peer address
  std::memset(&peer_addr_, 0, sizeof(peer_addr_));
  peer_addr_.sin_family = AF_INET;
  inet_pton(AF_INET, peer_.ip.c_str(), &peer_addr_.sin_addr);
  peer_addr_.sin_port = htons(peer_.port);
}

Pulse::~Pulse() { stop(); }

void Pulse::run() {
  stop_flag_.store(false);
  // Heartbeat sender thread
  thread_send_ = std::thread([this]() {
    while (!stop_flag_.load()) {
      send_heartbeat_();
      std::this_thread::sleep_for(std::chrono::milliseconds(hb_interval_ms_));
    }
  });
  // Receiver thread
  thread_recv_ = std::thread([this]() {
    char buf[4096];
    while (!stop_flag_.load()) {
      ssize_t len = recv(sockfd_, buf, sizeof(buf) - 1, 0);
      if (len > 0) {
        buf[len] = '\0';
        try {
          auto msg = nlohmann::json::parse(buf);
          handle_heartbeat_(msg);
        } catch (const std::exception &e) {
          std::cerr << "JSON parse error: " << e.what() << std::endl;
        }
      }
    }
  });
  // Block until threads finish
  thread_recv_.join();
  thread_send_.join();
}

void Pulse::stop() {
  if (!stop_flag_.exchange(true)) {
    // unblock recv
    shutdown(sockfd_, SHUT_RD);
    if (thread_recv_.joinable())
      thread_recv_.join();
    if (thread_send_.joinable())
      thread_send_.join();
    close(sockfd_);
  }
}

void Pulse::set_current_node(const NodeID &n) {
  std::lock_guard<std::mutex> lk(state_mtx_);
  current_node_ = n;
}

void Pulse::set_path(const Path &p) {
  std::lock_guard<std::mutex> lk(state_mtx_);
  local_path_ = p;
}

bool Pulse::propose() {
  std::unique_lock<std::mutex> lk(state_mtx_);
  // Build new proposal
  proposal_id_ = std::to_string(robot_id_) + "-" + std::to_string(seq_num_);
  // Random token
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<uint32_t> dist;
  token_ = dist(gen);
  my_prop_.id = proposal_id_;
  my_prop_.token = token_;
  my_prop_.path = local_path_;
  // Enter proposal state
  mode_ = Mode::PROPOSAL;
  ack_received_ = false;
  rej_received_ = false;

  bool completed =
      cv_proposal_.wait_for(lk, std::chrono::milliseconds(proposal_timeout_ms_),
                            [this] { return ack_received_ || rej_received_; });
  if (ack_received_) {
    mode_ = Mode::EXECUTION;
    return true;
  } else {
    mode_ = Mode::IDLE;
    return false;
  }
}

void Pulse::abort_execution() {
  std::lock_guard<std::mutex> lk(state_mtx_);
  mode_ = Mode::IDLE;
  ack_received_ = rej_received_ = false;
  cv_proposal_.notify_all();
}

void Pulse::send_heartbeat_() {
  nlohmann::json msg;
  msg["msg_type"] = "HEARTBEAT";
  msg["robot_id"] = robot_id_;
  msg["seq_num"] = seq_num_++;
  msg["mode"] = mode_to_string(mode_);
  msg["current_node"] = current_node_;
  if (mode_ == Mode::PROPOSAL) {
    msg["proposal"] = {{"id", my_prop_.id},
                       {"token", my_prop_.token},
                       {"path", my_prop_.path}};
  }
  if (mode_ == Mode::EXECUTION) {
    msg["current_path"] = local_path_;
  }
  if (pending_response_) {
    msg["response_to"] = pending_response_to_;
    msg["response"] = (pending_response_is_ack_ ? "ACK" : "REJ");
    pending_response_ = false;
  }
  auto s = msg.dump();
  sendto(sockfd_, s.c_str(), s.size(), 0, (struct sockaddr *)&peer_addr_,
         sizeof(peer_addr_));
}

void Pulse::handle_heartbeat_(const nlohmann::json &msg) {
  std::lock_guard<std::mutex> lk(state_mtx_);
  // Parse peer basic fields
  int peer_id = msg.value("robot_id", -1);
  Mode peer_mode = mode_from_string(msg.value("mode", "IDLE"));
  // Always update peer location if present
  peer_node_ = msg.value("current_node", std::string());
  // Always update peer_path if they are executing
  if (msg.contains("current_path")) {
    peer_path_ = msg.value("current_path", Path());
  }

  // If peer is proposing, update peer_prop from proposal object
  if (peer_mode == Mode::PROPOSAL && msg.contains("proposal")) {
    auto &p = msg["proposal"];
    peer_prop_.id = p.value("id", std::string());
    peer_prop_.token = p.value("token", 0U);
    peer_prop_.path = p.value("path", Path());

    // Decide response based on our current mode
    if (mode_ == Mode::IDLE) {
      pending_response_to_ = peer_prop_.id;
      pending_response_is_ack_ = true;
      pending_response_ = true;
      std::thread(accept_callback_).detach();
    } else if (mode_ == Mode::EXECUTION) {
      bool conflict = paths_conflict(local_path_, peer_prop_.path);
      pending_response_to_ = peer_prop_.id;
      pending_response_is_ack_ = !conflict;
      pending_response_ = true;
    } else if (mode_ == Mode::PROPOSAL) {
      // Both in proposal: break ties via token then robot_id
      if ((token_ > peer_prop_.token) ||
          (token_ == peer_prop_.token && robot_id_ > peer_id)) {
        // we win: transition to EXECUTION, reject peer
        ack_received_ = true;
        pending_response_to_ = peer_prop_.id;
        pending_response_is_ack_ = false;
        pending_response_ = true;
        mode_ = Mode::EXECUTION;
        cv_proposal_.notify_all();
      } else {
        // we lose
        rej_received_ = true;
        cv_proposal_.notify_all();
      }
    }
  }

  // If peer is responding to our proposal
  if (mode_ == Mode::PROPOSAL && msg.contains("response_to") &&
      msg.value("response_to", std::string()) == proposal_id_) {
    if (msg.value("response", std::string()) == "ACK") {
      ack_received_ = true;
    } else {
      rej_received_ = true;
    }
    cv_proposal_.notify_all();
  }
}

void Pulse::set_accept_callback(AcceptCallback cb) {
  this->accept_callback_ = cb;
}

NodeID Pulse::get_peer_node() const { return this->peer_node_; }

Path Pulse::get_peer_path() const { return this->peer_path_; }

} // namespace pulse
