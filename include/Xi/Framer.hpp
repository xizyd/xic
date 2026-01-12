// include/Xi/Framer.hpp

#ifndef XI_FRAMER
#define XI_FRAMER

#include "String.hpp"

// Framer.hpp
namespace Xi
{
    class Framer
    {
    private:
        long long maxPacketSize;
        long long expectedLength;
        String buffer;
        Array<String> packets;

    public:
        Framer(long long maxBytes) : maxPacketSize(maxBytes), expectedLength(0) {}

        /**
         * @brief Parses incoming data stream and extracts complete packets.
         * Ensures maximum security by validating lengths and preventing overflows.
         */
        void parse(const String &data)
        {
            // 1. Append incoming data to our internal stream buffer
            buffer.concat(data);

            while (buffer.length > 0)
            {
                // Step A: Determine the packet length if it is not yet known
                if (expectedLength == 0)
                {
                    // Non-destructively peek at the VarLong header
                    auto res = buffer.peekVarLong();

                    if (res.error)
                    {
                        // The VarLong is incomplete; we must await further data
                        return;
                    }

                    // Security: Guard against invalid or oversized packet declarations
                    if (res.value <= 0 || res.value > maxPacketSize)
                    {
                        buffer.clear(); // Clear buffer to recover safe state
                        expectedLength = 0;
                        return;
                    }

                    expectedLength = res.value;

                    // Header is valid; consume the bytes used by the VarLong
                    for (int i = 0; i < res.bytes; i++)
                    {
                        buffer.shift();
                    }
                }

                // Step B: Extract the packet body if the full length has arrived
                if (expectedLength > 0)
                {
                    if (buffer.length >= (usz)expectedLength)
                    {
                        // Efficiently slice the packet using begin()
                        String packet = buffer.begin(0, (usz)expectedLength);
                        packets.push(Xi::Move(packet));

                        // Remove the extracted bytes from the front of the buffer
                        for (long long i = 0; i < expectedLength; i++)
                        {
                            buffer.shift();
                        }

                        // Reset state for the next packet in the stream
                        expectedLength = 0;
                    }
                    else
                    {
                        // Packet body is incomplete; wait for more data
                        return;
                    }
                }
            }
        }

        bool available() const { return packets.length > 0; }

        String read()
        {
            if (packets.length == 0)
                return String();
            // Retrieve and remove the oldest packet (Queue logic)
            return packets.shift();
        }

        /**
         * @brief Static utility to wrap data into a framed packet.
         */
        static String build(const String &data)
        {
            String frame;
            frame.pushVarLong((long long)data.length);
            frame.concat(data);
            return frame;
        }
    };
}

#endif