/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#define _GNU_SOURCE
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#ifndef __vita__
#include <sys/mman.h>
#endif
#include <sys/time.h>
#include <pwd.h>
#include <dlfcn.h>
#include <libgen.h>

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

//=============================================================================

/*
================
Sys_Milliseconds
================
*/
/* base time in seconds, that's our origin
   timeval:tv_sec is an int: 
   assuming this wraps every 0x7fffffff - ~68 years since the Epoch (1970) - we're safe till 2038
   using unsigned long data type to work right with Sys_XTimeToSysTime */
unsigned long sys_timeBase = 0;
/* current time in ms, using sys_timeBase as origin
   NOTE: sys_timeBase*1000 + curtime -> ms since the Epoch
     0x7fffffff ms - ~24 days
   although timeval:tv_usec is an int, I'm not sure whether it is actually used as an unsigned int
     (which would affect the wrap period) */
int Sys_Milliseconds( void )
{
	struct timeval tp;
	int curtime;

	gettimeofday( &tp, NULL );
	
	if ( !sys_timeBase )
	{
		sys_timeBase = tp.tv_sec;
		return tp.tv_usec/1000;
	}

	curtime = (tp.tv_sec - sys_timeBase) * 1000 + tp.tv_usec / 1000;
	
	return curtime;
}


char *strlwr( char *s ) {
  if ( s==NULL ) { // bk001204 - paranoia
    assert(0);
    return s;
  }
  while (*s) {
    *s = tolower(*s);
    s++;
  }
  return s; // bk001204 - duh
}


/*
==================
Sys_RandomBytes
==================
*/
qboolean Sys_RandomBytes( byte *string, int len )
{
	FILE *fp;

	fp = fopen( "/dev/urandom", "r" );
	if( !fp )
		return qfalse;

	setvbuf( fp, NULL, _IONBF, 0 ); // don't buffer reads from /dev/urandom

	if ( fread( string, sizeof( byte ), len, fp ) != len ) {
		fclose( fp );
		return qfalse;
	}

	fclose( fp );
	return qtrue;
}


//============================================


// bk001129 - new in 1.26
void Sys_ListFilteredFiles( const char *basedir, const char *subdirs, const char *filter, char **list, int *numfiles ) {
	char	search[MAX_OSPATH*2+1];
	char	newsubdirs[MAX_OSPATH*2];
	char	filename[MAX_OSPATH*2];
	DIR		*fdir;
	struct	dirent *d;
	struct	stat st;

	if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
		return;
	}

	if ( *subdirs ) {
		Com_sprintf( search, sizeof(search), "%s/%s", basedir, subdirs );
	}
	else {
		Com_sprintf( search, sizeof(search), "%s", basedir );
	}

	if ((fdir = opendir(search)) == NULL) {
		return;
	}

	while ((d = readdir(fdir)) != NULL) {
		Com_sprintf(filename, sizeof(filename), "%s/%s", search, d->d_name);
		if (stat(filename, &st) == -1)
			continue;

		if (st.st_mode & S_IFDIR) {
			if ( !Q_streq( d->d_name, "." ) && !Q_streq( d->d_name, ".." ) ) {
				if ( *subdirs) {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s/%s", subdirs, d->d_name);
				} else {
					Com_sprintf( newsubdirs, sizeof(newsubdirs), "%s", d->d_name);
				}
				Sys_ListFilteredFiles( basedir, newsubdirs, filter, list, numfiles );
			}
		}
		if ( *numfiles >= MAX_FOUND_FILES - 1 ) {
			break;
		}
		Com_sprintf( filename, sizeof(filename), "%s/%s", subdirs, d->d_name );
		if ( !Com_FilterPath( filter, filename ) )
			continue;
		list[ *numfiles ] = FS_CopyString( filename );
		(*numfiles)++;
	}

	closedir(fdir);
}


