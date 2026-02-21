#ifndef XI_RHO_REACH_HPP
#define XI_RHO_REACH_HPP 1

#include "../Xi/Array.hpp"
#include "../Xi/Map.hpp"
#include "../Xi/Path.hpp"
#include "../Xi/String.hpp"
#include "Meta.hpp"
#include "Railway.hpp"
#include "Tunnel.hpp"

namespace Xi {
namespace Rho {

class Reach {
public:
  static String
  toNumericalHostnameTargetSource(const NumericalHostname &target,
                                  const NumericalHostname &source) {
    String res;
    // <LengthOfSource, Target, Source>
    // Use VarLong for length and each segment
    res.pushVarLong((long long)source.size());

    for (usz i = 0; i < target.size(); ++i) {
      res.pushVarLong((long long)target[i]);
    }

    for (usz i = 0; i < source.size(); ++i) {
      res.pushVarLong((long long)source[i]);
    }
    return res;
  }

  static void fromNumericalHostnameTargetSource(const String &data,
                                                NumericalHostname &target,
                                                NumericalHostname &source) {
    target.clear();
    source.clear();
    usz at = 0;

    auto lenRes = data.peekVarLong(at);
    if (lenRes.error)
      return;
    usz sourceLen = (usz)lenRes.value;
    at += lenRes.bytes;

    // Count how many total VarLongs are left to figure out target length
    // We can't know exactly without parsing all of them, so let's parse
    // greedily until we hit (total - sourceLen)
    Array<u64> allVals;
    while (at < data.length()) {
      auto valRes = data.peekVarLong(at);
      if (valRes.error)
        break;
      allVals.push((u64)valRes.value);
      at += valRes.bytes;
    }

    usz targetLen =
        (allVals.size() > sourceLen) ? allVals.size() - sourceLen : 0;

    for (usz i = 0; i < targetLen; ++i) {
      target.push(allVals[i]);
    }

    for (usz i = targetLen; i < allVals.size(); ++i) {
      source.push(allVals[i]);
    }
  }

  RailwayStation *station = nullptr;
  NumericalHostname source;
  NumericalHostname finalDestination;
  Array<String> rootPublicKeys;
  Array<String> lastProofedPublicKeys;
  Array<NumericalHostname> defaultServers;
  Map<u8, String> meta;
  Hostname destination;
  int maxHops = 50;

  Map<String, String>
      log; // Using String, String since Map<Hostname, Hostname> requires
           // Hostname to be hashable and implicitly convertible.

  Reach() {}

  bool run() {
    if (!station)
      return false;

    for (int hop = 0; hop < maxHops; ++hop) {
      // 1- Check if reach.destination has names
      if (!destination.includesNames()) {
        // 2- If false, set finalDestination to destination and return
        finalDestination = NumericalHostname(destination);
        return true;
      }

      // 3- Set finalDestination to destination beforeNamed();
      Hostname beforeNamed = destination.beforeNamed();
      if (beforeNamed.size() == 0 && defaultServers.size() > 0) {
        // Choose a random one from defaultServers (using simple randomization)
        usz idx = Xi::millis() % defaultServers.size();
        finalDestination = defaultServers[idx];
      } else {
        finalDestination = NumericalHostname(beforeNamed);
      }

      // 4- Open a new station, called currentStation, add
      // addStation(reach.station)
      RailwayStation currentStation;
      currentStation.addStation(*station);

      // 5- Set its currentStation.meta[3] to toNumericalHostnameTargetSource
      String nhts = toNumericalHostnameTargetSource(finalDestination, source);
      currentStation.meta.put(Meta::NumericalHostnameTargetSource, nhts);

      // 6- Calls currentStation.enrail()
      currentStation.enrail();

      // 7- Make a new Tunnel instance, sets tunnel.meta[1] to
      // reach.destination, probes for ephemeralPublicKey
      Tunnel tunnel;
      tunnel.meta.put(Meta::Hostname, destination.toString(false));
      tunnel.initEphemeral();

      // Note: Since Tunnel doesn't do network I/O implicitly in this
      // synchronous loop, we'd normally attach packet listeners to route
      // between tunnel and currentStation. But since this is a high-level
      // representation, we just model the state changes. In a real async
      // runtime, this loop would yield while waiting for `tunnel.onReady`.

      // To simulate the wait and process asynchronously, we assume the host
      // system will pump events. Since this function returns bool synchronously
      // in the design, it implies blocking or simplified logic.

      // Simulate parsing Proofed response
      // 9- Checks returned proofeds against rootPublicKeys.
      String proofedData = tunnel.otherMeta.get(Meta::Proofed)
                               ? *tunnel.otherMeta.get(Meta::Proofed)
                               : String();

      // Mock: split proofed data by comma (assuming it's a comma separated
      // string of base64 keys for simplicity, or just raw bytes) Let's assume
      // proofedData contains PublicKeys.

      bool foundValidProof = false;
      // Simplified check:
      for (usz i = 0; i < rootPublicKeys.size(); ++i) {
        if (proofedData.find(rootPublicKeys[i].c_str()) != -1) {
          foundValidProof = true;
          // 10- If proofed contains a key that is in our rootPublicKeys, we
          // pick any value if there from tunnel.meta[5]
          String *nextKey = tunnel.otherMeta.get(Meta::PublicKey);
          if (nextKey) {
            rootPublicKeys.push(*nextKey);
          }
          break;
        }
      }

      // 11- If tunnel.meta[1] is not empty, we set reach.destination to
      // tunnel.meta[1]. Careful: 'tunnel.meta[1]' refers to the PEER'S meta
      // returned to us, so we look in `tunnel.otherMeta`
      String *returnedHostname = tunnel.otherMeta.get(Meta::Hostname);
      if (returnedHostname && !returnedHostname->isEmpty()) {
        destination = Hostname(*returnedHostname);
      }

      // 12- Set lastProofedPublicKeys to proofed from that tunnel.
      lastProofedPublicKeys.clear();
      if (!proofedData.isEmpty()) {
        lastProofedPublicKeys.push(proofedData);
      }

      // 13- Disconnect and destroy the tunnel.
      Map<u64, String> reason;
      reason.put(0, "Reach Step Complete");
      tunnel.disconnect(reason);
      currentStation.removeStation(*station);

      // 14- Back to step 1 (achieved by loop).
    }
    return false; // Reached max hops
  }
};

} // namespace Rho
} // namespace Xi

#endif // XI_RHO_REACH_HPP
