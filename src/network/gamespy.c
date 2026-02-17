#include "openbc/gamespy.h"
#include <stdio.h>
#include <string.h>

bool bc_gamespy_is_query(const u8 *data, int len)
{
    return len > 0 && data[0] == '\\';
}

bool bc_gamespy_is_secure(const u8 *data, int len)
{
    /* \secure\XXXXXX  -- minimum 9 bytes */
    return len >= 9 && memcmp(data, "\\secure\\", 8) == 0;
}

int bc_gamespy_extract_secure(const u8 *data, int len,
                               char *out, int out_size)
{
    if (!bc_gamespy_is_secure(data, len)) return 0;

    /* Skip "\secure\" prefix (8 bytes) */
    const char *challenge = (const char *)data + 8;
    int clen = 0;
    while (8 + clen < len && challenge[clen] != '\\' && challenge[clen] != '\0')
        clen++;

    if (clen == 0 || clen >= out_size) return 0;
    memcpy(out, challenge, (size_t)clen);
    out[clen] = '\0';
    return clen;
}

/* Extract a GameSpy key's value from a query string.
 * Query format: \key1\value1\key2\value2\...
 * Returns pointer into query data, sets *value_len. NULL if not found. */
static const char *extract_gs_value(const u8 *query, int query_len,
                                    const char *key, int *value_len)
{
    const char *q = (const char *)query;
    int klen = (int)strlen(key);

    for (int i = 0; i < query_len; i++) {
        if (q[i] != '\\') continue;

        /* Check if key matches starting at i+1 */
        int start = i + 1;
        if (start + klen >= query_len) continue;
        if (memcmp(q + start, key, (size_t)klen) != 0) continue;
        if (q[start + klen] != '\\') continue;

        /* Found key, extract value */
        int vstart = start + klen + 1;
        int vend = vstart;
        while (vend < query_len && q[vend] != '\\')
            vend++;
        *value_len = vend - vstart;
        return q + vstart;
    }
    return NULL;
}

int bc_gamespy_build_response(u8 *out, int out_size,
                              const bc_server_info_t *info,
                              const u8 *query, int query_len)
{
    /* Build response matching stock BC QR1 format exactly.
     * Verified from stock dedi packet trace (267 bytes, 2026-02-16).
     *
     * Stock field order (info → basic → rules → players → final → queryid):
     *   \gamename\bcommander\gamever\60\location\0
     *   \hostname\<name>\missionscript\<script>\mapname\<display>
     *   \numplayers\N\maxplayers\N\gamemode\<mode>
     *   \timelimit\N\fraglimit\N\system\<sys>\password\0
     *   \player_0\<name>  (for each connected player)
     *   \final\
     *   \queryid\N.M      (appended by qr_flush_send, no trailing \)
     *
     * Key facts from trace:
     *   - gamename/gamever/location come FIRST (before hostname)
     *   - gamever is "60" (numeric), NOT "1.1" (string version is only in client auth)
     *   - \final\ comes BEFORE \queryid\ (stock QR SDK order)
     *   - No trailing backslash after queryid
     */
    int written = snprintf((char *)out, (size_t)out_size,
        /* Info callback (comes first in stock BC) */
        "\\gamename\\bcommander"
        "\\gamever\\60"
        "\\location\\0"
        /* Basic callback */
        "\\hostname\\%s"
        "\\missionscript\\%s"
        "\\mapname\\%s"
        "\\numplayers\\%d"
        "\\maxplayers\\%d"
        "\\gamemode\\%s"
        /* Rules callback */
        "\\timelimit\\%d"
        "\\fraglimit\\%d"
        "\\system\\%s"
        "\\password\\0",
        info->hostname,
        info->missionscript,
        info->mapname,
        info->numplayers,
        info->maxplayers,
        info->gamemode,
        info->timelimit,
        info->fraglimit,
        info->system);

    if (written < 0 || written >= out_size) return -1;

    /* Append \final\ THEN \queryid\ (stock QR SDK order).
     * qr_send_final (0x006ac950) appends \final\, then
     * qr_flush_send (0x006ac550) appends \queryid\N.M and sends.
     * No trailing backslash after queryid. */
    int added = snprintf((char *)out + written,
                          (size_t)(out_size - written), "\\final\\");
    if (added < 0 || written + added >= out_size) return -1;
    written += added;

    /* Echo queryid from query if present, otherwise default to 1.1 */
    const char *qid = NULL;
    int qid_len = 0;
    if (query && query_len > 0)
        qid = extract_gs_value(query, query_len, "queryid", &qid_len);

    if (qid && qid_len > 0 && qid_len < 32) {
        added = snprintf((char *)out + written,
                          (size_t)(out_size - written),
                          "\\queryid\\%.*s", qid_len, qid);
    } else {
        added = snprintf((char *)out + written,
                          (size_t)(out_size - written),
                          "\\queryid\\1.1");
    }
    if (added < 0 || written + added >= out_size) return -1;
    written += added;

    return written;
}