// bk001129 - in 1.17 this used to be
// char **Sys_ListFiles( const char *directory, const char *extension, int *numfiles, qboolean wantsubs )
char **Sys_ListFiles( const char *directory, const char *extension, const char *filter, int *numfiles, qboolean wantsubs )
{
	struct dirent *d;
	DIR		*fdir;
	qboolean dironly = wantsubs;
	char		search[MAX_OSPATH*2+MAX_QPATH+1];
	int			nfiles;
	int			extLen;
	int			length;
	char		**listCopy;
	char		*list[MAX_FOUND_FILES];
	int			i;
	struct stat st;
	qboolean	hasPatterns;
	const char	*x;

	if ( filter ) {

		nfiles = 0;
		Sys_ListFilteredFiles( directory, "", filter, list, &nfiles );

		list[ nfiles ] = NULL;
		*numfiles = nfiles;

		if ( !nfiles )
			return NULL;

		listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( listCopy[0] ) );
		for ( i = 0 ; i < nfiles ; i++ ) {
			listCopy[i] = list[i];
		}
		listCopy[i] = NULL;

		return listCopy;
	}

	if ( !extension)
		extension = "";

	if ( extension[0] == '/' && extension[1] == 0 ) {
		extension = "";
		dironly = qtrue;
	}

	if ((fdir = opendir(directory)) == NULL) {
		*numfiles = 0;
		return NULL;
	}

	extLen = (int)strlen( extension );
	hasPatterns = Com_HasPatterns( extension );
	if ( hasPatterns && extension[0] == '.' && extension[1] != '\0' ) {
		extension++;
	}
	
	// search
	nfiles = 0;

	while ((d = readdir(fdir)) != NULL) {
		if ( nfiles == MAX_FOUND_FILES - 1 )
			break;
		Com_sprintf(search, sizeof(search), "%s/%s", directory, d->d_name);
		if (stat(search, &st) == -1)
			continue;
		if ((dironly && !(st.st_mode & S_IFDIR)) ||
			(!dironly && (st.st_mode & S_IFDIR)))
			continue;
		if ( *extension ) {
			if ( hasPatterns ) {
				x = strrchr( d->d_name, '.' );
				if ( !x || !Com_FilterExt( extension, x+1 ) ) {
					continue;
				}
			} else {
				length = (int) strlen( d->d_name );
				if ( length < extLen || Q_stricmp( d->d_name + length - extLen, extension ) ) {
					continue;
				}
			}
		}
		list[ nfiles ] = FS_CopyString( d->d_name );
		nfiles++;
	}

	list[ nfiles ] = NULL;

	closedir( fdir );

	// return a copy of the list
	*numfiles = nfiles;

	if ( !nfiles ) {
		return NULL;
	}

	listCopy = Z_Malloc( ( nfiles + 1 ) * sizeof( listCopy[0] ) );
	for ( i = 0 ; i < nfiles ; i++ ) {
		listCopy[i] = list[i];
	}
	listCopy[i] = NULL;

	Com_SortFileList( listCopy, nfiles, extension[0] != '\0' );

	return listCopy;
}


/*
=================
Sys_FreeFileList
=================
*/
void Sys_FreeFileList( char **list ) {
	int		i;

	if ( !list ) {
		return;
	}

	for ( i = 0 ; list[i] ; i++ ) {
		Z_Free( list[i] );
	}

	Z_Free( list );
}


/*
=============
Sys_GetFileStats
=============
*/
qboolean Sys_GetFileStats( const char *filename, fileOffset_t *size, fileTime_t *mtime, fileTime_t *ctime ) {
	struct stat s;

	if ( stat( filename, &s ) == 0 ) {
		*size = (fileOffset_t)s.st_size;
		*mtime = (fileTime_t)s.st_mtime;
		*ctime = (fileTime_t)s.st_ctime;
		return qtrue;
	} else {
		*size = 0;
		*mtime = *ctime = 0;
		return qfalse;
	}
}


/*
=================
Sys_Mkdir
=================
*/
qboolean Sys_Mkdir( const char *path )
{

	if ( mkdir( path, 0750 ) == 0 ) {
		return qtrue;
	} else {
		if ( errno == EEXIST ) {
			return qtrue;
		} else {
			return qfalse;
		}
	}
}


/*
=================
Sys_FOpen
=================
*/
FILE *Sys_FOpen( const char *ospath, const char *mode )
{
	struct stat buf;

	// check if path exists and it is not a directory
	if ( stat( ospath, &buf ) == 0 && S_ISDIR( buf.st_mode ) )
		return NULL;

	return fopen( ospath, mode );
}


