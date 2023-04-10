#include <windows.h>
#include <stdio.h> 
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <io.h>

#include <cpuid.h>

#define NULL_ZERO_MALLOC 1

// include SSE2 intrinsics require some target switching
#pragma GCC push_options
#pragma GCC target ("arch=core2")
#include <emmintrin.h>
// restore the target selection
#pragma GCC pop_options

#define MEM_ALIGN 16

#define MEM_R1 64
#define MEM_R2 1024
#define MEM_R3 8*1024
#define MEM_R4 64*1024

typedef void(*memcpy_func)(void *dst, void *src, size_t size);
typedef void(*zeromem_func)(void *dst, size_t size);

static HANDLE glHeaps[4] = {NULL, NULL, NULL, NULL};

#define AROUND(_s, _a) _s = ((_s + _a - 1) & (~(size_t)(_a - 1)))

typedef struct _memblk_t
{
	HANDLE heap;
	uint8_t *heap_ptr;
	size_t mem_size;
} memblk_t;

static HANDLE NewHeap()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
			
	return HeapCreate(0, si.dwPageSize*32, 0);
}

static HANDLE GetHeap(size_t memsize)
{
	int sel = 3;
	
	if(memsize < MEM_R2)
	{
		sel = 0;
	}
	else if(memsize < MEM_R3)
	{
		sel = 1;
	}
	else if(memsize < MEM_R4)
	{
		sel = 2;
	}
	
	if(glHeaps[sel] == NULL)
	{
		glHeaps[sel] = NewHeap();
	}
	
	return glHeaps[sel];
}

static void memcpy_c(void *dst, void *src, size_t size)
{
	size_t bsize = size/4;
	
	uint32_t *psrc = src;
	uint32_t *pdst = dst;
	
	for (; bsize > 0; bsize--, psrc++, pdst++)
	{
		*pdst = *psrc;
	}
}

static void zeromem_c(void *dst, size_t size)
{
	size_t bsize = size/4;
	
	uint32_t *pdst = dst;
	
	for (; bsize > 0; bsize--, pdst++)
	{
		*pdst = 0;
	}
}

static memcpy_func memcpy_fast = memcpy_c;
static zeromem_func zeromem_fast = zeromem_c;

#pragma GCC push_options
#pragma GCC target ("arch=core2")

static void memcpy_sse2(void *dst, void *src, size_t size)
{
	size_t bsize = size/16;
	
	__m128i *psrc = (__m128i*)src;
	__m128i *pdst = (__m128i*)dst;
	
	for (; bsize > 0; bsize--, psrc++, pdst++)
	{
		const __m128i t = _mm_load_si128(psrc);
		_mm_store_si128(pdst, t);
		//pdst* = *psrc;
	}
}

static void zeromem_sse2(void *dst, size_t size)
{
	size_t bsize = size/16;
	__m128i t = _mm_setzero_si128();
	__m128i *pdst = (__m128i*)dst;
	
	for (; bsize > 0; bsize--, pdst++)
	{
		_mm_store_si128(pdst, t);
	}
}

#pragma GCC pop_options

void crt_enable_sse2()
{
	memcpy_fast = memcpy_sse2;
	zeromem_fast = zeromem_sse2;
}

typedef DWORD (WINAPI *GetVersionFunc)(void);

static int windows_sse_support()
{
	HANDLE h = GetModuleHandleA("kernel32.dll");
	if(h)
	{
		GetVersionFunc GetVersionPtr = (GetVersionFunc)GetProcAddress(h, "GetVersion");
    DWORD dwVersion = 0; 
    DWORD dwMajorVersion = 0;
    DWORD dwMinorVersion = 0; 
    DWORD dwBuild = 0;
		
		if(GetVersionPtr == NULL)
		{
			/* windows 8.1 don't have this function */
			return 3;
		}
		
		dwVersion = GetVersionPtr();
		
		dwMajorVersion = (DWORD)(LOBYTE(LOWORD(dwVersion)));
		dwMinorVersion = (DWORD)(HIBYTE(LOWORD(dwVersion)));

    // Get the build number.
    if (dwVersion < 0x80000000)              
        dwBuild = (DWORD)(HIWORD(dwVersion));

    // 95     - 4.0
    // NT 4.0 - 4.0
    // 98     - 4.10
    // ME     - 4.90
    // 2k     - 5.0
    //printf("Version is %d.%d (%d)\n", dwMajorVersion, dwMinorVersion, dwBuild);
		
		if(dwMajorVersion > 5)
		{
			return 1;
		}
		
		if(dwMajorVersion == 4)
		{
			if(dwMinorVersion >= 10)
			{
				return 1;
			}
		}
			
		return 0;
	}
	
	return 0;
}

int crt_sse2_is_safe()
{
	if(windows_sse_support() != 0)
	{
		uint32_t ceax, cebx, cecx, cedx;
		
		if(__get_cpuid (1, &ceax, &cebx, &cecx, &cedx))
		{
			if(cedx & (1 << 26)) // SSE2 support bit
			{
				return 1;
			}
		}
	}
	
	return 0;
}

