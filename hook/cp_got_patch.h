/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI: GOT redirection with a code-fingerprint gate (QNX 6.5 armle-v7).
 *
 * The stock iAP2 driver/transport are DT_SYMBOLIC (internal BL calls can't be interposed), so the hook
 * redirects their libc GOT slots instead, and only after verifying the surrounding code matches the bytes
 * this build was reverse-engineered against. If any fingerprint differs (a firmware we didn't analyse), the
 * hook installs nothing and the stack runs exactly as stock. Fail-closed by construction.
 */
#ifndef CP_GOT_PATCH_H
#define CP_GOT_PATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One 32-bit instruction we expect at (module_base + offset). */
typedef struct { uint32_t offset; uint32_t word; } cp_fingerprint_t;

/* Returns 1 if every fingerprint matches the bytes at base+offset, else 0. base is the module's load
 * address; reads go through the mapped image. Never writes. */
int cp_verify_fingerprints(const void* base, const cp_fingerprint_t* fps, size_t n);

/* Finds the GOT slot in [got, got+got_bytes) whose current value equals old_fn, and atomically swaps it to
 * new_fn after making the page writable. Returns the number of slots changed (0 or 1). old_fn is the real
 * libc function the module imported (resolved via dlsym from the module's handle). */
int cp_redirect_got(void* got, size_t got_bytes, const void* old_fn, const void* new_fn);

/* Redirect one specific GOT slot to new_fn (used with cp_hook_find_got_sym). Saves the previous value to
 * *old_out when non-NULL. Returns 1 on success. Works regardless of lazy/eager binding, because the slot is
 * located by symbol, not by its (possibly still-unresolved) value. */
int cp_redirect_slot(void** slot, const void* new_fn, void** old_out);

/* Locate a loaded module's GOT from its dynamic section (DT_PLTGOT + the relocation span). base is the
 * module load address. On success sets *got / *got_bytes and returns 1. Platform-specific (QNX dlinfo);
 * the host build returns 0 (tests drive cp_redirect_got directly). */
int cp_hook_find_got(const void* base, void** got, size_t* got_bytes);

/* Locate the .got.plt slot for a named imported symbol by walking DT_JMPREL (the PLT relocations) and
 * matching the symbol name via DT_SYMTAB/DT_STRTAB. On success sets *slot (address of the void* GOT entry)
 * and returns 1. This is binding-agnostic: it finds the right slot even before the symbol is lazily bound.
 * QNX-only; the host build returns 0. */
int cp_hook_find_got_sym(const void* base, const char* name, void*** slot);

#ifdef __cplusplus
}
#endif

#endif