/*
==============
Sys_ResetReadOnlyAttribute
==============
*/
qboolean Sys_ResetReadOnlyAttribute( const char *ospath )
{
	return qfalse;
}


/*
=================
Sys_Pwd
=================
*/
const char *Sys_Pwd( void ) 
{
#ifdef __vita__
	return "ux0:data/ioq3";
#else
	static char pwd[ MAX_OSPATH ];

	if ( pwd[0] )
		return pwd;

	// more reliable, linux-specific
	if ( readlink( "/proc/self/exe", pwd, sizeof( pwd ) - 1 ) != -1 )
	{
		pwd[ sizeof( pwd ) - 1 ] = '\0';
		dirname( pwd );
		return pwd;
	}

	if ( !getcwd( pwd, sizeof( pwd ) ) )
	{
		pwd[0] = '\0';
	}

	return pwd;
#endif
}


/*
=================
Sys_DefaultBasePath
=================
*/
const char *Sys_DefaultBasePath( void )
{
	return Sys_Pwd();
}


/*
=================
Sys_DefaultHomePath
=================
*/
const char *Sys_DefaultHomePath( void )
{
	// Used to determine where to store user-specific files
	static char homePath[ MAX_OSPATH ];

	const char *p;

	if ( *homePath )
		return homePath;
            
	if ( (p = getenv("HOME")) != NULL ) 
	{
		Q_strncpyz( homePath, p, sizeof( homePath ) );
#ifdef MACOS_X
		Q_strcat( homePath, sizeof(homePath), "/Library/Application Support/Quake3" );
#else
		Q_strcat( homePath, sizeof( homePath ), "/.q3a" );
#endif
		if ( mkdir( homePath, 0750 ) ) 
		{
			if ( errno != EEXIST ) 
				Sys_Error( "Unable to create directory \"%s\", error is %s(%d)\n", 
					homePath, strerror( errno ), errno );
		}
		return homePath;
	}
	return ""; // assume current dir
}


/*
 ================
Sys_SteamPath
================
*/
const char *Sys_SteamPath( void )
{
	static char steamPath[ MAX_OSPATH ];
	// Disabled since Steam doesn't let you install Quake 3 on Mac/Linux
#if 0
	const char *p;

	if( ( p = getenv( "HOME" ) ) != NULL )
	{
#ifdef MACOS_X
		char *steamPathEnd = "/Library/Application Support/Steam/SteamApps/common/" STEAMPATH_NAME;
#else
		char *steamPathEnd = "/.steam/steam/SteamApps/common/" STEAMPATH_NAME;
#endif
		Com_sprintf(steamPath, sizeof(steamPath), "%s%s", p, steamPathEnd);
	}
#endif
	return steamPath;
}


/*
=================
Sys_ShowConsole
=================
*/
void Sys_ShowConsole( int visLevel, qboolean quitOnClose )
{
	// not implemented
}


/*
========================================================================

LOAD/UNLOAD DLL

========================================================================
*/


static int dll_err_count = 0;


#ifdef __vita__
#include <vitasdk.h>
#define RTLD_LAZY 0x0001
#define RTLD_NOW  0x0002

typedef struct dllexport_s
{
	const char *name;
	void *func;
} dllexport_t;

typedef struct Dl_info_s
{
	void *dli_fhandle;
	const char *dli_sname;
	const void *dli_saddr;
} Dl_info;

typedef struct sysfuncs_s
{
	// mem
	void* (*pfnSysMalloc)(size_t);
	void* (*pfnSysCalloc)(size_t, size_t);
	void* (*pfnSysRealloc)(void*, size_t);
	void  (*pfnSysFree)(void*);
	// i/o
	FILE* (*pfnSysFopen)(const char*, const char*);
	int (*pfnSysFclose)(FILE*);
	int (*pfnSysFseek)(FILE*, long int, int);
	long int (*pfnSysFtell)(FILE*);
	int (*pfnSysFprintf)(FILE*, const char*, ...);
	size_t (*pfnSysFread)(void*, size_t, size_t, FILE*);
	size_t (*pfnSysFwrite)(const void*, size_t, size_t, FILE*);
	// sprintf
	int (*pfnSprintf)(char*, const char*, ...);
	int (*pfnSnprintf)(char*, int, const char*, ...);
	int (*pfnVsnprintf)(char*, int, const char*, va_list);
} sysfuncs_t;

