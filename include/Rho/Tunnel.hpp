#ifndef RHO_TUNNEL_HPP
#define RHO_TUNNEL_HPP

#include "../Xi/Array.hpp"
#include "../Xi/Crypto.hpp"
#include "../Xi/Func.hpp"
#include "../Xi/Map.hpp"
#include "../Xi/String.hpp"
#include "../Xi/Time.hpp"

namespace Xi {
struct Packet {
  Xi::String payload;
  u64 channel = 1;
  bool bypassHOL = false;
  bool important = true;
  u64 id = 0;
  u64 fragmentStartID = 0;
  u8 fragmentStatus = 0;
  Packet()
      : channel(1), bypassHOL(false), important(true), id(0),
        fragmentStartID(0), fragmentStatus(0) {}
  Packet(const Xi::String &p, u64 c = 1)
      : payload(p), channel(c), bypassHOL(false), important(true), id(0),
        fragmentStartID(0), fragmentStatus(0) {}
};

struct FromTo {
  u64 from;
  u64 to;
};
struct InflightBundle {
  u64 id;
  Xi::String data;
  bool important;
};

using PacketListener = Xi::Func<void(Packet)>;
using MapListener = Xi::Func<void(Xi::Map<u64, Xi::String>)>;
using VoidListener = Xi::Func<void()>;

namespace Encoding {
inline void writeVarLong(Xi::String &s, u64 v) { s.pushVarLong((long long)v); }
inline u64 readVarLong(const Xi::String &s, usz &at) {
  auto res = s.peekVarLong(at);
  if (res.error)
    return 0;
  at += res.bytes;
  return (u64)res.value;
}
inline void writeMap(Xi::String &s, const Xi::Map<u64, Xi::String> &m) {
  writeVarLong(s, m.size());
  for (auto &kv : m) {
    writeVarLong(s, kv.key);
    writeVarLong(s, kv.value.length);
    s += kv.value;
  }
}
inline Xi::Map<u64, Xi::String> readMap(const Xi::String &s, usz &at) {
  Xi::Map<u64, Xi::String> m;
  if (at >= s.length)
    return m;
  u64 count = readVarLong(s, at);
  for (u64 i = 0; i < count; ++i) {
    u64 k = readVarLong(s, at);
    u64 realLen = readVarLong(s, at);
    if (at + realLen <= s.length) {
      m.put(k, s.begin(at, at + (usz)realLen));
      at += (usz)realLen;
    } else
      break;
  }
  return m;
}
} // namespace Encoding

class Tunnel {
public:
  Xi::String name = "Tunnel";
  Xi::String key;
  bool isSecure = false, isWindowed = false, isAsleep = false;
  u64 lastSent = 0, lastSentHeartbeat = 0, lastSeen = 0;
  bool destroyAfterFlush = false, windowAfterFlush = false,
       secureAfterFlush = false, secureXAfterFlush = false;
  u64 aliveTimeout = 8000, disconnectTimeout = 20000;
  u64 lastSentNonce = 0, lastReceivedNonce = 0, receiveWindowMask = 0,
      resendPosition = 0;
  Xi::Array<InflightBundle> inflightBundles, nonImportantInflightBundles;
  Xi::Array<u64> droppedBundles;
  Xi::Map<u64, Xi::String> reassemblyBuffer;
  Xi::Array<Packet> outbox;
  Xi::KeyPair ephemeralKeypair;
  Xi::String theirEphemeralPublic, intendedEpheHash;
  PacketListener packetListener;
  MapListener probeListener, announceListener, disconnectListener;
  VoidListener switchRequestListener, destroyListener;

  Tunnel() { clear(); }
  void clear() {
    u64 now = Xi::millis();
    lastSent = now;
    lastSentHeartbeat = now;
    lastSeen = now;
    isAsleep = false;
    destroyAfterFlush = false;
    lastSentNonce = 0;
    lastReceivedNonce = 0;
    receiveWindowMask = 0;
  }

  void enableSecurity(const Xi::String &k) {
    if (k.length == 32) {
      key = k;
      isSecure = true;
    }
  }
  void enableWindowing(int windowSize = 64) { isWindowed = true; }
  void enableSecurityAfterFlush(const Xi::String &k) {
    if (k.length == 32) {
      key = k;
      secureAfterFlush = true;
    }
  }
  void enableSecurityXAfterFlush() { secureXAfterFlush = true; }
  void enableWindowingAfterFlush() { windowAfterFlush = true; }
  void setAliveTimeout(u64 t) {
    aliveTimeout = t;
    if (t == 0)
      isAsleep = false;
  }
  void setDisconnectTimeout(u64 t) { disconnectTimeout = t; }

