#ifndef COMPAT_H
#define COMPAT_H

#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma warning(disable:4996)
#endif // _MSC_VER

#ifdef _MSC_VER
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/param.h>
#endif // _MSC_VER

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#define strcasecmp(s1, s2) _stricmp(s1, s2)
#define strncasecmp(s1, s2, n) strnicmp(s1, s2, n)

#ifndef S_ISREG 
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#define fsync _commit
#define ftruncate _chsize
#endif // _WIN32

#endif // COMPAT_H
