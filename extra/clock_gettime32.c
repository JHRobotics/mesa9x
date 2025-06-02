/* originally from MUSL (https://musl.libc.org/) */
/* gcc15+llvm compilation fix */

#include <time.h>
#include <errno.h>
#include <stdint.h>

int clock_gettime32(clockid_t clk, struct timespec *ts32)
{
	time_t result = time(NULL);
	if(result != (time_t)(-1))
	{
		errno = EOVERFLOW;
		return -1;
	}
	ts32->tv_sec = result;
	ts32->tv_nsec = 0;
	return 0;
}