  void update() {
    u64 now = Xi::millis();
    if (disconnectTimeout > 0 && !destroyAfterFlush &&
        (now > lastSeen + disconnectTimeout)) {
      Xi::Map<u64, Xi::String> reason;
      reason.put(0, "Timeout");
      disconnect(reason);
      if (disconnectListener.isValid())
        disconnectListener(reason);
      destroyAfterFlush = true;
    }
  }

  void onPacket(PacketListener cb) { packetListener = Xi::Move(cb); }
  void onProbe(MapListener cb) { probeListener = Xi::Move(cb); }
  void onAnnounce(MapListener cb) { announceListener = Xi::Move(cb); }
  void onDisconnect(MapListener cb) { disconnectListener = Xi::Move(cb); }
  void onDestroy(VoidListener cb) { destroyListener = Xi::Move(cb); }
  void onSwitchRequest(VoidListener cb) {
    switchRequestListener = Xi::Move(cb);
  }

  void push(Packet pkt) { outbox.push(pkt); }
  void push(Xi::String s, u64 c = 1) { push(Packet(s, c)); }

  void probe(Xi::Map<u64, Xi::String> data) {
    Packet p;
    p.channel = 0;
    p.important = true;
    Encoding::writeVarLong(p.payload, 10);
    Encoding::writeMap(p.payload, data);
    push(p);
  }
  void announce(Xi::Map<u64, Xi::String> data) {
    Packet p;
    p.channel = 0;
    p.important = true;
    Encoding::writeVarLong(p.payload, 11);
    Encoding::writeMap(p.payload, data);
    push(p);
  }
  void disconnect(Xi::Map<u64, Xi::String> reason) {
    Packet p;
    p.channel = 0;
    p.important = true;
    Encoding::writeVarLong(p.payload, 1000); // 100 was shared with handshake
    Encoding::writeMap(p.payload, reason);
    push(p);
  }

  Xi::String generateSwitchRequest(Xi::String theirEpheKey) {
    if (theirEpheKey.length != 32)
      return Xi::String();
    theirEphemeralPublic = theirEpheKey;
    if (!ephemeralKeypair.publicKey.length)
      ephemeralKeypair = Xi::generateKeyPair();
    Xi::String req;
    if (isSecure && key.length == 32) {
      Xi::String theirHash = Xi::hash(theirEpheKey, 8),
                 myPub = ephemeralKeypair.publicKey, toSign = theirHash + myPub;
      Xi::String polyKey = Xi::createPoly1305Key(key, 0xFFFFFFFFFFFFFFFFULL);
      Xi::String fullTag = Xi::zeros(16);
      crypto_poly1305(fullTag.data(), toSign.data(), toSign.length,
                      polyKey.data());
      req.pushEach(fullTag.data(), 8);
    } else
      req += Xi::zeros(8);
    req += Xi::hash(theirEpheKey, 8);
    req += ephemeralKeypair.publicKey;
    Xi::String res;
    Encoding::writeVarLong(res, 20);
    res += req;
    return res;
  }

  bool enableSecurityX() {
    if (theirEphemeralPublic.length != 32 ||
        ephemeralKeypair.secretKey.length != 32)
      return false;
    if (intendedEpheHash.length == 8) {
      Xi::String myHash = Xi::hash(ephemeralKeypair.publicKey, 8);
      if (!myHash.constantTimeEquals(intendedEpheHash))
        return false;
    }
    Xi::String shared =
        Xi::sharedKey(ephemeralKeypair.secretKey, theirEphemeralPublic);
    Xi::String newKey = Xi::kdf(shared, "RhoPufferV1", 32);
    enableSecurity(newKey);
    return true;
  }

  void parsePacket(const Xi::String &raw) {
    usz cursor = 0;
    u8 header = raw[cursor++];
    Packet p;
    p.fragmentStatus = header & 0x03;
    bool hasChannel = (header >> 2) & 1;
    p.bypassHOL = (header >> 3) & 1;
    p.id = Encoding::readVarLong(raw, cursor);
    if (hasChannel)
      p.channel = Encoding::readVarLong(raw, cursor);
    else
      p.channel = 1;
    if (p.fragmentStatus != 0)
      p.fragmentStartID = Encoding::readVarLong(raw, cursor);
    if (cursor < raw.length)
      p.payload = raw.begin(cursor, raw.length);
    dispatchPacket(p);
  }

