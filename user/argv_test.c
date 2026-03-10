#include "syscall.h"
#include "ulib.h"

void _start(int64_t argc, char **argv) {
    print("argv_test: argc=");
    print_num((uint64_t)argc);
    print("\n");
    for (int64_t i = 0; i < argc; i++) {
        print("argv_test: argv[");
        print_num((uint64_t)i);
        print("]=");
        print(argv[i]);
        print("\n");
    }
    print("argv_test: PASS\n");
    sys_exit(0);
}
