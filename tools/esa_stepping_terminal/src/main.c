#include "protocol.h"
#include "repl.h"
#include <string.h>

int main(void)
{
    EST_ReplContext_t ctx;

    memset(&ctx, 0, sizeof(ctx));
    strncpy(ctx.socket_path, EST_DEFAULT_SOCKET_PATH, sizeof(ctx.socket_path) - 1);
    ctx.socket_path[sizeof(ctx.socket_path) - 1] = '\0';
    ctx.interrupted = 0;
    return est_repl_run(&ctx);
}