  void dispatchPacket(const Packet &p) {
    if (p.channel == 0) {
      usz pAt = 0;
      u64 type = Encoding::readVarLong(p.payload, pAt);
      if (type == 0) {
        if (!isWindowed)
          return;
        u64 count = Encoding::readVarLong(p.payload, pAt);
        for (u64 i = 0; i < count; ++i) {
          u64 f = Encoding::readVarLong(p.payload, pAt),
              t = Encoding::readVarLong(p.payload, pAt);
          for (u64 x = f; x <= t; ++x)
            removeInflight(x);
        }
        count = Encoding::readVarLong(p.payload, pAt);
        for (u64 i = 0; i < count; ++i) {
          u64 f = Encoding::readVarLong(p.payload, pAt),
              t = Encoding::readVarLong(p.payload, pAt);
          for (u64 x = f; x <= t; ++x)
            pretendReceived(x);
        }
        resendFrom(0);
      } else if (type == 10) {
        if (probeListener.isValid())
          probeListener(Encoding::readMap(p.payload, pAt));
      } else if (type == 11) {
        if (announceListener.isValid())
          announceListener(Encoding::readMap(p.payload, pAt));
      } else if (type == 1000) {
        if (disconnectListener.isValid())
          disconnectListener(Encoding::readMap(p.payload, pAt));
      } else if (type == 20) {
        pAt += 8;
        if (pAt + 8 <= p.payload.length) {
          intendedEpheHash = p.payload.begin(pAt, pAt + 8);
          pAt += 8;
        }
        if (pAt + 32 <= p.payload.length) {
          theirEphemeralPublic = p.payload.begin(pAt, pAt + 32);
        }
        if (switchRequestListener.isValid())
          switchRequestListener();
      }
    } else if (packetListener.isValid())
      packetListener(p);
  }

  void parse(const Xi::String &bundle) {
    lastSeen = Xi::millis();
    if (isAsleep)
      isAsleep = false;
    usz at = 0;
    u64 bID = 0;
    if (isWindowed) {
      bID = Encoding::readVarLong(bundle, at);
      if (hasReceived(bID))
        return;
    } else
      bID = lastReceivedNonce + 1; // Expected next

    Xi::String plain;
    Xi::String payload = bundle.begin(at, bundle.length);
    if (isSecure) {
      if (payload.length < 9)
        return;
      Xi::String aad;
      Encoding::writeVarLong(aad, bID);
      Xi::AEADOptions opt;
      opt.tag = payload.begin(0, 8);
      opt.text = payload.begin(8, payload.length);
      opt.ad = aad;
      opt.tagLength = 8;
      // We try bID. If it fails and we are not windowed, we might be out of
      // sync. But logically, non-windowed in a bus should be careful.
      if (!Xi::aeadOpen(key, bID, opt))
        return;
      plain = opt.text;
    } else
      plain = payload;

    if (plain.length == 0)
      return;
    // Success! Update nonce tracker.
    if (!isWindowed)
      lastReceivedNonce = bID;
    else
      pretendReceived(bID); // Updates lastReceivedNonce to max seen

    usz pAt = 0;
    u8 hb = plain[pAt++];
    bool padded = (hb >> 2) & 1, single = (hb >> 3) & 1;
    Xi::String content;
    if (padded) {
      u64 pLen = Encoding::readVarLong(plain, pAt);
      if (pAt + pLen <= plain.length)
        content = plain.begin(pAt, pAt + (usz)pLen);
      else
        return;
    } else
      content = plain.begin(pAt, plain.length);
    if (single)
      parsePacket(content);
    else {
      usz sAt = 0;
      while (sAt < content.length) {
        u64 pkL = Encoding::readVarLong(content, sAt);
        if (sAt + pkL > content.length)
          break;
        parsePacket(content.begin(sAt, sAt + (usz)pkL));
        sAt += (usz)pkL;
      }
    }
  }

