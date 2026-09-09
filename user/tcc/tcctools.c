/* HobbyOS: tcctools.c's real implementation (tcc -ar static-lib archiver,
 * -impdef Windows import-def generator, -m32/-m64 cross-compile helper,
 * -MD/-MF makedeps) is not needed for basic `.c -> .elf` compilation, which
 * is all early HobbyOS self-hosting milestones require — stubbed out to
 * avoid porting its large FILE*-heavy implementation. tcc_tool_impdef is
 * gated behind `#ifdef TCC_TARGET_PE` at its only call site (tcc.c) and
 * never compiled for this target, so it isn't stubbed here. */

ST_FUNC int tcc_tool_ar(TCCState *s1, int argc, char **argv)
{
    (void)s1; (void)argc; (void)argv;
    tcc_error("tcc -ar is not supported on HobbyOS");
    return 1;
}

ST_FUNC void tcc_tool_cross(TCCState *s, char **argv, int option)
{
    (void)s; (void)argv; (void)option;
    tcc_error("-m32/-m64 cross-compiling is not supported on HobbyOS");
}

ST_FUNC void gen_makedeps(TCCState *s, const char *target, const char *filename)
{
    (void)s; (void)target; (void)filename;
    tcc_error("-MD/-MF dependency-file generation is not supported on HobbyOS");
}
