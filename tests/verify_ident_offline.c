/* Offline verification: run cp_ident_patch on a captured real Identify and dump the result. */
#include "../hook/ident_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s in.bin out.bin compid_hex\n", argv[0]); return 2; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("open in"); return 1; }
    static unsigned char in[4096];
    size_t n = fread(in, 1, sizeof(in), f);
    fclose(f);
    unsigned cid = (unsigned)strtoul(argv[3], 0, 16);
    static unsigned char out[4096];
    int r = cp_ident_patch(in, n, out, sizeof(out), (unsigned short)cid);
    printf("cp_ident_patch(%u bytes, compid=0x%04X) -> %d\n", (unsigned)n, cid, r);
    if (r > 0) {
        printf("output link packet valid (sync+len+checksums): %s\n",
               cp_link_is_valid(out, (size_t)r) ? "YES" : "NO -- BROKEN");
        FILE* o = fopen(argv[2], "wb");
        fwrite(out, 1, (size_t)r, o);
        fclose(o);
        printf("wrote %d bytes to %s\n", r, argv[2]);
    } else {
        printf("patch returned %d (0=passthrough, -1=error)\n", r);
    }
    return 0;
}
