#ifndef __MESA9X_VERSION_H__INCLUDED__

#define MESA9X_STR_(x) #x
#define MESA9X_STR(x) MESA9X_STR_(x)

#if MESA_MAJOR == 24
# define MESA9X_MAJOR 24
# define MESA9X_MINOR 1
# define MESA9X_PATCH 4
#elif MESA_MAJOR == 23
# define MESA9X_MAJOR 23
# define MESA9X_MINOR 1
# define MESA9X_PATCH 9
#elif MESA_MAJOR == 21
# define MESA9X_MAJOR 21
# define MESA9X_MINOR 3
# define MESA9X_PATCH 8
#else
# define MESA9X_MAJOR 17
# define MESA9X_MINOR 3
# define MESA9X_PATCH 9
#endif

#ifndef MESA9X_BUILD
#define MESA9X_BUILD 98
#endif

#define MESA9X_VERSION_STR_BUILD(_ma, _mi, _pa, _bl) \
	_ma "." _mi "." _pa "." _bl

#define MESA9X_VERSION_STR MESA9X_VERSION_STR_BUILD( \
	MESA9X_STR(MESA9X_MAJOR), \
	MESA9X_STR(MESA9X_MINOR), \
	MESA9X_STR(MESA9X_PATCH), \
	MESA9X_STR(MESA9X_BUILD))

#define MESA9X_VERSION_NUM \
 	MESA9X_MAJOR,MESA9X_MINOR,MESA9X_PATCH,MESA9X_BUILD \

#endif /* __MESA9X_VERSION_H__INCLUDED__ */
