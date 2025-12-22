#ifndef RHO_NODE_HPP
#define RHO_NODE_HPP

#include "Puffer.hpp"
#include "../Xi/Crypto.hpp" 
#include "../Xi/Utils.hpp"

namespace Xi
{
    // -------------------------------------------------------------------------
    // Data Structures
    // -------------------------------------------------------------------------
    
    struct TypedData {
        u8 type;
        String data;
    };

    // -------------------------------------------------------------------------
    // Node Listener Interface
    // -------------------------------------------------------------------------
    struct NodeListener {
        // Lifecycle
        virtual void onLive() {}
        virtual void onDisconnect(bool local, const Array<TypedData>& data) {}
        
        // Data
        virtual void onMessage(const Packet& pkt) {}
        
        // Handshake
        virtual void onProbe(const Array<TypedData>& data) {}
        virtual void onAnnounce(const Array<TypedData>& data, const String& ephemeralPub) {}
        virtual void onSwitchRequest(const String& code, const Array<TypedData>& data, 
                                   const String& theirEphPub, const Array<String>& validatedStatics) {}
        virtual bool onSwitchAccepted(const Array<TypedData>& data, const Array<String>& validatedStatics) { return true; }
    };

    // -------------------------------------------------------------------------
    // Node Class
    // -------------------------------------------------------------------------
    class Node {
    public:
        Puffer tunnel;
        NodeListener* listener = null;
        
        // Configuration
        u64 activationTimeout = 2000; // ms

        // State
        bool live = false;
        u64 lastSentSack = 0;

        // Ephemeral State (Handshake)
        String myEphemeralSec;
        String myEphemeralPub;
        String theirEphemeralPub;
        String tempSharedSecret;
        String lastSwitchCode;
        
        // Key Rotation State
        bool tunnelNeedsUpgrade = false;
        String pendingNextKey;

        Node() {}

        void setListener(NodeListener* l) { listener = l; }

        // ---------------------------------------------------------------------
        // Public API
        // ---------------------------------------------------------------------

        bool active() const { return live; }

        // 1. Ingest wire data
        void parse(const String& bundle) {
            // Puffer handles Decryption, Replay Protection, and Reassembly
            tunnel.parse(bundle);

            // Process Puffer Inbox
            while(tunnel.inbox.length > 0) {
                // Optimization: Shift returns value, we move it to handler to avoid CoW
                handlePacket(tunnel.inbox.shift());
            }
        }

        // 2. Queue application message
        void push(const Packet& msg) {
            if (!live) return;
            if (msg.channel == 0) return; // Reserved
            tunnel.outbox.push(msg);
        }

        // 3. Generate wire data
        String flush(usz bundleBlockSize = 32, usz bundleMaxSize = 1400) {
            // Automatic SACK generation for Windowed mode
            if (live && tunnel.isWindowed && (millis() - lastSentSack > activationTimeout * 0.8)) {
                sendSack();
            }

            // Flush Puffer (Encrypts and packs bundles)
            String bundle = tunnel.flush(bundleBlockSize, bundleMaxSize);

            // Atomic Key Rotation
            if (tunnelNeedsUpgrade && pendingNextKey.len() == 32) {
                tunnel.enableSecurity(pendingNextKey);
                tunnelNeedsUpgrade = false;
                pendingNextKey = String(); 
            }

            return bundle;
        }

        // ---------------------------------------------------------------------
        // Control Operations
        // ---------------------------------------------------------------------

        void probe(const Array<TypedData>& data) {
            String payload;
            Encoding::writeVarLong(payload, 10); // CMD: PROBE
            serializeTypedDataArray(payload, data);
            
            Packet p; 
            p.channel = 0; 
            p.payload = payload; // CoW
            p.important = false;
            tunnel.outbox.push(p);
        }

        void announce(const Array<TypedData>& data) {
            initEphemeral();
            String payload;
            Encoding::writeVarLong(payload, 11); // CMD: ANNOUNCE
            serializeTypedDataArray(payload, data);
            payload += myEphemeralPub;

            Packet p; 
            p.channel = 0; 
            p.payload = payload; 
            p.important = false;
            tunnel.outbox.push(p);
        }

        void disconnect(const Array<TypedData>& data) {
            if (!live) return;
            String payload;
            Encoding::writeVarLong(payload, 2); // CMD: DISCONNECT
            serializeTypedDataArray(payload, data);
            
            Packet p; 
            p.channel = 0; 
            p.payload = payload; 
            p.important = true;
            tunnel.outbox.push(p);

            if (listener) listener->onDisconnect(true, data);
            destroy();
        }

