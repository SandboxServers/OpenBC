#include "openbc/gamespy.h"
#include <stdio.h>
#include <string.h>

bool bc_gamespy_is_query(const u8 *data, int len)
{
    return len > 0 && data[0] == '\\';
}

int bc_gamespy_build_response(u8 *out, int out_size,
                              const bc_server_info_t *info)
{
    int written = snprintf((char *)out, (size_t)out_size,
        "\\gamename\\bcommander"
        "\\hostname\\%s"
        "\\numplayers\\%d"
        "\\maxplayers\\%d"
        "\\mapname\\%s"
        "\\gametype\\%s"
        "\\hostport\\%u"
        "\\gamemode\\openplaying"
        "\\gamever\\1.1"
        "\\final\\",
        info->hostname,
        info->numplayers,
        info->maxplayers,
        info->mapname,
        info->gametype,
        info->hostport);

    if (written < 0 || written >= out_size) return -1;
    return written;
}
