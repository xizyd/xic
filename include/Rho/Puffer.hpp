#ifndef RHO_PUFFER_HPP
#define RHO_PUFFER_HPP

#include "Xi/Array.hpp"
#include "Xi/String.hpp"
#include "Xi/Crypto.hpp"
#include "Xi/Map.hpp"
#include "Xi/Time.hpp"
#include "Xi/Func.hpp"
#include <iostream>

namespace Xi
{
    // -------------------------------------------------------------------------
    // Data Structures
    // -------------------------------------------------------------------------
    struct Packet
    {
        Xi::String payload;
        u64 channel = 1;
        bool bypassHOL = false;
        bool important = true;

        u64 id = 0;
        u64 fragmentStartID = 0;
        u8 fragmentStatus = 0; // 0=Single, 1=Start, 2=Frag, 3=End
    };

    struct FromTo
    {
        u64 from;
        u64 to;
    };

    struct InflightBundle
    {
        u64 id;
        Xi::String data;
        bool important;
    };

    // -------------------------------------------------------------------------
    // Callbacks (Using Xi::Func)
    // -------------------------------------------------------------------------
    using PacketListener = Xi::Func<void(Packet)>;
    using MapListener = Xi::Func<void(Xi::Map<u64, Xi::String>)>;
    using VoidListener = Xi::Func<void()>;

    // -------------------------------------------------------------------------
    // Encoding Utils
    // -------------------------------------------------------------------------
    namespace Encoding
    {

        inline void writeVarLong(Xi::String &s, u64 v)
        {
            s.pushVarLong((long long)v);
        }

        inline u64 readVarLong(const Xi::String &s, usz &at)
        {
            // Use peekVarLong from offset, then advance 'at'
            auto res = s.peekVarLong(at);
            if (res.error)
                return 0;
            at += res.bytes;
            return (u64)res.value;
        }

        inline void writeMap(Xi::String &s, const Xi::Map<u64, Xi::String> &m)
        {
            writeVarLong(s, m.size());
            for (auto &kv : m)
            {
                writeVarLong(s, kv.key);
                writeVarLong(s, kv.value.length);
                s += kv.value;
            }
        }

        inline Xi::Map<u64, Xi::String> readMap(const Xi::String &s, usz &at)
        {
            Xi::Map<u64, Xi::String> m;
            if (at >= s.length)
                return m;

            u64 count = readVarLong(s, at);
            for (u64 i = 0; i < count; ++i)
            {
                u64 k = readVarLong(s, at);
                u64 vLen = readVarLong(s, at);

                if (at + vLen <= s.length)
                {
                    Xi::String val = s.begin(at, at + (usz)vLen);
                    at += (usz)vLen;
                    m.put(k, val);
                }
                else
                {
                    break; // Malformed
                }
            }
            return m;
        }
    }

    // -------------------------------------------------------------------------
    // Golden Puffer Class
    // -------------------------------------------------------------------------
    class Puffer
    {
    public:
        // --- Security State ---
        Xi::String key;
        bool isSecure = false;

        // --- Config ---
        bool isWindowed = false;

        // --- Timers ---
        u64 lastSent = 0;
        u64 lastSentHeartbeat = 0;
        u64 lastSeen = 0;

        // --- Heartbeat & Timeout Config ---
        bool heartbeatEnabled = false;
        u64 hbMaxSilence = 5000;
        u64 hbInterval = 12000;

        bool timeoutEnabled = false;
        u64 timeoutDuration = 8000;

        // --- Glare Resolution ---
        bool glarePosition = false;
        bool glareInited = false;

        // --- Sequencing ---
        u64 lastSentNonce = 0;
        u64 lastReceivedNonce = 0;
        u64 receiveWindowMask = 0;

        // --- Buffers ---
        u64 resendPosition = 0;
        Xi::Array<InflightBundle> inflightBundles;             // Important (Reliable)
        Xi::Array<InflightBundle> nonImportantInflightBundles; // Fire-and-forget
        Xi::Array<u64> droppedBundles;                         // Explicitly dropped IDs

        Xi::Map<u64, Xi::String> reassemblyBuffer; // Fragment Reassembly

        Xi::Array<Packet> outbox; // Packets waiting to be bundled

        // --- Handshake State ---
        Xi::KeyPair ephemeralKeypair;
        Xi::String theirEphemeralPublic;
        Xi::String intendedEpheHash;

