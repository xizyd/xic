#ifndef RHO_PUFFER_HPP
#define RHO_PUFFER_HPP

#include "../Xi/Array.hpp"
#include "../Xi/String.hpp"
#include "../Xi/Crypto.hpp"
#include "../Xi/Utils.hpp"

namespace Xi
{
    // -------------------------------------------------------------------------
    // Data Structures
    // -------------------------------------------------------------------------
    struct Packet {
        String payload;
        u64 channel = 1;
        bool bypassHOL = false;
        bool important = true; 
        
        u64 id = 0; 
        u64 fragmentStartID = 0;
        u8 fragmentStatus = 0; // 0=Single, 1=Start, 2=Frag, 3=End
    };

    struct FromTo {
        u64 from;
        u64 to;
    };

    struct InflightBundle {
        u64 id;
        String data; 
        bool important;
    };

    // -------------------------------------------------------------------------
    // Encoding Utils
    // -------------------------------------------------------------------------
    namespace Encoding {
        inline void writeVarLong(String& s, u64 v) {
            do {
                u8 temp = (u8)(v & 0b01111111);
                v >>= 7;
                if (v != 0) temp |= 0b10000000;
                s.push(temp);
            } while (v != 0);
        }

        inline u64 readVarLong(const String& s, usz& at) {
            u64 num = 0;
            int shift = 0;
            if (at >= s.len()) return 0;
            const u8* d = s.data();
            
            while (true) {
                if (at >= s.len()) break; 
                u8 b = d[at++];
                num |= (u64)(b & 0x7F) << shift;
                if ((b & 0x80) == 0) break;
                shift += 7;
            }
            return num;
        }
    }

    // -------------------------------------------------------------------------
    // Golden Puffer Class
    // -------------------------------------------------------------------------
    class Puffer {
    public:
        // Config
        String key; 
        bool isSecure = false;
        bool isWindowed = false; 

        // Glare Resolution
        bool glarePosition = false;
        bool glareInited = false;

        // Sequencing
        u64 lastSentNonce = 0;
        u64 lastReceivedNonce = 0;
        u64 receiveWindowMask = 0; 
        
        // Buffers
        u64 resendPosition = 0;
        Array<InflightBundle> inflightBundles;             // Important (Map-like via ID)
        Array<InflightBundle> nonImportantInflightBundles; // Fire-and-forget
        
        Array<Packet> outbox; // User pushes here
        Array<Packet> inbox;  // User pops from here

        Puffer() {}

        // ---------------------------------------------------------------------
        // Configuration
        // ---------------------------------------------------------------------
        void enableSecurity(const String& k) {
            if (k.len() == 32) {
                key = k;
                isSecure = true;
            }
        }
        
        void enableWindowing() { isWindowed = true; }

        // ---------------------------------------------------------------------
        // State Management
        // ---------------------------------------------------------------------

        bool hasReceived(u64 id) const {
            if (id == 0) return true; 
            if (id > lastReceivedNonce) return false; 
            u64 diff = lastReceivedNonce - id;
            if (diff >= 64) return true; 
            return (receiveWindowMask >> diff) & 1;
        }

        void pretendReceived(u64 id) {
            if (id == 0) return;
            if (id > lastReceivedNonce) {
                u64 diff = id - lastReceivedNonce;
                if (diff >= 64) receiveWindowMask = 1; 
                else {
                    receiveWindowMask <<= diff;
                    receiveWindowMask |= 1;
                }
                lastReceivedNonce = id;
            } else {
                u64 diff = lastReceivedNonce - id;
                if (diff < 64) receiveWindowMask |= ((u64)1 << diff);
            }
        }

        void removeInflight(u64 id) {
            // Remove from Important
            for (usz i = 0; i < inflightBundles.length; ++i) {
                if (inflightBundles[i].id == id) {
                    inflightBundles.remove(i);
                    if (resendPosition > i) resendPosition--;
                    return; 
                }
            }
        }

        void resendFrom(u64 x) {
            resendPosition = 0;
            for(usz i=0; i<inflightBundles.length; ++i) {
                if(inflightBundles[i].id >= x) {
                    resendPosition = i;
                    break;
                }
            }
        }
        