static inline void *malloc_int(size_t size)
{
	size_t hsize;
	uint8_t *hptr;
	memblk_t *mem;
	uintptr_t p;
	HANDLE heap;
	
	hsize = size + sizeof(memblk_t) + MEM_ALIGN - 1;
	
	heap = GetHeap(size);
	
	hptr = HeapAlloc(heap, 0, hsize);
	if(hptr != NULL)
	{
		p = (uintptr_t)hptr;
		p += MEM_ALIGN - 1;
		p += sizeof(memblk_t);
		p &= ~((uintptr_t)(MEM_ALIGN - 1));
		
		mem = ((memblk_t*)p)-1;
		mem->heap = heap;
		mem->heap_ptr = hptr;
		mem->mem_size = size;
		
		return mem+1;
	}
	
	return NULL;
}

void *malloc(size_t size)
{
	if(size == 0)
	{
		size = 16;
	}
	AROUND(size, 16);
		
	return malloc_int(size);
}

void *realloc(void *ptr, size_t new_size)
{
	memblk_t *mem;
	size_t hsize;
	void *tmp;
	
	if(new_size == 0 && ptr == NULL)
	{
		new_size = 16;
		AROUND(new_size, 16);
		return malloc_int(new_size);
	}
	else if(new_size == 0)
	{
		free(ptr);
		return NULL;
	}
	else if(ptr == NULL)
	{
		AROUND(new_size, 16);
		return malloc_int(new_size);
	}
	
	AROUND(new_size, 16);
	
	mem = ((memblk_t*)ptr)-1;
	if(new_size == mem->mem_size)
	{
		return ptr;
	}
	
	hsize = (uint8_t*)ptr - mem->heap_ptr + new_size;
	tmp = HeapReAlloc(mem->heap, HEAP_REALLOC_IN_PLACE_ONLY, mem->heap_ptr, hsize);
	
	if(tmp != NULL)
	{
		mem->mem_size = new_size;
		return ptr;
	}
	else
	{
		tmp = malloc_int(new_size);
		if(tmp)
		{
			memcpy_fast(tmp, ptr, mem->mem_size);
			free(ptr);
			
			return tmp;
		}
	}
	
	return NULL;
}

void *calloc(size_t num, size_t size)
{
	size_t total = num*size;
	void *ptr;
	if(total == 0)
	{
		size = 16;
	}
	AROUND(total, 16);
	
	ptr = malloc_int(total);
  if(ptr != NULL)
  {
  	//memset(ptr, 0, total);
  	zeromem_fast(ptr, total);
  }
  
  return ptr;
}

void free(void *ptr)
{
	if(ptr != NULL)
	{
		memblk_t *mem = ((memblk_t*)ptr)-1;
		
		HeapFree(mem->heap, 0, mem->heap_ptr);
	}
}

__declspec(dllimport) void* _expand(void* ptr, size_t new_size)
{	
	if(ptr != NULL)
	{
		size_t hsize;
		void *tmp;
		memblk_t *mem = ((memblk_t*)ptr)-1;
	
		hsize = (uint8_t*)ptr - mem->heap_ptr + new_size;
		
		tmp = HeapReAlloc(mem->heap, HEAP_REALLOC_IN_PLACE_ONLY, mem->heap_ptr, hsize);
		
		if(tmp)
		{
			mem->mem_size = new_size;
			return ptr;
		}
	}
	
	return NULL;
	//return HeapReAlloc(GetHeap(), HEAP_REALLOC_IN_PLACE_ONLY, ptr, new_size);
}

size_t _msize2(void* ptr)
{	
	if(ptr != NULL)
	{
		//return HeapSize(GetHeap(), 0, ptr);
		memblk_t *mem = (memblk_t*)ptr;
		mem--;
		
		return mem->mem_size;
	}
	
	return 0;
}

__declspec(dllimport) size_t _msize(void* ptr)
{
	return _msize2(ptr);
}

char *strdup(const char *str1)
{
	size_t len = strlen(str1);
	char *str2 = malloc(len+1);
	if(str2 != NULL)
	{
		memcpy(str2, str1, len+1);
	}
	
	return str2;
}

__declspec(dllimport) char *_strdup(const char *str1)
{
	size_t len = strlen(str1);
	char *str2 = malloc(len+1);
	if(str2 != NULL)
	{
		memcpy(str2, str1, len+1);
	}
	
	return str2;
}

char *strndup(const char *str1, size_t size)
{
	size_t len = strlen(str1);
	
	if(size == 0)
	{
		return NULL;
	}
	
	if(len > size-1)
	{
		len = size-1;
	}
	
	char *str2 = malloc(len+1);
	if(str2 != NULL)
	{
		memcpy(str2, str1, len+1);
	}
	
	str2[len] = '\0';
	
	return str2;
}

static unsigned int crt_locks_num = 0;
CRITICAL_SECTION *crt_locks = NULL;

void crt_locks_init(int count)
{
	int i;
	if(count > crt_locks_num)
	{
		crt_locks = realloc(crt_locks, sizeof(CRITICAL_SECTION) * crt_locks_num);
		
		for(i = crt_locks_num; i < count; i++)
		{
			InitializeCriticalSection(crt_locks+i);
		}
		
		crt_locks_num = count;
	}
}