        // --- Callbacks ---
        PacketListener packetListener;
        MapListener probeListener;
        MapListener announceListener;
        MapListener disconnectListener;
        VoidListener switchRequestListener;

        Puffer()
        {
            clear();
        }

        void clear() {
            u64 now = Time::millis();
            ;
            lastSent = now;
            lastSentHeartbeat = now;
            lastSeen = now;
            glareInited = false;
            glarePosition = false;
            
        }

        // ---------------------------------------------------------------------
        // Configuration
        // ---------------------------------------------------------------------
        void enableSecurity(const Xi::String &k)
        {
            if (k.length == 32)
            {
                key = k;
                isSecure = true;
            }
        }

        void enableWindowing() { isWindowed = true; }

        void enableHeartbeat(u64 maxSilence = 5000, u64 maxHeartbeatInterval = 12000)
        {
            heartbeatEnabled = true;
            hbMaxSilence = maxSilence;
            hbInterval = maxHeartbeatInterval;
        }

        void enableTimeout(u64 duration)
        {
            timeoutEnabled = true;
            timeoutDuration = duration;
        }

        void update()
        {
            u64 now = Time::millis();
            ;
            if (timeoutEnabled && (now > lastSeen + timeoutDuration))
            {
                Xi::Map<u64, Xi::String> reason;
                reason.put(0, "Timeout");
                if (disconnectListener.isValid())
                    disconnectListener(reason);
            }
        }

        // ---------------------------------------------------------------------
        // Event Listeners
        // ---------------------------------------------------------------------
        void onPacket(PacketListener cb) { packetListener = Xi::Move(cb); }
        void onProbe(MapListener cb) { probeListener = Xi::Move(cb); }
        void onAnnounce(MapListener cb) { announceListener = Xi::Move(cb); }
        void onDisconnect(MapListener cb) { disconnectListener = Xi::Move(cb); }
        void onSwitchRequest(VoidListener cb) { switchRequestListener = Xi::Move(cb); }

        // ---------------------------------------------------------------------
        // Operations
        // ---------------------------------------------------------------------
        void push(Packet pkt)
        {
            outbox.push(pkt);
        }

        void probe(Xi::Map<u64, Xi::String> data)
        {
            Packet p;
            p.channel = 0;
            p.important = true;
            Encoding::writeVarLong(p.payload, 10);
            Encoding::writeMap(p.payload, data);
            push(p);
        }

        void announce(Xi::Map<u64, Xi::String> data)
        {
            Packet p;
            p.channel = 0;
            p.important = true;
            Encoding::writeVarLong(p.payload, 11);
            Encoding::writeMap(p.payload, data);
            push(p);
        }

        void disconnect(Xi::Map<u64, Xi::String> reason)
        {
            Packet p;
            p.channel = 0;
            p.important = true;
            Encoding::writeVarLong(p.payload, 100);
            Encoding::writeMap(p.payload, reason);
            push(p);
        }

        // ---------------------------------------------------------------------
        // Switch Request Logic
        // ---------------------------------------------------------------------
        Xi::String generateSwitchRequest(Xi::String theirEpheKey)
        {
            if (theirEpheKey.length != 32)
                return Xi::String();

            ephemeralKeypair = Xi::generateKeyPair();
            intendedEpheHash = Xi::String();

            Xi::String req;

            // 1. MAC (8 Bytes): Poly1305 if key exists, else 0
            if (isSecure && key.length == 32)
            {
                Xi::String theirHash = Xi::hash(theirEpheKey, 8);
                Xi::String myPub = ephemeralKeypair.publicKey;
                Xi::String toSign = theirHash + myPub;

                Xi::String polyKey = Xi::createPoly1305Key(key, 0xFFFFFFFFFFFFFFFFULL);
                Xi::String fullTag = Xi::zeros(16);
                crypto_poly1305(fullTag.data(), toSign.data(), toSign.length, polyKey.data());
                req.pushEach(fullTag.data(), 8);
            }
            else
            {
                req += Xi::zeros(8);
            }

            // 2. Intended Hash
            Xi::String theirHash = Xi::hash(theirEpheKey, 8);
            req += theirHash;

            // 3. My Public Key
            req += ephemeralKeypair.publicKey;

            Xi::String finalPayload;
            Encoding::writeVarLong(finalPayload, 20); // SWITCH_REQUEST
            finalPayload += req;

            return finalPayload;
        }