        Array<FromTo> showReceived() const {
            Array<FromTo> res;
            if (lastReceivedNonce == 0) return res;
            
            // Current head
            FromTo head; head.from = lastReceivedNonce; head.to = lastReceivedNonce;
            res.push(head);

            // Scan window bits
            u64 mask = receiveWindowMask;
            // Bit 0 is lastReceivedNonce (already added).
            // Bit k corresponds to lastReceivedNonce - k.
            for(int k=1; k<64; ++k) {
                if ((mask >> k) & 1) {
                    u64 id = lastReceivedNonce - k;
                    if (id > 0) {
                        FromTo ft; ft.from = id; ft.to = id;
                        res.push(ft);
                    }
                }
            }
            return res;
        }

        // ---------------------------------------------------------------------
        // Parse: Wire -> Inbox
        // ---------------------------------------------------------------------
        void parse(const String& bundle) {
            usz at = 0;
            u64 bundleID = 0;

            // 1. Read Bundle ID (Exposed Metadata)
            if (isWindowed) bundleID = Encoding::readVarLong(bundle, at);
            else bundleID = lastReceivedNonce + 1;

            // 2. Replay & Window Check
            if (isWindowed && hasReceived(bundleID)) return;
            
            // 3. Security Check (The Header Trick)
            // If secure, we expect at least MAC (8 bytes) + Header (1 byte)
            if (at >= bundle.len()) return;
            
            // Check the "Is Secure" bit in the encrypted/plaintext header byte
            u8 firstByte = bundle[at];
            bool wireSecure = (firstByte & 1);
            if (isSecure != wireSecure) return; // Mismatch logic

            // 4. Decrypt & Authenticate
            String plainPayload;
            String payload; 
            payload.pushEach(bundle.data() + at, bundle.len() - at);

            if (isSecure) {
                String aad;
                Encoding::writeVarLong(aad, bundleID);

                if (payload.len() < 9) return; // Header(1) + MAC(8) min
                usz cipherLen = payload.len() - 8;
                
                String ciphertext;
                ciphertext.pushEach(payload.data(), cipherLen);
                String mac;
                mac.pushEach(payload.data() + cipherLen, 8);

                String polyKey = createPoly1305Key(key, bundleID);
                
                usz aad_len = aad.len();
                usz cipher_len = ciphertext.len();
                usz aad_pad = (16 - (aad_len % 16)) % 16;
                usz cipher_pad = (16 - (cipher_len % 16)) % 16;
                
                String dataToAuth;
                dataToAuth += aad;
                dataToAuth += Xi::zeros(aad_pad);
                dataToAuth += ciphertext; 
                dataToAuth += Xi::zeros(cipher_pad);
                for (int i = 0; i < 8; ++i) dataToAuth.push((aad_len >> (i * 8)) & 0xFF);
                for (int i = 0; i < 8; ++i) dataToAuth.push((cipher_len >> (i * 8)) & 0xFF);

                String calcFullTag = Xi::zeros(16);
                crypto_poly1305(calcFullTag.data(), dataToAuth.data(), dataToAuth.len(), polyKey.data());
                
                String calcTag;
                calcTag.pushEach(calcFullTag.data(), 8);
                if (!mac.constantTimeEquals(calcTag)) return;

                // Decrypt
                // Note: The LSB of ciphertext[0] was forced. 
                // Decryption will yield a garbage LSB in plainPayload[0].
                plainPayload = streamXor(key, bundleID, ciphertext, 1);
            } else {
                plainPayload = payload;
            }

            if (plainPayload.len() == 0) return;

            // 5. Parse Bundle Header
            u8 headerByte = plainPayload[0]; 
            // Bit 0: Secure (Ignored here, handled by trick)
            bool bundleIsCompressed = (headerByte >> 1) & 1;
            bool bundleIsPadded = (headerByte >> 2) & 1;
            bool bundleIsSingle = (headerByte >> 3) & 1;
            bool bundleGlarePos = (headerByte >> 4) & 1;

            // 6. Glare Resolution
            if (glareInited) {
                if (bundleGlarePos == glarePosition) return; // Drop glare
            } else {
                glarePosition = !bundleGlarePos;
                glareInited = true;
            }

            // 7. Parse Body
            usz pAt = 1; // Skip header
            
            if (bundleIsPadded) {
                u64 padLen = Encoding::readVarLong(plainPayload, pAt);
                if (plainPayload.len() > padLen) plainPayload.length -= (usz)padLen; 
            }
            
            if (bundleIsCompressed) {
                u64 origLen = Encoding::readVarLong(plainPayload, pAt);
                (void)origLen; // Compression hook
            }

            if (bundleIsSingle) {
                if (pAt < plainPayload.len()) {
                    String rawPkt;
                    rawPkt.pushEach(plainPayload.data() + pAt, plainPayload.len() - pAt);
                    parsePacket(rawPkt);
                }
            } else {
                while (pAt < plainPayload.len()) {
                    u64 pktLen = Encoding::readVarLong(plainPayload, pAt);
                    if (pAt + pktLen > plainPayload.len()) break;
                    String rawPkt;
                    rawPkt.pushEach(plainPayload.data() + pAt, (usz)pktLen);
                    parsePacket(rawPkt);
                    pAt += (usz)pktLen;
                }
            }
            
            // 8. Update State
            if (isWindowed) pretendReceived(bundleID);
            else lastReceivedNonce = bundleID;
        }

