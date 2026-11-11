/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * CarPlay -> BMW HUD/KOMBI: locate a loaded module's GOT (QNX 6.5). Separated from cp_got_patch.c so the
 * host unit tests link cp_got_patch.c without a QNX runtime dependency.
 *
 * QNX 6.5 has no dl_iterate_phdr / dlinfo, so we parse the module's own ELF image, which the loader maps at
 * its base: Elf32_Ehdr at base -> program headers -> PT_DYNAMIC -> DT_PLTGOT (+ DT_PLTRELSZ to bound the
 * GOT slots). The GOT address and relocation of DT_PLTGOT are confirmed on-car (phase P4); the hook is
 * fail-closed, so a wrong/absent GOT just means the recv seam is not installed and the stack stays stock.
 */
#include "cp_got_patch.h"

#if defined(__QNX__)
#include <sys/elf.h>
#include <sys/elf_dyn.h>
#include <stddef.h>
#include <string.h>

#ifndef DT_PLTRELSZ
#define DT_PLTRELSZ 2
#endif
#ifndef DT_STRTAB
#define DT_STRTAB 5
#endif
#ifndef DT_SYMTAB
#define DT_SYMTAB 6
#endif
#ifndef DT_JMPREL
#define DT_JMPREL 23
#endif
#ifndef ELF32_R_SYM
#define ELF32_R_SYM(i) ((i) >> 8)
#endif

static const Elf32_Dyn* cp_find_dynamic(const unsigned char* b) {
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)b;
    const Elf32_Phdr* ph;
    unsigned i;
    if (!(b[0] == 0x7f && b[1] == 'E' && b[2] == 'L' && b[3] == 'F')) return 0;
    ph = (const Elf32_Phdr*)(b + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; ++i)
        if (ph[i].p_type == PT_DYNAMIC)
            return (const Elf32_Dyn*)(b + ph[i].p_vaddr);   /* p_vaddr relative for a .so */
    return 0;
}

int cp_hook_find_got_sym(const void* base, const char* name, void*** slot) {
    const unsigned char* b = (const unsigned char*)base;
    const Elf32_Dyn* dyn = cp_find_dynamic(b);
    Elf32_Addr jmprel = 0, symtab = 0, strtab = 0;
    Elf32_Word pltrelsz = 0;
    const Elf32_Dyn* d;
    const Elf32_Rel* rel;
    const Elf32_Sym* syms;
    const char* strs;
    unsigned nrel, k;
    uintptr_t bb = (uintptr_t)base;

    if (!dyn || !slot) return 0;
    for (d = dyn; d->d_tag != DT_NULL; ++d) {
        if (d->d_tag == DT_JMPREL)   jmprel   = d->d_un.d_ptr;
        if (d->d_tag == DT_PLTRELSZ) pltrelsz = d->d_un.d_val;
        if (d->d_tag == DT_SYMTAB)   symtab   = d->d_un.d_ptr;
        if (d->d_tag == DT_STRTAB)   strtab   = d->d_un.d_ptr;
    }
    if (!jmprel || !symtab || !strtab || !pltrelsz) return 0;
    if (jmprel < (Elf32_Addr)bb) jmprel += (Elf32_Addr)bb;   /* normalise relative -> absolute */
    if (symtab < (Elf32_Addr)bb) symtab += (Elf32_Addr)bb;
    if (strtab < (Elf32_Addr)bb) strtab += (Elf32_Addr)bb;

    rel  = (const Elf32_Rel*)(uintptr_t)jmprel;
    syms = (const Elf32_Sym*)(uintptr_t)symtab;
    strs = (const char*)(uintptr_t)strtab;
    nrel = (unsigned)(pltrelsz / sizeof(Elf32_Rel));
    for (k = 0; k < nrel; ++k) {
        unsigned si = ELF32_R_SYM(rel[k].r_info);
        const char* nm = strs + syms[si].st_name;
        if (strcmp(nm, name) == 0) {
            Elf32_Addr off = rel[k].r_offset;
            if (off < (Elf32_Addr)bb) off += (Elf32_Addr)bb;
            *slot = (void**)(uintptr_t)off;
            return 1;
        }
    }
    return 0;
}

int cp_hook_find_got(const void* base, void** got, size_t* got_bytes) {
    const unsigned char* b = (const unsigned char*)base;
    const Elf32_Dyn* dyn = cp_find_dynamic(b);
    if (!dyn) return 0;
    {
        Elf32_Addr pltgot = 0;
        Elf32_Word pltrelsz = 0;
        const Elf32_Dyn* d;
        for (d = dyn; d->d_tag != DT_NULL; ++d) {
            if (d->d_tag == DT_PLTGOT)   pltgot = d->d_un.d_ptr;
            if (d->d_tag == DT_PLTRELSZ) pltrelsz = d->d_un.d_val;
        }
        if (!pltgot) return 0;
        /* DT_PLTGOT may be stored relative (common on a QNX dll) or already absolute; normalise to absolute.
         * cp_redirect_got matches slots by value, so a small over-estimate of the span is safe. */
        if (pltgot < (Elf32_Addr)(uintptr_t)base) pltgot += (Elf32_Addr)(uintptr_t)base;
        *got = (void*)(uintptr_t)pltgot;
        *got_bytes = (size_t)(pltrelsz ? (pltrelsz / sizeof(Elf32_Rel)) * sizeof(void*) + 16 * sizeof(void*)
                                       : 512 * sizeof(void*));
        return 1;
    }
}

#else   /* host build: tests exercise cp_redirect_got directly */

int cp_hook_find_got(const void* base, void** got, size_t* got_bytes) {
    (void)base; (void)got; (void)got_bytes;
    return 0;
}

int cp_hook_find_got_sym(const void* base, const char* name, void*** slot) {
    (void)base; (void)name; (void)slot;
    return 0;
}

#endif