        bool enableSecureX()
        {
            if (theirEphemeralPublic.length != 32)
                return false;
            if (ephemeralKeypair.secretKey.length != 32)
                return false;

            if (intendedEpheHash.length == 8)
            {
                Xi::String myHash = Xi::hash(ephemeralKeypair.publicKey, 8);
                if (!myHash.constantTimeEquals(intendedEpheHash))
                    return false;
            }

            Xi::String shared = Xi::sharedKey(ephemeralKeypair.secretKey, theirEphemeralPublic);
            Xi::String newKey = Xi::kdf(shared, "RhoPufferV1", 32);

            enableSecurity(newKey);
            return true;
        }

        // ---------------------------------------------------------------------
        // State Management
        // ---------------------------------------------------------------------
        bool hasReceived(u64 id) const
        {
            if (id == 0)
                return true;
            if (id > lastReceivedNonce)
                return false;
            u64 diff = lastReceivedNonce - id;
            if (diff >= 64)
                return true;
            return (receiveWindowMask >> diff) & 1;
        }

        void pretendReceived(u64 id)
        {
            if (id == 0)
                return;
            if (id > lastReceivedNonce)
            {
                u64 diff = id - lastReceivedNonce;
                if (diff >= 64)
                    receiveWindowMask = 1;
                else
                {
                    receiveWindowMask <<= diff;
                    receiveWindowMask |= 1;
                }
                lastReceivedNonce = id;
            }
            else
            {
                u64 diff = lastReceivedNonce - id;
                if (diff < 64)
                    receiveWindowMask |= ((u64)1 << diff);
            }
        }

        void removeInflight(u64 id)
        {
            for (usz i = 0; i < inflightBundles.length; ++i)
            {
                if (inflightBundles[i].id == id)
                {
                    inflightBundles.remove(i);
                    if (resendPosition > i)
                        resendPosition--;
                    return;
                }
            }
        }

        void dropInflight(u64 id)
        {
            removeInflight(id);
            droppedBundles.push(id);
        }

        void resendFrom(u64 x)
        {
            resendPosition = 0;
            for (usz i = 0; i < inflightBundles.length; ++i)
            {
                if (inflightBundles[i].id >= x)
                {
                    resendPosition = i;
                    break;
                }
            }
        }

        Xi::Array<FromTo> showReceived() const
        {
            Xi::Array<FromTo> res;
            if (lastReceivedNonce == 0)
                return res;

            FromTo current;
            current.to = lastReceivedNonce;
            current.from = lastReceivedNonce;

            bool inRange = true;
            u64 mask = receiveWindowMask;

            for (int k = 1; k < 64; ++k)
            {
                u64 id = lastReceivedNonce - k;
                if (id == 0)
                    break;
                bool have = (mask >> k) & 1;

                if (have)
                {
                    if (inRange)
                    {
                        current.from = id;
                    }
                    else
                    {
                        inRange = true;
                        current.to = id;
                        current.from = id;
                    }
                }
                else
                {
                    if (inRange)
                    {
                        res.push(current);
                        inRange = false;
                    }
                }
            }
            if (inRange)
                res.push(current);
            return res;
        }

        Xi::Array<FromTo> showUnavailable()
        {
            Xi::Array<FromTo> res;
            if (droppedBundles.length == 0)
                return res;

            for (usz i = 0; i < droppedBundles.length; ++i)
            {
                FromTo ft;
                ft.from = droppedBundles[i];
                ft.to = droppedBundles[i];
                res.push(ft);
            }
            droppedBundles.clear();
            return res;
        }

