/* HobbyOS compat shim for TCC: assert.h.
 * __builtin_trap() is freestanding-safe (raises an illegal-instruction
 * trap) — no libc, no message formatting dependency. assert.h is
 * re-includable multiple times with different NDEBUG state by design
 * (matching real assert.h), so no include guard here. */
#undef assert

#ifdef NDEBUG
# define assert(cond) ((void)0)
#else
# define assert(cond) ((cond) ? (void)0 : __builtin_trap())
#endif
