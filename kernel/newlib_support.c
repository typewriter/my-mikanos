#include <errno.h>
#include <sys/types.h>

caddr_t sbrk(int incr)
{
    return NULL;
}

void _exit(void)
{
    while (1)
        __asm__("hlt");
}

int getpid(void)
{
    return 1;
}

int kill(int pid, int sig)
{
    errno = EINVAL;
    return -1;
}