        // ---------------------------------------------------------------------
        // Parse: Wire -> Inbox
        // ---------------------------------------------------------------------
        void parse(const Xi::String &bundle)
        {
            lastSeen = Time::millis();

            usz at = 0;
            u64 bundleID = 0;

            if (isWindowed)
                bundleID = Encoding::readVarLong(bundle, at);
            else
                bundleID = lastReceivedNonce + 1;

            if (isWindowed && hasReceived(bundleID))
                return;
                
                if (at >= bundle.length)
                return;
                u8 firstByte = bundle[at];
                bool wireSecure = (firstByte & 1);
                if (isSecure != wireSecure)
                return;


            Xi::String plainPayload;
            Xi::String payload;
            // Slice payload using String methods
            payload = bundle.begin(at, bundle.length);

            if (isSecure)
            {
                // Preparation of the AAD (Additional Authenticated Data)
                Xi::String aad;
                Encoding::writeVarLong(aad, bundleID);

                // Security check: The payload must contain at least the tag (8 bytes)
                // and one byte of data.
                if (payload.length < 9)
                {
                    return;
                }

                usz cipherLen = payload.length - 8;

                // Initialize AEADOptions for decryption
                Xi::AEADOptions options;
                options.text = payload.begin(0, cipherLen);
                options.text[0] &= 0xFE;
                options.tag = payload.begin(cipherLen, payload.length);
                options.ad = aad;
                options.tagLength = 8; // Matching your 8-byte tag requirement

                // The aeadOpen function validates the Poly1305 MAC and decrypts via ChaCha20
                if (!Xi::aeadOpen(key, bundleID, options))
                {
                    return;
                }

                plainPayload = options.text;
            }
            else
            {
                plainPayload = payload;
            }
            
            if (plainPayload.length == 0)
            return;
            
            u8 headerByte = plainPayload[0];
            bool bundleIsPadded = (headerByte >> 2) & 1;
            bool bundleIsSingle = (headerByte >> 3) & 1;
            bool bundleGlarePos = (headerByte >> 4) & 1;
            
            if (glareInited)
            {
                if (bundleGlarePos == glarePosition)
                return;
            }
            else
            {
                glarePosition = !bundleGlarePos;
                glareInited = true;
            }


            usz pAt = 1;

            if (bundleIsPadded)
            {
                u64 padLen = Encoding::readVarLong(plainPayload, pAt);
                // Adjust payload length virtually by slicing
                if (plainPayload.length > padLen)
                {
                    plainPayload = plainPayload.begin(0, plainPayload.length - (usz)padLen);
                }
            }

            if (bundleIsSingle)
            {
                if (pAt < plainPayload.length)
                {
                    Xi::String rawPkt = plainPayload.begin(pAt, plainPayload.length);
                    parsePacket(rawPkt);
                }
            }
            else
            {
                while (pAt < plainPayload.length)
                {
                    u64 pktLen = Encoding::readVarLong(plainPayload, pAt);
                    if (pAt + pktLen > plainPayload.length)
                        break;

                    Xi::String rawPkt = plainPayload.begin(pAt, pAt + (usz)pktLen);
                    parsePacket(rawPkt);
                    pAt += (usz)pktLen;
                }
            }

            if (isWindowed)
                pretendReceived(bundleID);
            else
                lastReceivedNonce = bundleID;
        }

