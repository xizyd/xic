#ifndef XI_STREAM_HPP
#define XI_STREAM_HPP 1

#include "Array.hpp"
#include "String.hpp"

namespace Xi {

/**
 * A unidirectional stream of data, represented as an Array of Strings.
 */
class Stream : public Array<String> {
public:
  Stream() {}
  virtual ~Stream() = default;
};

/**
 * A bidirectional stream that encapsulates a forward (this object) and an
 * inverse stream.
 */
class DuplexStream : public Stream {
public:
  Stream inverse;

  DuplexStream() {}

  /**
   * Constructs a DuplexStream from two existing Streams.
   */
  DuplexStream(const Stream &a, const Stream &b) : Stream(a), inverse(b) {}

  /**
   * Appends data to the inverse stream.
   */
  void ipush(const String &data) { inverse.push(data); }

  /**
   * Prepends data to the inverse stream.
   */
  void iunshift(const String &data) { inverse.unshift(data); }

  /**
   * Returns the size of the inverse stream.
   */
  usz isize() const { return inverse.size(); }

  /**
   * Removes and returns the last element of the inverse stream.
   */
  String ipop() { return inverse.pop(); }

  /**
   * Removes and returns the first element of the inverse stream.
   */
  String ishift() { return inverse.shift(); }
};

} // namespace Xi

#endif // XI_STREAM_HPP