  void build(usz bBS = 32, usz bMS = 1400) {
    if (isAsleep)
      return;
    while (outbox.length > 0) {
      Xi::String py;
      py.push(0);
      bool single = false, important = false;
      usz consumed = 0;
      Xi::String tF;
      serializePacket(tF, outbox[0]);
      usz overhead = 1 + 9 + 8 + bBS,
          avail = (bMS > overhead) ? bMS - overhead : 0;
      if (tF.length > avail) {
        Packet p = outbox.shift();
        usz fS = (avail > 15) ? avail - 15 : 1, off = 0;
        while (off < p.payload.length) {
          usz len = (p.payload.length - off > fS) ? fS : p.payload.length - off;
          Packet f(p.payload.begin(off, off + len), p.channel);
          f.id = p.id;
          f.important = p.important;
          f.fragmentStartID = p.id;
          f.fragmentStatus = (off == 0)
                                 ? (p.payload.length <= off + len ? 0 : 1)
                                 : (p.payload.length <= off + len ? 3 : 2);
          outbox.unshift(f);
          off += len;
        }
        continue;
      }
      if (outbox.length == 1) {
        single = true;
        py += tF;
        important |= outbox[0].important;
        consumed = 1;
      } else {
        for (usz i = 0; i < outbox.length; ++i) {
          Xi::String t;
          serializePacket(t, outbox[i]);
          if (py.length + t.length + 9 > avail)
            break;
          Encoding::writeVarLong(py, t.length);
          py += t;
          important |= outbox[i].important;
          consumed++;
        }
      }
      for (usz k = 0; k < consumed; ++k)
        outbox.shift();
      Xi::String fP;
      fP.push(0);
      bool pad = false;
      usz dL = py.length - 1;
      Xi::String lV;
      Encoding::writeVarLong(lV, dL);
      usz cT = 1 + lV.length + dL, rem = cT % bBS;
      if (rem != 0) {
        pad = true;
        fP += lV;
        fP.pushEach(py.data() + 1, dL);
        fP += Xi::zeros(bBS - rem);
      } else
        fP.pushEach(py.data() + 1, dL);
      u8 h = 0;
      if (isSecure)
        h |= 1;
      if (pad)
        h |= (1 << 2);
      if (single)
        h |= (1 << 3);
      fP[0] = h;
      Xi::String bD;
      u64 cBID = ++lastSentNonce;
      if (isWindowed)
        Encoding::writeVarLong(bD, cBID);
      if (isSecure) {
        Xi::String aad;
        Encoding::writeVarLong(aad, cBID);
        Xi::AEADOptions opt;
        opt.text = fP;
        opt.ad = aad;
        opt.tagLength = 8;
        if (Xi::aeadSeal(key, cBID, opt)) {
          bD += opt.tag;
          bD += opt.text;
        }
      } else
        bD.pushEach(fP.data(), fP.length);
      InflightBundle ib;
      ib.id = cBID;
      ib.data = Xi::String(bD.data(), bD.length);
      ib.important = important;
      if (important)
        inflightBundles.push(ib);
      else
        nonImportantInflightBundles.push(ib);
    }
  }

  // Common methods
  bool hasReceived(u64 id) const {
    if (id == 0)
      return true;
    if (id > lastReceivedNonce)
      return false;
    u64 diff = lastReceivedNonce - id;
    if (diff >= 64)
      return true;
    return (receiveWindowMask >> diff) & 1;
  }
  void pretendReceived(u64 id) {
    if (id == 0)
      return;
    if (id > lastReceivedNonce) {
      u64 diff = id - lastReceivedNonce;
      if (diff >= 64)
        receiveWindowMask = 1;
      else {
        receiveWindowMask <<= diff;
        receiveWindowMask |= 1;
      }
      lastReceivedNonce = id;
    } else {
      u64 diff = lastReceivedNonce - id;
      if (diff < 64)
        receiveWindowMask |= ((u64)1 << diff);
    }
  }
  void removeInflight(u64 id) {
    for (usz i = 0; i < inflightBundles.length; ++i)
      if (inflightBundles[i].id == id) {
        inflightBundles.remove(i);
        if (resendPosition > i)
          resendPosition--;
        return;
      }
  }
  void resendFrom(u64 x) {
    resendPosition = 0;
    for (usz i = 0; i < inflightBundles.length; ++i)
      if (inflightBundles[i].id >= x) {
        resendPosition = i;
        break;
      }
  }
  Xi::Array<FromTo> showReceived() const {
    Xi::Array<FromTo> res;
    if (lastReceivedNonce == 0)
      return res;
    FromTo cur;
    cur.to = lastReceivedNonce;
    cur.from = lastReceivedNonce;
    bool inRange = true;
    u64 mask = receiveWindowMask;
    for (int k = 1; k < 64; ++k) {
      u64 id = lastReceivedNonce - k;
      if (id == 0)
        break;
      bool have = (mask >> k) & 1;
      if (have) {
        if (inRange)
          cur.from = id;
        else {
          inRange = true;
          cur.to = id;
          cur.from = id;
        }
      } else if (inRange) {
        res.push(cur);
        inRange = false;
      }
    }
    if (inRange)
      res.push(cur);
    return res;
  }
  Xi::Array<FromTo> showUnavailable() {
    Xi::Array<FromTo> res;
    for (usz i = 0; i < droppedBundles.length; ++i) {
      FromTo ft;
      ft.from = droppedBundles[i];
      ft.to = droppedBundles[i];
      res.push(ft);
    }
    droppedBundles.clear();
    return res;
  }
  void serializePacket(Xi::String &b, const Packet &p) {
    u8 h = (p.fragmentStatus & 0x03);
    if (p.channel != 1)
      h |= (1 << 2);
    if (p.bypassHOL)
      h |= (1 << 3);
    b.push(h);
    Encoding::writeVarLong(b, p.id);
    if (p.channel != 1)
      Encoding::writeVarLong(b, p.channel);
    if (p.fragmentStatus != 0)
      Encoding::writeVarLong(b, p.fragmentStartID);
    b += p.payload;
  }

