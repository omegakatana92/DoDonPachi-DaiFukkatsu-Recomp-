/* TinyCC configuration for the in-memory compiler embedded in the collection
 * (replaces the file "configure" writes).  The target (x86-64 ELF or PE) is
 * chosen by the build: TCC_TARGET_X86_64 and, on Windows, TCC_TARGET_PE. */
#define TCC_VERSION "0.9.28rc"
#define CONFIG_TCC_PREDEFS 1
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_SEMLOCK 0
#ifndef CONFIG_TCCDIR
#define CONFIG_TCCDIR "."
#endif
