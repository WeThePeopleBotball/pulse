<table align="center">
  <tr>
    <td><img src=".github/assets/logo.png" alt="Pulse Logo" width="80"></td>
    <td><h1 style="margin: 0;">Pulse: Peer-to-Peer Path Reservation Protocol</h1></td>
  </tr>
</table>

**Pulse** is a lightweight, peer-to-peer reservation protocol that lets **two robots** safely reserve and traverse shared resources (nodes in a graph or map) without collisions or central coordination.

It’s designed for **real-time**, **decentralized** operation over **unreliable UDP**. Robots exchange periodic **heartbeat messages** containing their full coordination state, including:

- Current **mode**: `IDLE`, `PROPOSAL`, or `EXECUTION`
- **Current node**: physical position of the robot
- **Pending proposal**: unique `proposal_id`, randomized `token`, and requested `path`
- **Executing path**: the robot’s current reserved path (if any)
- **Piggybacked responses**: ACK or REJ to peer’s last proposal

> “Just keep repeating the truth, and eventually the other side will hear you.”

This principle underpins Pulse's robustness: it does not rely on perfect packet delivery. Instead, it handles loss by **spamming truth** (heartbeats with full state), and **responding to incoming proposals** with embedded decisions.

---

## 🧠 How Pulse Works

Each robot runs an instance of `Pulse`, which manages its internal state machine and communicates with its peer over UDP. Coordination happens through three modes:

### 1. `IDLE`
- The default mode.
- **Always accepts** incoming proposals from the peer (sends `ACK`).
- Should **vacate any conflicting nodes** if necessary — this is handled by registering an `accept_callback(Path)`.

### 2. `PROPOSAL`
- Robot initiates a reservation for a path.
- Sends heartbeats containing:
  - A **unique `proposal_id`** (e.g. `robotID-seqNum`)
  - A **random `token`** for tie-breaking
  - The desired `path`
- Waits for a response (`ACK` or `REJ`) for up to `proposal_timeout_ms`.
- If the peer is also proposing:
  - If paths **conflict**, the robot with the **higher token** wins. If tied, higher `robot_id` wins.
  - If no conflict, both proposals succeed.
- A successful proposal transitions to **`EXECUTION`**, otherwise returns to **`IDLE`**.

### 3. `EXECUTION`
- Robot has **locked the path** and begins traversing it.
- Heartbeats now include the full `current_path`.
- Incoming peer proposals are:
  - ACK’d if there’s no path conflict.
  - REJ’d if paths overlap.
- When done (or aborted), robot transitions back to **`IDLE`**.

---

## 🔁 Resilience Through Redundancy

Pulse runs on **unreliable UDP**, but handles message loss **gracefully** without explicit retries.

### How?
- Every **heartbeat is a complete snapshot** of the robot’s current state.
- **Proposal responses** (`ACK` / `REJ`) are **piggybacked** on multiple outgoing heartbeats.
- **Incoming messages** are processed with strict **sequence numbers**, so only the most recent state is used.
- If a message is dropped, the peer will hear the **same truth again soon**.

This makes the protocol **eventually consistent**, robust under typical WiFi/UDP loss, and **safe by default**: no robot proceeds unless it receives a valid, recent response.

---

## ✅ Safety Guarantees

Pulse provides:

| Property             | Guarantee | Mechanism |
|----------------------|-----------|-----------|
| **Mutual exclusion** | ✅        | Conflict detection + token tie-breaking |
| **Safe fallbacks**   | ✅        | Proposal timeout → IDLE |
| **Fair arbitration** | ✅        | Random tokens and IDs resolve ties |
| **Resilience to drops** | ✅    | Redundant state broadcasts |
| **Deadlock prevention** | ✅    | Always accepts while IDLE |

---

## 🚀 Getting Started

1. **Include** the core files in your project:

```cpp
#include "pulse.hpp"
````

2. **Create an instance**:

```cpp
pulse::Endpoint peer{"<peer-ip>", <peer-port>};
pulse::Pulse coordinator(my_robot_id, heartbeat_ms, proposal_timeout_ms, my_port, peer);
```

3. **Start the system**:

```cpp
coordinator.run(); // blocks until stop() is called
```

4. **Use the API**:

```cpp
coordinator.set_current_node("A2");
coordinator.set_path({"A3", "A4", "A5"});
bool ok = coordinator.propose(); // returns true if accepted
```

5. **React to accepted proposals**:

```cpp
coordinator.set_accept_callback([](const pulse::Path &p) {
    std::cout << "Peer accepted path: ";
    for (const auto &n : p) std::cout << n << " ";
    std::cout << std::endl;
    // Optional: move out of the way, cancel your path, etc.
});
```

6. **Stop when done**:

```cpp
coordinator.abort_execution();
coordinator.stop();
```

---

## 🔧 Use Case

Pulse is ideal for systems where two autonomous agents:

* Share a discrete environment (e.g. nodes in a warehouse, intersections in a grid)
* Need **conflict-free reservation** of paths
* Want lightweight, decentralized coordination without relying on TCP or a central server

---

## 📡 Why Only Two Robots?

Pulse is a **two-party protocol** by design:

* It assumes only one peer.
* Token comparison and conflict logic are strictly pairwise.

If you want to scale this to **N robots**, you'd need:

* A central arbiter, **or**
* A more complex distributed agreement protocol (e.g. Ricart–Agrawala, token ring, or RAFT)

---

## 📬 Protocol Summary

Each heartbeat includes:

```json
{
  "msg_type": "HEARTBEAT",
  "robot_id": 1,
  "seq_num": 42,
  "mode": "PROPOSAL",
  "current_node": "B3",
  "proposal": {
    "id": "1-42",
    "token": 9834271,
    "path": ["B4", "B5"]
  },
  "response_to": "2-41",
  "response": "REJ"
}
```