  bool readyToSend() const {
    if (isAsleep)
      return false;
    u64 now = Xi::millis();
    u64 hI = aliveTimeout > 0 ? (u64)(aliveTimeout / 2.5) : 0;
    bool hb = (aliveTimeout > 0) && ((now > lastSent + aliveTimeout) ||
                                     (now > lastSentHeartbeat + hI));
    return nonImportantInflightBundles.length > 0 ||
           (resendPosition < inflightBundles.length) || outbox.length > 0 || hb;
  }

  Xi::String flush(usz bBS = 32, usz bMS = 1400) {
    if (isAsleep)
      return Xi::String();
    u64 now = Xi::millis();
    if (destroyAfterFlush && inflightBundles.length == 0 &&
        nonImportantInflightBundles.length == 0 && outbox.length == 0) {
      if (destroyListener.isValid()) {
        destroyListener();
        return Xi::String();
      }
    }
    if (aliveTimeout > 0) {
      u64 hI = (u64)(aliveTimeout / 2.5);
      if ((now > lastSent + aliveTimeout) || (now > lastSentHeartbeat + hI)) {
        Packet h;
        h.channel = 0;
        h.important = false;
        Encoding::writeVarLong(h.payload, 0);
        if (isWindowed) {
          auto rec = showReceived();
          Encoding::writeVarLong(h.payload, rec.length);
          for (auto &f : rec) {
            Encoding::writeVarLong(h.payload, f.from);
            Encoding::writeVarLong(h.payload, f.to);
          }
          auto un = showUnavailable();
          Encoding::writeVarLong(h.payload, un.length);
          for (auto &f : un) {
            Encoding::writeVarLong(h.payload, f.from);
            Encoding::writeVarLong(h.payload, f.to);
          }
        } else {
          Encoding::writeVarLong(h.payload, 0);
          Encoding::writeVarLong(h.payload, 0);
        }
        outbox.unshift(h);
        lastSentHeartbeat = now;
      }
    }
    if (outbox.length > 0)
      build(bBS, bMS);
    Xi::String ret;
    if (nonImportantInflightBundles.length > 0) {
      InflightBundle ib = nonImportantInflightBundles.shift();
      ret = Xi::String(ib.data.data(), ib.data.length);
    } else if (resendPosition < inflightBundles.length) {
      InflightBundle &ib = inflightBundles[resendPosition++];
      ret = Xi::String(ib.data.data(), ib.data.length);
    }

    if (ret.length > 0) {
      lastSent = Xi::millis();
      if (!isWindowed) {
        inflightBundles.clear();
        resendPosition = 0;
      }
    }

    if (aliveTimeout > 0 && (Xi::millis() - lastSeen > aliveTimeout))
      isAsleep = true;
    if (secureAfterFlush) {
      isSecure = true;
      secureAfterFlush = false;
    }
    if (windowAfterFlush) {
      isWindowed = true;
      windowAfterFlush = false;
    }
    if (secureXAfterFlush) {
      enableSecurityX();
      secureXAfterFlush = false;
    }
    return ret;
  }
};
} // namespace Xi
#endif
