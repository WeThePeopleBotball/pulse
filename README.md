<table align="center">
  <tr>
    <td><img src=".github/assets/logo.png" alt="Socks Logo" width="80"></td>
    <td><h1 style="margin: 0;">Pulse: Peer-to-Peer Path Reservation Service</h1></td>
  </tr>
</table>

**Pulse** is a tiny, UDP-based coordinator that lets two robots safely reserve and traverse shared “nodes” in a map without collision. It works by exchanging periodic **heartbeat** messages carrying:

- **Mode** (`IDLE`, `PROPOSAL`, `EXECUTION`)  
- **Current node** (where the robot is physically)  
- **Pending proposal** (a unique `proposal_id`, random `token`, and desired `path`)  
- **Current path** (when in `EXECUTION`)  
- **Piggy-backed responses** (`ACK` or `REJ` to a peer’s proposal)

## How It Works

1. **IDLE**  
   - Always **accept** and ACK incoming proposals.  
   - Vacate any conflicting nodes immediately (no blocking) - use `set_accept_callback(Path p)` for that.  
2. **PROPOSAL**  
   - Send your desired path and wait (up to `proposal_timeout`) for an ACK or REJ.  
   - If both robots propose at the same time:  
     - Higher **token** wins.  
     - On tie, higher **robot_id** wins.  
3. **EXECUTION**  
   - You’ve locked the path—heartbeats now include your full `current_path`.  
   - Incoming proposals are ACK’d if paths don’t overlap, REJ’d otherwise.  
   - When you’re done (or aborted), you return to **IDLE**.

## Getting Started

- Include **`pulse.hpp`**/**`pulse.cpp`** in your project.  
- Instantiate:

```cpp
  pulse::Endpoint peer{"<peer-ip>", <peer-port>};
  pulse::Pulse coordinator(my_id, hb_ms, timeout_ms, my_port, peer);
```

* Call `run()` to start networking threads.
* Use `set_current_node()`, `set_path()`, then `propose()` to negotiate.
* Optionally register an **accept callback** to be notified when you ACK a peer’s request.
* Stop with `abort_execution()` or `stop()` when finished.

Pulse gives you a dead-simple, reliable reservation layer so two robots can share resources without stepping on each other.