        void requestSwitch(const Array<TypedData>& data, const String& destEphPub, const Array<KeyPair>& statics) {
            if (live) return; 
            initEphemeral();
            
            lastSwitchCode = Xi::randomBytes(8);
            theirEphemeralPub = destEphPub;
            
            tempSharedSecret = Xi::sharedKey(myEphemeralSec, theirEphemeralPub);
            String tempKey = Xi::kdf(tempSharedSecret, "RHO_SWITCH", 32);

            String plaintext;
            serializeTypedDataArray(plaintext, data);
            serializeStatics(plaintext, statics, destEphPub); 

            String sealed = Xi::aeadSeal(tempKey, 0, lastSwitchCode, plaintext);
            
            String payload;
            Encoding::writeVarLong(payload, 20); // CMD: SWITCH_REQ
            payload += lastSwitchCode;
            payload += myEphemeralPub;
            payload += sealed; 

            Packet p; p.channel = 0; p.payload = payload; p.important = true;
            tunnel.outbox.push(p);
        }

        void acceptSwitch(const String& code, const Array<TypedData>& data, const Array<KeyPair>& statics) {
            if (live) return;
            if (tempSharedSecret.len() == 0) return;

            String tempKey = Xi::kdf(tempSharedSecret, "RHO_SWITCH", 32);

            String plaintext;
            serializeTypedDataArray(plaintext, data);
            serializeStatics(plaintext, statics, theirEphemeralPub);

            String sealed = Xi::aeadSeal(tempKey, 1, code, plaintext);

            String payload;
            Encoding::writeVarLong(payload, 21); // CMD: SWITCH_RES
            payload += code;
            payload += sealed;

            Packet p; p.channel = 0; p.payload = payload; p.important = true;
            tunnel.outbox.push(p);

            tunnelNeedsUpgrade = true;
            pendingNextKey = Xi::kdf(tempSharedSecret, String(), 32); 
            live = true;
            if(listener) listener->onLive();
        }

    private:
        // ---------------------------------------------------------------------
        // Internals
        // ---------------------------------------------------------------------

        void initEphemeral() {
            if (myEphemeralSec.len() == 0) {
                auto kp = Xi::generateKeyPair(); 
                myEphemeralPub = kp.publicKey;
                myEphemeralSec = kp.secretKey;
            }
        }

        void destroy() {
            live = false;
            myEphemeralSec = String();
            tempSharedSecret = String();
        }

        void sendSack() {
            lastSentSack = millis();
            // Optimization: Don't build packet if window is empty or not windowed
            if (!tunnel.isWindowed) return;
            
            Array<FromTo> sackRanges = tunnel.showReceived();
            if (sackRanges.length == 0) return;

            String payload;
            Encoding::writeVarLong(payload, 1); // CMD: ACK
            Encoding::writeVarLong(payload, tunnel.lastReceivedNonce); 
            Encoding::writeVarLong(payload, sackRanges.length);
            for(auto& ft : sackRanges) {
                Encoding::writeVarLong(payload, ft.from);
                Encoding::writeVarLong(payload, ft.to);
            }
            
            Packet p; 
            p.channel = 0; 
            p.payload = payload; 
            p.important = false; 
            p.bypassHOL = true;
            tunnel.outbox.push(p);
        }