/* --- GameSpy Master Server Algorithm (gsmsalg) --- */

static u8 gsvalfunc(int reg)
{
    if (reg < 26) return (u8)(reg + 'A');
    if (reg < 52) return (u8)(reg + 'G');  /* 'a' - 26 + 'G' = 'a' */
    if (reg < 62) return (u8)(reg - 4);    /* '0' = 48, 52 - 4 = 48 */
    if (reg == 62) return '+';
    if (reg == 63) return '/';
    return 0;
}

void bc_gsmsalg(char *dst, const char *challenge,
                const char *secret_key, int enctype)
{
    u8 enctmp[256];
    u8 tmp[66];
    int size, keysz;
    u8 x, y, z, a, b;
    char *p;

    size = (int)strlen(challenge);
    if (size < 1 || size > 65) {
        dst[0] = '\0';
        return;
    }
    keysz = (int)strlen(secret_key);

    /* RC4-style key scheduling */
    for (int i = 0; i < 256; i++)
        enctmp[i] = (u8)i;

    a = 0;
    for (int i = 0; i < 256; i++) {
        a = (u8)(a + enctmp[i] + (u8)secret_key[i % keysz]);
        x = enctmp[a];
        enctmp[a] = enctmp[i];
        enctmp[i] = x;
    }

    /* Stream cipher */
    a = 0;
    b = 0;
    int i;
    for (i = 0; challenge[i]; i++) {
        a = (u8)(a + (u8)challenge[i] + 1);
        x = enctmp[a];
        b = (u8)(b + x);
        y = enctmp[b];
        enctmp[b] = x;
        enctmp[a] = y;
        tmp[i] = (u8)((u8)challenge[i] ^ enctmp[(u8)(x + y)]);
    }
    /* Pad to multiple of 3 */
    for (size = i; size % 3; size++)
        tmp[size] = 0;

    /* Enctype 1 or 2 transforms (BC uses enctype 0, so this is rarely needed) */
    (void)enctype;

    /* Base64-style encoding */
    p = dst;
    for (i = 0; i < size; i += 3) {
        x = tmp[i];
        y = tmp[i + 1];
        z = tmp[i + 2];
        *p++ = (char)gsvalfunc(x >> 2);
        *p++ = (char)gsvalfunc(((x & 3) << 4) | (y >> 4));
        *p++ = (char)gsvalfunc(((y & 15) << 2) | (z >> 6));
        *p++ = (char)gsvalfunc(z & 63);
    }
    *p = '\0';
}

int bc_gamespy_build_validate(u8 *out, int out_size,
                               const char *challenge)
{
    char validate[89];
    bc_gsmsalg(validate, challenge, BC_GAMESPY_SECRET_KEY, 0);

    /* Stock QR SDK order: validate → final → queryid (no trailing \) */
    int written = snprintf((char *)out, (size_t)out_size,
        "\\gamename\\bcommander"
        "\\gamever\\60"
        "\\location\\0"
        "\\validate\\%s"
        "\\final\\"
        "\\queryid\\1.1",
        validate);

    if (written < 0 || written >= out_size) return -1;
    return written;
}
