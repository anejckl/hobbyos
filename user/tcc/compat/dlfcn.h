/* HobbyOS compat shim for TCC: dlfcn.h.
 * TCC uses this to link against / resolve symbols in real *host* .so files
 * during native linking and its -run JIT mode — neither applies here
 * (HobbyOS has its own ld.so, incompatible with this Linux dlopen scheme,
 * and -run is out of scope per the plan). Always-fail stubs. */
#ifndef HOBBYOS_TCC_DLFCN_H
#define HOBBYOS_TCC_DLFCN_H

#define RTLD_LAZY   0x001
#define RTLD_NOW    0x002
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void *)0)

void *dlopen(const char *filename, int flag);
int dlclose(void *handle);
const char *dlerror(void);
void *dlsym(void *handle, const char *symbol);

#endif
