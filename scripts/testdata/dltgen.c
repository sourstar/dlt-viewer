/* Generate a synthetic DLT log file for benchmarking and testing.
 *
 * Produces DLTv1 storage-header records containing verbose log messages with a
 * spread of application ids, context ids, log levels and payload sizes, so that
 * filtering and searching have something realistic to chew on.
 *
 *   cc -O2 -o dltgen dltgen.c
 *   ./dltgen out.dlt 500          # 500 MiB
 *
 * SPDX-License-Identifier: MPL-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define DLT_HTYP_UEH  0x01
#define DLT_HTYP_WEID 0x04
#define DLT_HTYP_WTMS 0x10
#define DLT_HTYP_VERS1 0x20

#define DLT_MSIN_VERB 0x01
#define DLT_TYPE_LOG  0x00

#define DLT_TYPE_INFO_STRG 0x00000200u
#define DLT_SCOD_ASCII     0x00000000u

static void put_u16_be(unsigned char *p, uint16_t v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void put_u32_be(unsigned char *p, uint32_t v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static void put_u16_le(unsigned char *p, uint16_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put_u32_le(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

static const char *APIDS[] = { "SYS", "APP1", "APP2", "NET", "DIAG", "AUDI", "VID", "GPS" };
static const char *CTIDS[] = { "CTX1", "CTX2", "MAIN", "IO",  "TICK", "ERR",  "DBG", "TRC" };
#define NAPID (int)(sizeof(APIDS)/sizeof(APIDS[0]))
#define NCTID (int)(sizeof(CTIDS)/sizeof(CTIDS[0]))

/* log levels: 1 fatal .. 6 verbose; weight towards info/debug like real traffic */
static const int LEVELS[] = { 4, 4, 4, 4, 5, 5, 5, 3, 3, 2, 6, 4 };
#define NLEVEL (int)(sizeof(LEVELS)/sizeof(LEVELS[0]))

static void set_id(char *dst, const char *src)
{
    memset(dst, 0, 4);
    size_t n = strlen(src);
    memcpy(dst, src, n > 4 ? 4 : n);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <output.dlt> <size-in-MiB>\n", argv[0]);
        return 2;
    }
    const char *outname = argv[1];
    long long target = atoll(argv[2]) * 1024LL * 1024LL;
    if (target <= 0) { fprintf(stderr, "size must be positive\n"); return 2; }

    FILE *f = fopen(outname, "wb");
    if (!f) { perror("fopen"); return 1; }

    /* one big stdio buffer: this writes gigabytes */
    static char iobuf[1 << 20];
    setvbuf(f, iobuf, _IOFBF, sizeof iobuf);

    unsigned char msg[1024];
    char payload[512];
    long long written = 0;
    unsigned long long n = 0;
    uint32_t seconds = 1700000000u;
    uint32_t tmsp = 0;
    unsigned char mcnt = 0;

    while (written < target) {
        const char *apid = APIDS[n % NAPID];
        const char *ctid = CTIDS[(n / NAPID) % NCTID];
        int level = LEVELS[n % NLEVEL];

        /* every 101st message carries a distinctive token to search/filter for */
        int len;
        if (n % 101 == 0)
            len = snprintf(payload, sizeof payload,
                           "NEEDLE marker event seq=%llu ctx=%s state=active", n, ctid);
        else
            len = snprintf(payload, sizeof payload,
                           "seq=%llu %s/%s value=%lu status=ok detail=%s", n, apid, ctid,
                           (unsigned long)(n * 2654435761u % 100000u),
                           (n % 7 == 0) ? "recalibrating subsystem" : "nominal");
        if (len < 0) len = 0;
        if (len > (int)sizeof payload - 1) len = (int)sizeof payload - 1;
        int strlen_with_nul = len + 1;

        int payload_size = 4 /* type info */ + 2 /* length */ + strlen_with_nul;
        int dlt_len = 4 /* standard */ + 4 /* ecu */ + 4 /* tmsp */ + 10 /* extended */ + payload_size;

        unsigned char *p = msg;

        /* storage header */
        memcpy(p, "DLT\x01", 4);                        p += 4;
        put_u32_le(p, seconds);                         p += 4;
        put_u32_le(p, (uint32_t)((n * 977) % 1000000)); p += 4;
        set_id((char *)p, "ECU1");                      p += 4;

        /* standard header */
        *p++ = DLT_HTYP_VERS1 | DLT_HTYP_UEH | DLT_HTYP_WEID | DLT_HTYP_WTMS;
        *p++ = mcnt++;
        put_u16_be(p, (uint16_t)dlt_len);               p += 2;

        /* standard header extra */
        set_id((char *)p, "ECU1");                      p += 4;
        put_u32_be(p, tmsp);                            p += 4;

        /* extended header */
        *p++ = (unsigned char)(DLT_MSIN_VERB | (DLT_TYPE_LOG << 1) | (level << 4));
        *p++ = 1; /* one argument */
        set_id((char *)p, apid);                        p += 4;
        set_id((char *)p, ctid);                        p += 4;

        /* payload: one ASCII string argument */
        put_u32_le(p, DLT_TYPE_INFO_STRG | DLT_SCOD_ASCII); p += 4;
        put_u16_le(p, (uint16_t)strlen_with_nul);            p += 2;
        memcpy(p, payload, len); p += len;
        *p++ = '\0';

        size_t total = (size_t)(p - msg);
        if (fwrite(msg, 1, total, f) != total) { perror("fwrite"); fclose(f); return 1; }
        written += (long long)total;

        n++;
        tmsp += 3 + (n % 11);
        if (n % 5000 == 0) seconds++;
    }

    fclose(f);
    fprintf(stderr, "wrote %llu messages, %lld bytes to %s\n", n, written, outname);
    return 0;
}