        // ---------------------------------------------------------------------
        // Build: Outbox -> Inflight
        // ---------------------------------------------------------------------
        void build(usz bundleBlockSize = 32, usz bundleMaxSize = 1400) {
            while (outbox.length > 0) {
                String content;
                content.push(0); // Header slot
                
                bool isSingle = false;
                bool containsImportant = false;
                usz consumed = 0;

                // Greedy Packing Strategy
                
                // 1. Try Single Packet Optimization
                Packet& first = outbox[0];
                String tempFirst;
                serializePacket(tempFirst, first);
                
                // Check for Fragmentation requirement
                // Overhead estimate: Header(1) + ID(9) + MAC(8) + Pad(BlockSize)
                usz overhead = 1 + 9 + 8 + bundleBlockSize; 
                usz available = (bundleMaxSize > overhead) ? bundleMaxSize - overhead : 0;

                if (tempFirst.len() > available) {
                    // **Fragmentation Kicks In**
                    // We split outbox[0] into fragments and unshift them back to outbox
                    // Then continue loop to pack them normally.
                    
                    Packet p = outbox.shift(); // Remove large packet
                    
                    // We need to account for Packet Header overhead in fragments (~10 bytes)
                    usz fragSize = available - 15; // Conservative
                    if (fragSize < 1) fragSize = 1;

                    Array<String> chunks;
                    usz offset = 0;
                    while(offset < p.payload.len()) {
                        usz len = p.payload.len() - offset;
                        if (len > fragSize) len = fragSize;
                        String chunk;
                        chunk.pushEach(p.payload.data() + offset, len);
                        chunks.push(chunk);
                        offset += len;
                    }

                    // Create Fragments
                    for (usz i = chunks.length; i > 0; --i) {
                        Packet frag;
                        frag.id = p.id; // Same Message ID
                        frag.channel = p.channel;
                        frag.payload = chunks[i-1];
                        frag.important = p.important;
                        frag.fragmentStartID = p.id; 
                        
                        if (chunks.length == 1) frag.fragmentStatus = 0; // Single (shouldn't happen here usually)
                        else if (i-1 == 0) frag.fragmentStatus = 1; // Start
                        else if (i-1 == chunks.length - 1) frag.fragmentStatus = 3; // End
                        else frag.fragmentStatus = 2; // Middle

                        outbox.unshift(frag);
                    }
                    continue; // Restart loop to pack the fragments
                }

                // Normal Packing
                if (outbox.length == 1 && tempFirst.len() <= available) {
                    // Single Packet Mode
                    isSingle = true;
                    content += tempFirst;
                    containsImportant |= first.important;
                    consumed = 1;
                } else {
                    // Multi Packet Mode
                    for (usz i = 0; i < outbox.length; ++i) {
                        Packet& p = outbox[i];
                        String temp;
                        serializePacket(temp, p);
                        
                        // Check Size limit
                        if (content.len() + temp.len() + 5 > bundleMaxSize - overhead) break;
                        
                        Encoding::writeVarLong(content, temp.len());
                        content += temp;
                        containsImportant |= p.important;
                        consumed++;
                    }
                }

                // Remove packed packets
                for(usz k=0; k<consumed; ++k) outbox.shift();

                // ---------------------------------------------------------
                // Finalize Bundle (Padding & Header)
                // ---------------------------------------------------------
                
                // Padding
                bool padded = false;
                usz remainder = content.len() % bundleBlockSize;
                if (remainder != 0) {
                    usz pad = bundleBlockSize - remainder;
                    String padStr;
                    Encoding::writeVarLong(padStr, pad);
                    
                    String finalC;
                    finalC.push(0); // Header
                    finalC += padStr;
                    finalC.pushEach(content.data() + 1, content.len() - 1);
                    finalC += Xi::zeros(pad);
                    content = finalC;
                    padded = true;
                }

                // Header Construction
                u8 header = 0;
                if (isSecure) header |= 1;
                // Bit 1: Compressed (todo)
                if (padded) header |= (1 << 2);
                if (isSingle) header |= (1 << 3);
                if (glarePosition) header |= (1 << 4);
                content[0] = header;

                u64 currentBundleID = ++lastSentNonce;
                String bundleData;
                
                if (isWindowed) Encoding::writeVarLong(bundleData, currentBundleID);

                // Encryption / Wire Formation
                if (isSecure) {
                    String aad;
                    Encoding::writeVarLong(aad, currentBundleID);
                    
                    // Encrypt
                    String ciphertext = streamXor(key, currentBundleID, content, 1);
                    
                    // The Trick: Force Ciphertext[0] LSB to 1
                    ciphertext[0] = (ciphertext[0] & 0xFE) | 1;

                    // MAC
                    String polyKey = createPoly1305Key(key, currentBundleID);
                    
                    usz aad_len = aad.len();
                    usz cipher_len = ciphertext.len();
                    usz aad_pad = (16 - (aad_len % 16)) % 16;
                    usz cipher_pad = (16 - (cipher_len % 16)) % 16;
                    
                    String dataToAuth;
                    dataToAuth += aad;
                    dataToAuth += Xi::zeros(aad_pad);
                    dataToAuth += ciphertext;
                    dataToAuth += Xi::zeros(cipher_pad);
                    for (int i = 0; i < 8; ++i) dataToAuth.push((aad_len >> (i * 8)) & 0xFF);
                    for (int i = 0; i < 8; ++i) dataToAuth.push((cipher_len >> (i * 8)) & 0xFF);

                    String fullTag = Xi::zeros(16);
                    crypto_poly1305(fullTag.data(), dataToAuth.data(), dataToAuth.len(), polyKey.data());

                    bundleData += ciphertext;
                    bundleData.pushEach(fullTag.data(), 8);
                } else {
                    // Plaintext Mode
                    // The Trick: Force Content[0] LSB to 0
                    content[0] = (content[0] & 0xFE); 
                    bundleData += content;
                }

                InflightBundle ib;
                ib.id = currentBundleID;
                ib.data = bundleData;
                ib.important = containsImportant;

                if (containsImportant) {
                    inflightBundles.push(ib);
                } else {
                    nonImportantInflightBundles.push(ib);
                }
            }
        }