#define MAX_DLNAMELEN 256

typedef struct dll_s
{
	SceUID handle;
	char name[MAX_DLNAMELEN];
	int refcnt;
	dllexport_t *exp;
	struct dll_s *next;
} dll_t;

typedef struct modarg_s
{
	sysfuncs_t imports;
	dllexport_t *exports;
} modarg_t;

static sysfuncs_t sys_exports = {
	// mem
	malloc,
	calloc,
	realloc,
	free,
	// io
	fopen,
	fclose,
	fseek,
	ftell,
	fprintf,
	fread,
	fwrite,
	// string
	sprintf,
	snprintf,
	vsnprintf,
};

static modarg_t modarg;

static dll_t *dll_list;
static char *dll_err = NULL;
static char dll_err_buf[1024];

static void *dlfind( const char *name )
{
	dll_t *d = NULL;
	for( d = dll_list; d; d = d->next )
		if( !strcmp( d->name, name ) )
			break;
	return d;
}

static const char *dlname( void *handle )
{
	dll_t *d = NULL;
	// iterate through all dll_ts to check if the handle is actually in the list
	// and not some bogus pointer from god knows where
	for( d = dll_list; d; d = d->next ) if( d == handle ) break;
	return d ? d->name : NULL;
}

void *dlopen( const char *name, int flag )
{
	if( !name ) return NULL;

	dll_t *old = dlfind( name );
	if( old ) { old->refcnt++; return old; }

	modarg.imports = sys_exports;
	modarg.exports = NULL;

	int status = 0;
	modarg_t *arg = &modarg;
	SceUID h = sceKernelLoadStartModule( name, sizeof( arg ), &arg, 0, NULL, &status );
	if( !h ) { dll_err = "dlopen(): something went wrong"; return NULL; }
	if( h < 0 )
	{
		snprintf( dll_err_buf, sizeof( dll_err_buf ), "dlopen(%s): error 0x%X\n", name, h );
		dll_err = dll_err_buf;
		return NULL;
	}

	if( status == SCE_KERNEL_START_FAILED || status == SCE_KERNEL_START_NO_RESIDENT )
	{
		dll_err = "dlopen(): module_start() failed";
		return NULL;
	}

	if( !modarg.exports )
	{
		dll_err = "dlopen(): NULL exports";
		return NULL;
	}

	dll_t *new = calloc( 1, sizeof( dll_t ) );
	if( !new ) { dll_err = "dlopen(): out of memory";  return NULL; }
	snprintf( new->name, MAX_DLNAMELEN, name );
	new->handle = h;
	new->exp = modarg.exports;
	new->refcnt = 1;

	new->next = dll_list;
	dll_list = new;

	return new;
}

void *dlsym( void *handle, const char *symbol )
{
	if( !handle || !symbol ) { dll_err = "dlsym(): NULL args"; return NULL; }
	if( !dlname( handle ) ) { dll_err = "dlsym(): unknown handle"; return NULL; }
	dll_t *d = handle;
	if( !d->refcnt ) { dll_err = "dlsym(): call dlopen() first"; return NULL; }
	dllexport_t *f = NULL;
	for( f = d->exp; f && f->func; f++ )
		if( !strcmp( f->name, symbol ) )
			break;

	if( f && f->func )
	{
		return f->func;
	}
	else
	{
		dll_err = "dlsym(): symbol not found in dll";
		return NULL;
	}
}

int dlclose( void *handle )
{
	if( !handle ) { dll_err = "dlclose(): NULL arg"; return -1; }
	if( !dlname( handle ) ) { dll_err = "dlclose(): unknown handle"; return -2; }

	dll_t *d = handle;
	d->refcnt--;
	if( d->refcnt <= 0 )
	{
		int status = 0;
		int ret = sceKernelStopUnloadModule( d->handle, 0, NULL, 0, NULL, &status );
		if( ret != SCE_OK )
		{
			snprintf( dll_err_buf, sizeof( dll_err_buf ), "dlclose(): error %d", ret );
			dll_err = dll_err_buf;
		}
		else if( status == SCE_KERNEL_STOP_CANCEL )
		{
			dll_err = "dlclose(): module doesn't want to stop";
			return -3;
		}

		if( d == dll_list )
			dll_list = d->next;
		else
			for( dll_t *pd = dll_list; pd; pd = pd->next )
			{
				if( pd->next == d )
				{
					pd->next = d->next;
					break;
				}
			}

		free( d );
	}

	return 0;
}