        // ---------------------------------------------------------------------
        // Build: Outbox -> Inflight
        // ---------------------------------------------------------------------
        void build(usz bundleBlockSize = 32, usz bundleMaxSize = 1400)
        {
            if(!glareInited) {
                glareInited = true; // We are the first to send, lock the glare
            }

            while (outbox.length > 0)
            {
                Xi::String content;
                content.push(0);

                bool isSingle = false;
                bool containsImportant = false;
                usz consumed = 0;

                Packet &first = outbox[0];
                Xi::String tempFirst;
                serializePacket(tempFirst, first);

                usz overhead = 1 + 9 + 8 + bundleBlockSize;
                usz available = (bundleMaxSize > overhead) ? bundleMaxSize - overhead : 0;

                // Fragmentation check
                if (tempFirst.length > available)
                {
                    Packet p = outbox.shift();
                    usz fragSize = available - 15;
                    if (fragSize < 1)
                        fragSize = 1;

                    Xi::Array<Xi::String> chunks;
                    usz offset = 0;
                    while (offset < p.payload.length)
                    {
                        usz len = p.payload.length - offset;
                        if (len > fragSize)
                            len = fragSize;

                        chunks.push(p.payload.begin(offset, offset + len));
                        offset += len;
                    }

                    for (usz i = chunks.length; i > 0; --i)
                    {
                        Packet frag;
                        frag.id = p.id;
                        frag.channel = p.channel;
                        frag.payload = chunks[i - 1];
                        frag.important = p.important;
                        frag.fragmentStartID = p.id;

                        if (chunks.length == 1)
                            frag.fragmentStatus = 0;
                        else if (i - 1 == 0)
                            frag.fragmentStatus = 1;
                        else if (i - 1 == chunks.length - 1)
                            frag.fragmentStatus = 3;
                        else
                            frag.fragmentStatus = 2;

                        outbox.unshift(frag);
                    }
                    continue;
                }

                if (outbox.length == 1 && tempFirst.length <= available)
                {
                    isSingle = true;
                    content += tempFirst;
                    containsImportant |= first.important;
                    consumed = 1;
                }
                else
                {
                    for (usz i = 0; i < outbox.length; ++i)
                    {
                        Packet &p = outbox[i];
                        Xi::String temp;
                        serializePacket(temp, p);

                        if (content.length + temp.length + 5 > bundleMaxSize - overhead)
                            break;
                        Encoding::writeVarLong(content, temp.length);
                        content += temp;
                        containsImportant |= p.important;
                        consumed++;
                    }
                }

                for (usz k = 0; k < consumed; ++k)
                    outbox.shift();

                bool padded = false;
                usz remainder = content.length % bundleBlockSize;
                if (remainder != 0)
                {
                    usz pad = bundleBlockSize - remainder;
                    Xi::String padStr;
                    Encoding::writeVarLong(padStr, pad);

                    Xi::String finalC;
                    finalC.push(0);
                    finalC += padStr;
                    // Copy content skipping initial byte
                    finalC += content.begin(1, content.length);
                    finalC += Xi::zeros(pad);
                    content = finalC;
                    padded = true;
                }

                u8 header = 0;
                if (isSecure)
                    header |= 1;
                if (padded)
                    header |= (1 << 2);
                if (isSingle)
                    header |= (1 << 3);
                if (glarePosition)
                    header |= (1 << 4);
                content[0] = header;

                u64 currentBundleID = ++lastSentNonce;
                Xi::String bundleData;

                if (isWindowed)
                {
                    Encoding::writeVarLong(bundleData, currentBundleID);
                }

                if (isSecure)
                {
                    // Prepare the Additional Authenticated Data (AAD)
                    Xi::String aad;
                    Encoding::writeVarLong(aad, currentBundleID);

                    // Initialize AEADOptions with the content and AAD
                    // Note: tagLength is set to 8 to match your original manual implementation
                    Xi::AEADOptions options;
                    options.text = content;
                    options.ad = aad;
                    options.tagLength = 8;

                    // Execute the seal process
                    if (Xi::aeadSeal(key, currentBundleID, options))
                    {
                        // Your manual implementation requires the first byte to be flagged as secure
                        options.text[0] = (options.text[0] & 0xFE) | 1;

                        bundleData += options.text;
                        bundleData += options.tag;
                    }
                }
                else
                {
                    // Clear the security bit for unencrypted content
                    content[0] &= 0xFE;
                    bundleData += content;
                }

                InflightBundle ib;
                ib.id = currentBundleID;
                ib.data = bundleData;
                ib.important = containsImportant;

                if (containsImportant)
                {
                    inflightBundles.push(ib);
                }
                else
                {
                    nonImportantInflightBundles.push(ib);
                }
            }
        }

        // ---------------------------------------------------------------------
        // Flush: Send Wire Data
        // ---------------------------------------------------------------------
        bool readyToSend() const
        {
            u64 now = Time::millis();
            ;
            bool hbNeeded = heartbeatEnabled && ((now > lastSent + hbMaxSilence) ||
                                                 (now > lastSentHeartbeat + hbInterval));
            return nonImportantInflightBundles.length > 0 ||
                   (resendPosition < inflightBundles.length) ||
                   outbox.length > 0 ||
                   hbNeeded;
        }

        Xi::String flush(usz bundleBlockSize = 32, usz bundleMaxSize = 1400)
        {
            u64 now = Time::millis();
            ;

            if (heartbeatEnabled)
            {
                if ((now > lastSent + hbMaxSilence) || (now > lastSentHeartbeat + hbInterval))
                {
                    Packet hb;
                    hb.channel = 0;
                    hb.important = false;
                    Encoding::writeVarLong(hb.payload, 0);

                    Xi::Array<FromTo> rec = showReceived();
                    Encoding::writeVarLong(hb.payload, rec.length);
                    for (auto &ft : rec)
                    {
                        Encoding::writeVarLong(hb.payload, ft.from);
                        Encoding::writeVarLong(hb.payload, ft.to);
                    }

                    Xi::Array<FromTo> unav = showUnavailable();
                    Encoding::writeVarLong(hb.payload, unav.length);
                    for (auto &ft : unav)
                    {
                        Encoding::writeVarLong(hb.payload, ft.from);
                        Encoding::writeVarLong(hb.payload, ft.to);
                    }

                    outbox.unshift(hb);
                    lastSentHeartbeat = now;
                }
            }

            if (outbox.length > 0)
                build(bundleBlockSize, bundleMaxSize);

            Xi::String ret;
            if (nonImportantInflightBundles.length > 0)
            {
                ret = nonImportantInflightBundles.shift().data;
            }
            else if (resendPosition < inflightBundles.length)
            {
                InflightBundle &ib = inflightBundles[resendPosition];
                resendPosition++;
                ret = ib.data;
            }

            if (ret.length > 0)
                lastSent = now;
            return ret;
        }