        // ---------------------------------------------------------------------
        // Flush: Send Wire Data
        // ---------------------------------------------------------------------
        bool readyToSend() const { 
            return nonImportantInflightBundles.length > 0 || 
                   (resendPosition < inflightBundles.length); 
        }

        String flush(usz bundleBlockSize = 32, usz bundleMaxSize = 1400) {
            // 1. Packetize new items from Outbox
            if (outbox.length > 0) {
                build(bundleBlockSize, bundleMaxSize);
            }

            // 2. Priority 1: Non-Important (Fire & Forget)
            if (nonImportantInflightBundles.length > 0) {
                return nonImportantInflightBundles.shift().data;
            }

            // 3. Priority 2: Important (Resend / New)
            if (resendPosition < inflightBundles.length) {
                InflightBundle& ib = inflightBundles[resendPosition];
                resendPosition++;
                return ib.data; 
            }

            return String();
        }
    private:
        void serializePacket(String& b, const Packet& p) {
            u8 header = 0;
            header |= (p.fragmentStatus & 0x03);
            if (p.channel != 1) header |= (1 << 2);
            if (p.bypassHOL) header |= (1 << 3);
            b.push(header);
            Encoding::writeVarLong(b, p.id);
            if (p.channel != 1) Encoding::writeVarLong(b, p.channel);
            if (p.fragmentStatus != 0) Encoding::writeVarLong(b, p.fragmentStartID);
            b += p.payload;
        }

        void parsePacket(const String& raw) {
            usz at = 0;
            if (raw.len() == 0) return;
            u8 header = raw[at++];
            u8 fragStatus = header & 0x03;
            bool hasChannel = (header >> 2) & 1;
            bool bypass = (header >> 3) & 1;
            Packet p;
            p.fragmentStatus = fragStatus;
            p.bypassHOL = bypass;
            p.id = Encoding::readVarLong(raw, at);
            if (hasChannel) p.channel = Encoding::readVarLong(raw, at);
            else p.channel = 1;
            if (fragStatus != 0) p.fragmentStartID = Encoding::readVarLong(raw, at);
            if (at < raw.len()) p.payload.pushEach(raw.data() + at, raw.len() - at);
            inbox.push(p);
        }
    };
} 

#endif // XI_PUFFER_HPP