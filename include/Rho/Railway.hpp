#ifndef RHO_RAILWAY_HPP
#define RHO_RAILWAY_HPP

// #include <iostream>
#include "Xi/Array.hpp"
#include "Xi/String.hpp"
#include "Xi/Map.hpp"
#include "Xi/Crypto.hpp"
#include "Xi/Func.hpp"
#include "Xi/Time.hpp"

namespace Xi
{

    // -------------------------------------------------------------------------
    // Primitives & Flags
    // -------------------------------------------------------------------------
    static const unsigned char RAILWAY_SECURE = 0x01;
    static const unsigned char RAILWAY_HAS_META = 0x04;

    // -------------------------------------------------------------------------
    // Railway Class
    // -------------------------------------------------------------------------

    class Railway
    {
    public:
        struct RailwayPacket
        {
            u32 channel;
            Xi::String payload;
        };

        struct RailwayChannel
        {
            Xi::String key;
            Xi::Array<Xi::u8> bitmap; // Window bitmap
            Xi::u64 slidePos;

            Xi::u64 lastSentNonce;

            Xi::u64 lastReceivedTime;
            Xi::u64 lastSentTime;

            String lastReceivedMeta;
            bool updateMeta = false;

            Xi::Map<u64, Xi::String> meta; // Switched from Array<TypedData> to Map
        };

    private:
        Xi::Map<u32, RailwayChannel> channels;
        Xi::Array<u32> availableToGenerate;

        int windowBitmapSize;
        int scanLength;
        Xi::u64 destroyTimeout;

        // --- Helpers ---

        void generateNewAvailableId()
        {
            while (true)
            {
                u32 newId = (Xi::random(100000)) + 1; // 1 to 100000

                // Ensure not in use
                if (channels.has(newId))
                    continue;

                // Ensure not in available list
                if (availableToGenerate.find(newId) != -1)
                    continue;

                availableToGenerate.push(newId);
                return;
            }
        }

    public:
        Railway()
        {
            windowBitmapSize = 64;
            scanLength = 10;
            destroyTimeout = 30000;
        }

        ~Railway() {}

        void setWindowBitmap(int size)
        {
            if (size % 8 != 0 || size <= 0)
                return;
            this->windowBitmapSize = size;
        }

        int generate()
        {
            if (availableToGenerate.length == 0)
            {
                generateNewAvailableId();
            }
            int id = availableToGenerate.pop();
            return id;
        }

        RailwayChannel *get(int channelId, const Xi::String &key = Xi::String())
        {
            if (!channels.has(channelId))
            {
                RailwayChannel newChannel;
                newChannel.key = key;
                newChannel.bitmap.alloc(this->windowBitmapSize / 8);

                // Zero fill bitmap
                for (int i = 0; i < this->windowBitmapSize / 8; ++i)
                    newChannel.bitmap.push(0);

                newChannel.slidePos = 0;
                newChannel.lastSentNonce = 0;

                channels.put(channelId, newChannel);

                return channels.get(channelId);
            }
            else
            {
                RailwayChannel *ch = channels.get(channelId);
                if (key.length > 0)
                {
                    ch->key = key;
                }
                return ch;
            }
        }

        void onClose(int channelId)
        {
            // Hook for subclass or callback
        }

        void remove(int channelId)
        {
            if (channels.remove(channelId))
            {
                this->onClose(channelId);
            }
        }