        void handlePacket(Packet&& msg) { // Move Semantics
            if (msg.channel != 0) {
                if (live && listener) listener->onMessage(msg);
                return;
            }

            usz cursor = 0;
            u64 cmd = Encoding::readVarLong(msg.payload, cursor);

            switch(cmd) {
                case 1: { // ACK
                    if (!tunnel.isWindowed) break;
                    Encoding::readVarLong(msg.payload, cursor); // Anchor (ignored, implicit in ranges)
                    u64 count = Encoding::readVarLong(msg.payload, cursor);
                    for(u64 i=0; i<count; ++i) {
                        u64 from = Encoding::readVarLong(msg.payload, cursor);
                        u64 to = Encoding::readVarLong(msg.payload, cursor);
                        // Batch removal
                        for(u64 id = from; id <= to; ++id) tunnel.removeInflight(id);
                    }
                    break;
                }
                case 2: { // DISCONNECT
                    if (!live) break;
                    Array<TypedData> data = parseTypedDataArray(msg.payload, cursor);
                    if (listener) listener->onDisconnect(false, data);
                    destroy();
                    break;
                }
                case 10: { // PROBE
                    if (listener) listener->onProbe(parseTypedDataArray(msg.payload, cursor));
                    break;
                }
                case 11: { // ANNOUNCE
                    Array<TypedData> data = parseTypedDataArray(msg.payload, cursor);
                    String pub;
                    if (cursor + 32 <= msg.payload.len()) {
                        pub.pushEach(msg.payload.data() + cursor, 32);
                        theirEphemeralPub = pub; 
                    }
                    if (listener) listener->onAnnounce(data, pub);
                    break;
                }
                case 20: { // SWITCH_REQ
                    if (live) break; 
                    initEphemeral();
                    
                    if (msg.payload.len() < cursor + 40) break;
                    
                    String code; 
                    code.pushEach(msg.payload.data() + cursor, 8); cursor += 8;
                    
                    String theirEph;
                    theirEph.pushEach(msg.payload.data() + cursor, 32); cursor += 32;
                    
                    String sealed;
                    sealed.pushEach(msg.payload.data() + cursor, msg.payload.len() - cursor);

                    theirEphemeralPub = theirEph;
                    tempSharedSecret = Xi::sharedKey(myEphemeralSec, theirEph);
                    String tempKey = Xi::kdf(tempSharedSecret, "RHO_SWITCH", 32);

                    String plain = Xi::aeadOpen(tempKey, 0, code, sealed);
                    if (plain.len() == 0) break; 

                    usz pCur = 0;
                    Array<TypedData> tData = parseTypedDataArray(plain, pCur);
                    Array<String> proofs = parseStatics(plain, pCur);
                    
                    Array<String> valid;
                    for(usz i=0; i<proofs.length; i+=2) {
                        const String& pub = proofs[i];
                        const String& sig = proofs[i+1];
                        String shared = Xi::sharedKey(myEphemeralSec, pub);
                        String calc = Xi::hash(shared, 8); 
                        if (sig.constantTimeEquals(calc)) valid.push(pub);
                    }

                    if (listener) listener->onSwitchRequest(code, tData, theirEph, valid);
                    break;
                }
                case 21: { // SWITCH_RES
                    if (!lastSwitchCode.len() || !tempSharedSecret.len()) break;
                    
                    String code;
                    code.pushEach(msg.payload.data() + cursor, 8); cursor += 8;
                    
                    if (!code.constantTimeEquals(lastSwitchCode)) break;

                    String sealed;
                    sealed.pushEach(msg.payload.data() + cursor, msg.payload.len() - cursor);
                    
                    String tempKey = Xi::kdf(tempSharedSecret, "RHO_SWITCH", 32);
                    String plain = Xi::aeadOpen(tempKey, 1, code, sealed);
                    if (plain.len() == 0) break;

                    usz pCur = 0;
                    Array<TypedData> tData = parseTypedDataArray(plain, pCur);
                    Array<String> proofs = parseStatics(plain, pCur);

                    Array<String> valid;
                    for(usz i=0; i<proofs.length; i+=2) {
                        const String& pub = proofs[i];
                        const String& sig = proofs[i+1];
                        String shared = Xi::sharedKey(myEphemeralSec, pub);
                        String calc = Xi::hash(shared, 8);
                        if (sig.constantTimeEquals(calc)) valid.push(pub);
                    }

                    live = true;
                    String tunnelKey = Xi::kdf(tempSharedSecret, String(), 32);
                    
                    if (listener && listener->onSwitchAccepted(tData, valid)) {
                        tunnel.enableSecurity(tunnelKey);
                        listener->onLive();
                    }
                    break;
                }
            }
        }

        // ---------------------------------------------------------------------
        // Serialization Helpers
        // ---------------------------------------------------------------------

        void serializeTypedDataArray(String& s, const Array<TypedData>& items) {
            Encoding::writeVarLong(s, items.length);
            for(auto& item : items) {
                Encoding::writeVarLong(s, item.type);
                Encoding::writeVarLong(s, item.data.len());
                s += item.data;
            }
        }

        Array<TypedData> parseTypedDataArray(const String& s, usz& at) {
            Array<TypedData> res;
            u64 count = Encoding::readVarLong(s, at);
            for(u64 i=0; i<count; ++i) {
                if (at >= s.len()) break;
                TypedData td;
                td.type = (u8)Encoding::readVarLong(s, at);
                u64 len = Encoding::readVarLong(s, at);
                if (at + len > s.len()) break;
                
                // Optimized slice copy
                td.data.pushEach(s.data() + at, (usz)len);
                at += (usz)len;
                res.push(td);
            }
            return res;
        }

        void serializeStatics(String& s, const Array<KeyPair>& items, const String& theirEph) {
            Encoding::writeVarLong(s, items.length);
            for(auto& item : items) {
                s += item.publicKey;
                String proofSec = Xi::sharedKey(item.secretKey, theirEph);
                String proof = Xi::hash(proofSec, 8);
                s += proof;
            }
        }

        Array<String> parseStatics(const String& s, usz& at) {
            Array<String> res;
            u64 count = Encoding::readVarLong(s, at);
            for(u64 i=0; i<count; ++i) {
                if (at + 32 + 8 > s.len()) break;
                
                String pub;
                pub.pushEach(s.data() + at, 32); at += 32;
                
                String proof;
                proof.pushEach(s.data() + at, 8); at += 8;
                
                res.push(pub);
                res.push(proof);
            }
            return res;
        }
    };
}

#endif // XI_NODE_HPP