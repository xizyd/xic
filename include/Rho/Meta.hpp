#ifndef XI_RHO_META_HPP
#define XI_RHO_META_HPP 1

#include "../Xi/Primitives.hpp"

namespace Xi {
namespace Rho {

/**
 * Standardized Metadata Keys used across the Rho networking stack.
 * Refer to dev/meta.md for details.
 */
struct Meta {
  static const u8 Proofed = 0;
  static const u8 Hostname = 1;
  static const u8 NumericalHostname = 2;
  static const u8 NumericalHostnameTargetSource = 3;
  static const u8 Certs = 4;
  static const u8 PublicKey = 5;
  static const u8 Name = 6;
  static const u8 UUID = 7;
};

} // namespace Rho
} // namespace Xi

#endif // XI_RHO_META_HPP
