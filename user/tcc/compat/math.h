/* HobbyOS compat shim for TCC: math.h.
 * The only math.h symbol TCC used (ldexp, in the hex-float literal parser)
 * was removed along with float/double literal support — nothing needs
 * anything from this header anymore, it just needs to resolve. */
#ifndef HOBBYOS_TCC_MATH_H
#define HOBBYOS_TCC_MATH_H
#endif
