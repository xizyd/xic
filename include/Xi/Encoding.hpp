// src/Xi/Encoding.hpp

#ifndef XI_ENCODING_HPP
#define XI_ENCODING_HPP

#include "Array.hpp"
#include "String.hpp"

namespace Xi {
    // -------------------------------------------------------------------------
    // Structures
    // -------------------------------------------------------------------------

    struct ReadVarIntResult {
        int value;
        int bytes;
        bool error;
    };

    struct ReadVarLongResult {
        long long value;
        int bytes;
        bool error;
    };

    struct ReadUVarIntBEResult {
        unsigned int value;
        int bytes;
        bool error;
    };

    struct ReadUVarLongBEResult {
        unsigned long long value;
        int bytes;
        bool error;
    };

    struct ReadPrefixResult {
        String value;
        int bytes;
        bool error;
    };

    // -------------------------------------------------------------------------
    // Encoding / Decoding Logic
    // -------------------------------------------------------------------------

    struct Encoding {
        // --- VarInt / VarLong ---

        static int getVarIntLength(int value) {
            unsigned int u_value = value;
            if (value < 0) return 10;
            if (u_value < 128) return 1;
            if (u_value < 16384) return 2;
            if (u_value < 2097152) return 3;
            if (u_value < 268435456) return 4;
            return 5;
        }

        static int getVarLongLength(long long value) {
            int len = 1;
            unsigned long long val = value;
            while (val >= 128ULL) {
                val >>= 7;
                len++;
            }
            return len;
        }

        static void writeVarInt(String& buffer, int value) {
            unsigned int val = value;
            do {
                u8 temp = val & 0x7f;
                val >>= 7;
                if (val != 0) temp |= 0x80;
                buffer.push(temp);
            } while (val != 0);
        }

        static void writeVarLong(String& buffer, long long value) {
            unsigned long long val = value;
            do {
                u8 temp = val & 0x7fULL;
                val >>= 7;
                if (val != 0ULL) temp |= 0x80;
                buffer.push(temp);
            } while (val != 0ULL);
        }

        static ReadVarIntResult readVarInt(const String& buffer, usz offset) {
            int result = 0;
            int shift = 0;
            usz cursor = offset;
            u8 byte;
            do {
                if (cursor >= buffer.length) return {0, 0, true};
                byte = buffer[cursor++];
                result |= (byte & 0x7f) << shift;
                shift += 7;
                if (shift > 35) return {0, 0, true};
            } while (byte & 0x80);
            return {result, (int)(cursor - offset), false};
        }

        static ReadVarLongResult readVarLong(const String& buffer, usz offset) {
            unsigned long long result = 0ULL;
            long long shift = 0LL;
            usz cursor = offset;
            u8 byte;
            do {
                if (cursor >= buffer.length) return {0LL, 0, true};
                byte = buffer[cursor++];
                result |= (unsigned long long)(byte & 0x7f) << shift;
                shift += 7LL;
                if (shift > 70LL) return {0LL, 0, true};
            } while (byte & 0x80);
            return {(long long)result, (int)(cursor - offset), false};
        }

        // --- Big Endian Unsigned VarInt (for specific protocols) ---

        static int writeUVarIntBE(String& buffer, unsigned int value) {
            if (value == 0) {
                buffer.push(0);
                return 1;
            }

            // Using stack array to reverse
            u8 temp[10];
            int idx = 0;
            unsigned int val = value;

            while (val > 0) {
                temp[idx++] = (val & 0x7f);
                val >>= 7;
            }

            // Write reversed with continuation bits
            for (int i = idx - 1; i >= 0; --i) {
                u8 byte_out = temp[i];
                if (i > 0) byte_out |= 0x80;
                buffer.push(byte_out);
            }
            return idx;
        }

        static ReadUVarIntBEResult readUVarIntBE(const String& buffer, usz offset) {
            unsigned int value = 0;
            usz cursor = offset;
            u8 byte_in;
            int count = 0;
            do {
                if (cursor >= buffer.length) return {0, 0, true};
                byte_in = buffer[cursor++];

                if ((value & 0xFE000000) != 0) return {0, 0, true}; // Overflow check

                value = (value << 7) | (byte_in & 0x7F);
                count++;
                if (count > 5) return {0, 0, true};

            } while ((byte_in & 0x80) != 0);

            return {value, (int)(cursor - offset), false};
        }

        // --- Utilities ---

        static String prefix(const String& packet, bool includeLength = true) {
            if (!includeLength) return packet; // Copy
            String res;
            
            // Write len
            writeVarInt(res, (int)packet.length);
            // Write data
            res.set(packet, res.length);
            
            return res;
        }

    };

    // -------------------------------------------------------------------------
    // Framer: Packet Parsing
    // -------------------------------------------------------------------------
    class Framer {
    private:
        long long maxPacketu8s;
        long long expectedLength;
        String buffer;
        
        // Use Array as Queue (push/shift)
        Array<String> packets;

    public:
        Framer(long long maxu8s) : maxPacketu8s(maxu8s), expectedLength(0) {}

        void parse(const String& data) {
            // Append incoming data
            buffer.set(data, 0);

            while (buffer.length > 0) {
                // 1. Read Length if not yet known
                if (expectedLength == 0) {
                    auto res = Encoding::readVarLong(buffer, 0);
                    if (res.error) {
                        // Not enough bytes for VarLong yet
                        break; 
                    }

                    long long len = res.value;
                    if (len > maxPacketu8s) {
                         // Reset or throw. Here we just reset to recover safe state.
                         buffer.destroy(); 
                         expectedLength = 0;
                         return;
                    }

                    expectedLength = len;
                    
                    // Remove the VarLong bytes from front
                    // Xi::Array::shift() removes 1 byte at a time efficiently (O(1) logic)
                    for(int i=0; i<res.bytes; ++i) buffer.shift();
                }

                // 2. Read Packet if we have enough data
                if (expectedLength > 0) {
                    if (buffer.length >= (usz)expectedLength) {
                        // Extract packet
                        String packet;
                        packet.alloc((usz)expectedLength);
                        
                        // Shift bytes out of buffer into packet
                        for(long long i=0; i<expectedLength; ++i) {
                            packet.push(buffer.shift());
                        }

                        packets.push(Xi::Move(packet));
                        expectedLength = 0;
                    } else {
                        // Wait for more data
                        break;
                    }
                }
            }
        }

        bool available() const {
            return packets.length > 0;
        }

        String read() {
            if (packets.length == 0) return String();
            // Queue pop (shift)
            return packets.shift(); 
        }

        static String build(const String& data) {
            return Encoding::prefix(data, true);
        }
    };

}

#endif // XI_ENCODING_HPP