/*
 * Stack-protection (/GS) support for the kernel build.
 *
 * The WDK libraries (wdfdriverentry.lib, vhfkm.lib, ntoskrnl.lib) are compiled with /GS and
 * reference these symbols, but this WDK install does not provide them. The driver is built with
 * /GS-, so these exist only to satisfy the linker. Each boot gets a random cookie, and a
 * mismatch fails fast the way the MSVC runtime does.
 */
#include <ntddk.h>
#include <intrin.h>

EXTERN_C uintptr_t __security_cookie = 0x2B992DDFA232;

EXTERN_C VOID __cdecl __security_init_cookie(VOID) {
  LARGE_INTEGER counter = KeQueryPerformanceCounter(NULL);
  uintptr_t cookie = (uintptr_t) counter.QuadPart;

  // Keep the cookie non-zero and different from the static fallback.
  if (cookie == 0 || cookie == 0x2B992DDFA232) {
    cookie = 0x2B992DDFA232 ^ 0x5A5A5A5A5A5A5A5A;
  }
  __security_cookie = cookie;
}

EXTERN_C VOID __fastcall __security_check_cookie(uintptr_t cookie) {
  if (cookie != __security_cookie) {
    __fastfail(FAST_FAIL_STACK_COOKIE_CHECK_FAILURE);
  }
}