char *dlerror( void )
{
	char *err = dll_err;
	dll_err = NULL;
	return err;
}

int dladdr( const void *addr, Dl_info *info )
{
	dll_t *d = NULL;
	dllexport_t *f = NULL;
	for( d = dll_list; d; d = d->next )
		for( f = d->exp; f && f->func; f++ )
			if( f->func == addr ) goto for_end;
for_end:
	if( d && f && f->func )
	{
		if( info )
		{
			info->dli_fhandle = d;
			info->dli_sname = f->name;
			info->dli_saddr = addr;
		}
		return 1;
	}
	return 0;
}
#endif

/*
=================
Sys_LoadLibrary
=================
*/
void *Sys_LoadLibrary( const char *name )
{
	const char *ext;
	void *handle = NULL;
#ifndef __vita__
	if ( FS_AllowedExtension( name, qfalse, &ext ) )
	{
		Com_Error( ERR_FATAL, "Sys_LoadLibrary: Unable to load library with '%s' extension", ext );
	}
#endif
	handle = dlopen( name, RTLD_NOW );
	if (!handle) {
		printf("Error while Loading %s: %s\n", dll_err);
	}
	return handle;
}


/*
=================
Sys_UnloadLibrary
=================
*/
void Sys_UnloadLibrary( void *handle )
{
	if ( handle != NULL )
		dlclose( handle );
}


/*
=================
Sys_LoadFunction
=================
*/
void *Sys_LoadFunction( void *handle, const char *name )
{
	const char *error;
	char buf[1024];
	void *symbol = NULL;
	size_t nlen;

	if ( handle == NULL || name == NULL || *name == '\0' ) 
	{
		dll_err_count++;
		return NULL;
	}

	dlerror(); /* clear old error state */
	symbol = dlsym( handle, name );
	error = dlerror();
	if ( error != NULL )
	{
		nlen = strlen( name ) + 1;
		if ( nlen >= sizeof( buf ) )
			return NULL;
		buf[0] = '_';
		strcpy( buf+1, name );
		dlerror(); /* clear old error state */
		symbol = dlsym( handle, buf );
	}

	if ( !symbol )
		dll_err_count++;

	return symbol;
}


/*
=================
Sys_LoadFunctionErrors
=================
*/
int Sys_LoadFunctionErrors( void )
{
	int result = dll_err_count;
	dll_err_count = 0;
	return result;
}


#ifdef USE_AFFINITY_MASK
/*
=================
Sys_GetAffinityMask
=================
*/
uint64_t Sys_GetAffinityMask( void )
{
	cpu_set_t cpu_set;

	if ( sched_getaffinity( getpid(), sizeof( cpu_set ), &cpu_set ) == 0 ) {
		uint64_t mask = 0;
		int cpu;
		for ( cpu = 0; cpu < sizeof( mask ) * 8; cpu++ ) {
			if ( CPU_ISSET( cpu, &cpu_set ) ) {
				mask |= (1ULL << cpu);
			}
		}
		return mask;
	} else {
		return 0;
	}
}


/*
=================
Sys_SetAffinityMask
=================
*/
qboolean Sys_SetAffinityMask( const uint64_t mask )
{
	cpu_set_t cpu_set;
	int cpu;

	CPU_ZERO( &cpu_set );
	for ( cpu = 0; cpu < sizeof( mask ) * 8; cpu++ ) {
		if ( mask & (1ULL << cpu) ) {
			CPU_SET( cpu, &cpu_set );
		}
	}

	if ( sched_setaffinity( getpid(), sizeof( cpu_set ), &cpu_set ) == 0 ) {
		return qtrue;
	} else {
		return qfalse;
	}
}
#endif // USE_AFFINITY_MASK