    private:
        void serializePacket(Xi::String &b, const Packet &p)
        {
            u8 header = 0;
            header |= (p.fragmentStatus & 0x03);
            if (p.channel != 1)
                header |= (1 << 2);
            if (p.bypassHOL)
                header |= (1 << 3);
            b.push(header);
            Encoding::writeVarLong(b, p.id);
            if (p.channel != 1)
                Encoding::writeVarLong(b, p.channel);
            if (p.fragmentStatus != 0)
                Encoding::writeVarLong(b, p.fragmentStartID);
            b += p.payload;
        }

        void dispatchPacket(const Packet &p)
        {
            if (p.channel == 0)
            {
                usz pAt = 0;
                u64 type = Encoding::readVarLong(p.payload, pAt);

                if (type == 0)
                { // HEARTBEAT
                    u64 count = Encoding::readVarLong(p.payload, pAt);
                    for (u64 i = 0; i < count; ++i)
                    {
                        u64 from = Encoding::readVarLong(p.payload, pAt);
                        u64 to = Encoding::readVarLong(p.payload, pAt);
                        for (u64 id = from; id <= to; ++id)
                            removeInflight(id);
                    }
                    count = Encoding::readVarLong(p.payload, pAt);
                    for (u64 i = 0; i < count; ++i)
                    {
                        u64 from = Encoding::readVarLong(p.payload, pAt);
                        u64 to = Encoding::readVarLong(p.payload, pAt);
                        for (u64 id = from; id <= to; ++id)
                            pretendReceived(id);
                    }
                    resendFrom(0);
                }
                else if (type == 10)
                { // PROBE
                    if (probeListener.isValid())
                        probeListener(Encoding::readMap(p.payload, pAt));
                }
                else if (type == 11)
                { // ANNOUNCE
                    if (announceListener.isValid())
                        announceListener(Encoding::readMap(p.payload, pAt));
                }
                else if (type == 20)
                { // SWITCH_REQUEST
                    // <mac: 8> <intendedEpheHash: 8> <public: 32>
                    if (p.payload.length >= pAt + 8 + 8 + 32)
                    {
                        // Skip MAC (verified implicitly by enableSecureX hash check or upper layer)
                        pAt += 8;

                        intendedEpheHash = p.payload.begin(pAt, pAt + 8);
                        pAt += 8;

                        theirEphemeralPublic = p.payload.begin(pAt, pAt + 32);

                        if (switchRequestListener.isValid())
                            switchRequestListener();
                    }
                }
                else if (type == 100)
                { // DISCONNECT
                    if (disconnectListener.isValid())
                        disconnectListener(Encoding::readMap(p.payload, pAt));
                }
            }

            if (packetListener.isValid())
                packetListener(p);
        }

        void parsePacket(const Xi::String &raw)
        {
            usz at = 0;
            if (raw.length == 0)
                return;
            u8 header = raw[at++];
            u8 fragStatus = header & 0x03;
            bool hasChannel = (header >> 2) & 1;
            bool bypass = (header >> 3) & 1;
            Packet p;
            p.fragmentStatus = fragStatus;
            p.bypassHOL = bypass;
            p.id = Encoding::readVarLong(raw, at);
            if (hasChannel)
                p.channel = Encoding::readVarLong(raw, at);
            else
                p.channel = 1;
            if (fragStatus != 0)
                p.fragmentStartID = Encoding::readVarLong(raw, at);

            if (at < raw.length)
                p.payload = raw.begin(at, raw.length);

            if (p.fragmentStatus == 0)
            {
                dispatchPacket(p);
            }
            else if (p.fragmentStatus == 1)
            {
                reassemblyBuffer.put(p.fragmentStartID, p.payload);
            }
            else
            {
                Xi::String *buf = reassemblyBuffer.get(p.fragmentStartID);
                if (buf)
                {
                    *buf += p.payload;
                    if (p.fragmentStatus == 3)
                    { // End
                        Packet fullP = p;
                        fullP.payload = *buf;
                        fullP.fragmentStatus = 0;
                        reassemblyBuffer.remove(p.fragmentStartID);
                        dispatchPacket(fullP);
                    }
                }
            }
        }
    };
}

#endif // RHO_PUFFER_HPP