void crt_locks_destroy()
{
	int i;
	for(i = 0; i < crt_locks_num; i++)
	{
		DeleteCriticalSection(crt_locks+i);
	}
	
	free(crt_locks);
}

void crt_lock(int lock_no)
{
	if(lock_no < crt_locks_num)
	{
		EnterCriticalSection(crt_locks+lock_no);
	}
}

void crt_unlock(int lock_no)
{
	if(lock_no < crt_locks_num)
	{
		LeaveCriticalSection(crt_locks+lock_no);
	}
}



/*
  From: https://github.com/gcc-mirror/gcc/blob/master/libiberty/
*/

#ifndef ERANGE
#define ERANGE 34
#endif

uint64_t strtoull(const char *nptr, char **endptr, register int base)
{
	register const char *s = nptr;
	register uint64_t acc;
	register int c;
	register uint64_t cutoff;
	register int neg = 0, any, cutlim;

	/*
	 * See strtol for comments as to the logic used.
	 */
	do {
		c = *s++;
	} while (isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;
	cutoff = (uint64_t)ULLONG_MAX / (uint64_t)base;
	cutlim = (uint64_t)ULLONG_MAX % (uint64_t)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (isdigit(c))
			c -= '0';
		else if (isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = ULLONG_MAX;
		errno = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return (acc);
}

int64_t strtoll(const char *nptr, char **endptr, register int base)
{
	register const char *s = nptr;
	register uint64_t acc;
	register int c;
	register uint64_t cutoff;
	register int neg = 0, any, cutlim;

	/*
	 * Skip white space and pick up leading +/- sign if any.
	 * If base is 0, allow 0x for hex and 0 for octal, else
	 * assume decimal; if base is already 16, allow 0x.
	 */
	do {
		c = *s++;
	} while (isspace(c));
	if (c == '-') {
		neg = 1;
		c = *s++;
	} else if (c == '+')
		c = *s++;
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X')) {
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	 * Compute the cutoff value between legal numbers and illegal
	 * numbers.  That is the largest legal value, divided by the
	 * base.  An input number that is greater than this value, if
	 * followed by a legal input character, is too big.  One that
	 * is equal to this value may be valid or not; the limit
	 * between valid and invalid numbers is then based on the last
	 * digit.  For instance, if the range for longs is
	 * [-2147483648..2147483647] and the input base is 10,
	 * cutoff will be set to 214748364 and cutlim to either
	 * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
	 * a value > 214748364, or equal but the next digit is > 7 (or 8),
	 * the number is too big, and we will return a range error.
	 *
	 * Set any if any `digits' consumed; make it negative to indicate
	 * overflow.
	 */
	cutoff = neg ? -(uint64_t)LLONG_MIN : LLONG_MAX;
	cutlim = cutoff % (uint64_t)base;
	cutoff /= (uint64_t)base;
	for (acc = 0, any = 0;; c = *s++) {
		if (isdigit(c))
			c -= '0';
		else if (isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim))
			any = -1;
		else {
			any = 1;
			acc *= base;
			acc += c;
		}
	}
	if (any < 0) {
		acc = neg ? LLONG_MIN : LLONG_MAX;
		errno = ERANGE;
	} else if (neg)
		acc = -acc;
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return (acc);
}

/*
  
*/

#define _EXIT_LOCK1     13      /* lock #1 for exit code            */

#define ONEXITTBLINCR   4

#ifndef _MT
#define _MT
#endif

typedef void (__cdecl *_PVFV)(void);
typedef int  (__cdecl *_PIFV)(void);

/* msvcrt imports */
void __cdecl _lock(int);
void __cdecl _unlock(int);

void _lockexit (void)
{
	_lock(_EXIT_LOCK1);
}

void _unlockexit(void)
{
	_unlock(_EXIT_LOCK1);
}

_onexit_t __dllonexit (
        _onexit_t func,
        _PVFV ** pbegin,
        _PVFV ** pend
        )
{
        _PVFV   *p;
        unsigned oldsize;

#ifdef _MT
        _lockexit();            /* lock the exit code */
#endif  /* _MT */

        /*
         * First, make sure the table has room for a new entry
         */
        if ( (oldsize = _msize2( *pbegin )) <= (unsigned)((char *)(*pend) -
            (char *)(*pbegin)) ) {
            /*
             * not enough room, try to grow the table
             */
            if ( (p = (_PVFV *) realloc((*pbegin), oldsize +
                ONEXITTBLINCR * sizeof(_PVFV))) == NULL )
            {
                /*
                 * didn't work. don't do anything rash, just fail
                 */
#ifdef _MT
                _unlockexit();
#endif  /* _MT */

                return NULL;
            }

            /*
             * update (*pend) and (*pbegin)
             */
            (*pend) = p + ((*pend) - (*pbegin));
            (*pbegin) = p;
        }

        /*
         * Put the new entry into the table and update the end-of-table
         * pointer.
         */
         *((*pend)++) = (_PVFV)func;

#ifdef _MT
        _unlockexit();
#endif  /* _MT */

        return func;

}