        Xi::String build(const RailwayPacket &pkt)
        {
            RailwayChannel *channel = this->get(pkt.channel);

            const bool isSecure = (channel->key.length > 0);

            // --- Construct Header (1 byte) ---
            Xi::u8 header = 0;
            if (isSecure)
                header |= RAILWAY_SECURE;

            // --- Serialize Metadata (Map) ---
            Xi::String metaBytes;
            // Serialize map: Key(VarLong) + Val(Prefixed)
            for (auto &item : channel->meta)
            {
                metaBytes.pushVarLong((long long)item.key);
                metaBytes.pushVarLong(item.value.length);
                metaBytes += item.value;
            }

            Xi::String content;

            if (channel->updateMeta || metaBytes.constantTimeEquals(channel->lastReceivedMeta))
            {
                // std::cout << "Mismatch: " << metaBytes.toDeci() << " vs: " << channel->lastReceivedMeta.toDeci() << "\n";
                metaBytes.unshiftVarLong();
                content += metaBytes;
                header |= RAILWAY_HAS_META;
                channel->updateMeta = false;

                // std::cout << "Sending metadata..\n";
            }

            content += pkt.payload;

            // --- Write Channel ID (3 bytes) ---
            Xi::String channelIdBuf;
            channelIdBuf.push((pkt.channel >> 16) & 0xFF);
            channelIdBuf.push((pkt.channel >> 8) & 0xFF);
            channelIdBuf.push(pkt.channel & 0xFF);

            Xi::String ad;
            ad.push(header);
            ad += channelIdBuf;

            channel->lastSentTime = Time::millis();

            // --- Handle Encryption ---
            if (isSecure)
            {
                channel->lastSentNonce++;
                const Xi::u64 nonce = channel->lastSentNonce;

                Xi::String nonceBytes;
                nonceBytes.pushVarLong((long long)nonce);

                // Xi::aeadSeal returns [Ciphertext][Tag] (concatenated)
                // Railway wire format expects: AD + Nonce + Tag + Ciphertext
                int tagLen = 8;

                AEADOptions aeo = {content, ad, "", 8};

                aeadSeal(channel->key, nonce, aeo);

                return ad + nonceBytes + aeo.tag + aeo.text;
            }
            else
            {
                return ad + content;
            }
        }

        RailwayPacket parse(const Xi::String &buf)
        {
            if (buf.length < 4)
                return {0, Xi::String()};

            usz cursor = 0;
            const Xi::u8 *d = buf.data();

            const Xi::u8 header = d[cursor++];
            const u32 channelId = ((u32)d[cursor] << 16) | ((u32)d[cursor + 1] << 8) | (u32)d[cursor + 2];
            cursor += 3;

            // Maintain available pool
            long long availIdx = availableToGenerate.find(channelId);
            if (availIdx != -1)
            {
                availableToGenerate.remove((Xi::usz)availIdx);
                generateNewAvailableId();
            }

            // AD is the first 4 bytes
            Xi::String ad = buf.begin(0, 4);

            const bool isSecure = (header & RAILWAY_SECURE) != 0;
            const bool updatesMeta = (header & RAILWAY_HAS_META) != 0;

            bool exists = channels.has(channelId);
            if (isSecure && (!exists || channels.get(channelId)->key.length == 0))
            {
                return {0, Xi::String()}; // Cannot decrypt
            }

            RailwayChannel *channel = this->get(channelId);

            if (channel == nullptr)
            {
                // Log the error or drop the packet
                return {0, Xi::String()};
            }

            Xi::String decryptedData;

            if (isSecure)
            {
                auto nonceRes = buf.peekVarLong(cursor);
                if (nonceRes.error)
                    return {0, Xi::String()};

                const Xi::u64 nonce = (u64)nonceRes.value;
                cursor += nonceRes.bytes;

                int tagLen = 8;
                if (cursor + tagLen > buf.length)
                    return {0, Xi::String()};

                // --- Replay Protection ---
                if (nonce <= channel->slidePos)
                {
                    Xi::u64 diff = channel->slidePos - nonce;
                    if (diff >= (Xi::u64)this->windowBitmapSize)
                        return {0, Xi::String()}; // Too old
                    int byteIndex = (int)(diff / 8);
                    int bitIndex = (int)(diff % 8);
                    if ((channel->bitmap[byteIndex] >> bitIndex) & 1)
                        return {0, Xi::String()}; // Replay
                }

                Xi::String tag = buf.begin(cursor, cursor + 8);
                cursor += 8; // Move the cursor past the tag

                // 2. The ciphertext starts EXACTLY at the new cursor
                // Ensure no hidden offset or 'tagLen' constant is interfering
                Xi::String ciphertext = buf.begin(cursor, buf.length);

                // 3. Prepare the options
                AEADOptions aeo = {ciphertext, ad, tag, 8};

                // This should now produce a DataToAuth that matches the Seal trace
                bool success = aeadOpen(channel->key, nonce, aeo);

                // DEBUG
                // std::cout << "Verification: " << (success ? "PASS" : "FAIL") << std::endl;

                if (!success)
                { // Was "if (success)" which was wrong
                    return {0, Xi::String()};
                }

                // 3. Extract the now-decrypted text
                decryptedData = aeo.text;

                // --- Update Sliding Window ---
                if (nonce > channel->slidePos)
                {
                    Xi::u64 shift = nonce - channel->slidePos;
                    if (shift >= (Xi::u64)this->windowBitmapSize)
                    {
                        // Reset all to 0
                        for (Xi::usz i = 0; i < channel->bitmap.length; ++i)
                            channel->bitmap[i] = 0;
                    }
                    else
                    {
                        // Manual bit shift logic for Array<u8>
                        int byteShift = (int)(shift / 8);
                        int bitShift = (int)(shift % 8);

                        if (byteShift > 0)
                        {
                            int len = (int)channel->bitmap.length;
                            for (int i = len - 1; i >= byteShift; --i)
                            {
                                channel->bitmap[i] = channel->bitmap[i - byteShift];
                            }
                            for (int i = 0; i < byteShift; ++i)
                                channel->bitmap[i] = 0;
                        }

                        if (bitShift > 0)
                        {
                            int len = (int)channel->bitmap.length;
                            for (int i = len - 1; i > 0; --i)
                            {
                                channel->bitmap[i] = (channel->bitmap[i] << bitShift) | (channel->bitmap[i - 1] >> (8 - bitShift));
                            }
                            channel->bitmap[0] <<= bitShift;
                        }
                    }
                    channel->slidePos = nonce;
                }

                Xi::u64 newDiff = channel->slidePos - nonce;
                int byteIndex = (int)(newDiff / 8);
                int bitIndex = (int)(newDiff % 8);
                channel->bitmap[byteIndex] |= (1 << bitIndex);
            }
            else
            {
                decryptedData = buf.begin(cursor, buf.length);
            }

            channel->lastReceivedTime = Time::millis();

            usz dataCursor = 0;
            if (updatesMeta && decryptedData.length > 0)
            {
                // Read meta container (prefixed buffer)
                // Using manual peek/readVarLong logic combined with Xi::String
                auto metaLenRes = decryptedData.peekVarLong(0);

                if (!metaLenRes.error)
                {
                    usz metaTotalLen = (usz)metaLenRes.value;
                    usz metaHeaderBytes = metaLenRes.bytes;

                    if (metaHeaderBytes + metaTotalLen <= decryptedData.length)
                    {
                        Xi::String metaBlob = decryptedData.begin(metaHeaderBytes, metaHeaderBytes + metaTotalLen);

                        channel->lastReceivedMeta = metaBlob;

                        usz metaCursor = 0;

                        channel->meta.clear();
                        while (metaCursor < metaBlob.length)
                        {
                            auto keyRes = metaBlob.peekVarLong(metaCursor);
                            if (keyRes.error)
                                break;
                            metaCursor += keyRes.bytes;
                            u64 key = (u64)keyRes.value;

                            auto valLenRes = metaBlob.peekVarLong(metaCursor);
                            if (valLenRes.error)
                                break;
                            metaCursor += valLenRes.bytes;

                            usz vLen = (usz)valLenRes.value;
                            if (metaCursor + vLen > metaBlob.length)
                                break;

                            channel->meta.put(key, metaBlob.begin(metaCursor, metaCursor + vLen));
                            metaCursor += vLen;

                            // std::cout << "Put: " << key << " Value: " << metaBlob.begin(metaCursor, metaCursor + vLen) << "\n";
                        }
                        dataCursor += (metaHeaderBytes + metaTotalLen);
                    }
                }
            }

            Xi::String payload;
            if (dataCursor < decryptedData.length)
            {
                payload = decryptedData.begin(dataCursor, decryptedData.length);
            }

            RailwayPacket pkt;
            pkt.channel = channelId;
            pkt.payload = payload;
            return pkt;
        }

        void cleanOldRailwayChannels()
        {
            const Xi::u64 now = Time::millis();

            Xi::Array<int> keysToRemove;
            for (auto &entry : channels)
            {
                if (now - entry.value.lastReceivedTime > destroyTimeout)
                {
                    keysToRemove.push(entry.key);
                }
            }

            for (auto k : keysToRemove)
            {
                remove(k);
            }
        }

        void update()
        {
            cleanOldRailwayChannels();
        }
    };
}

#endif // RHO_RAILWAY_HPP