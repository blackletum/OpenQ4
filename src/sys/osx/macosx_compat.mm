/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

// -*- mode: objc -*-
#import "../../idlib/precompiled.h"

#include "../posix/posix_public.h"

#import <AppKit/AppKit.h>
#import <OpenGL/OpenGL.h>
#import <mach-o/dyld.h>
#import <mach/mach_time.h>
#import <pthread.h>
#import <errno.h>
#import <limits.h>
#import <stdlib.h>
#import <string.h>
#import <sys/sysctl.h>
#import <sys/stat.h>
#import <unistd.h>

#import "macosx_local.h"
#import "macosx_sys.h"

static idStr	basepath;
static idStr	savepath;

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static bool Sys_CopyPathIfFits( char *outPath, size_t outPathSize, const char *sourcePath ) {
	if ( outPath == NULL || outPathSize <= 0 || sourcePath == NULL || sourcePath[0] == '\0' ) {
		return false;
	}
	if ( strlen( sourcePath ) >= outPathSize ) {
		outPath[0] = '\0';
		return false;
	}
	idStr::Copynz( outPath, sourcePath, outPathSize );
	return true;
}

static bool Sys_CopyExecutablePath( char *outPath, size_t outPathSize ) {
	if ( outPath == NULL || outPathSize <= 0 ) {
		return false;
	}

	uint32_t bufferSize = static_cast<uint32_t>( outPathSize );
	char *pathBuffer = outPath;
	bool allocatedPathBuffer = false;

	outPath[0] = '\0';
	if ( _NSGetExecutablePath( pathBuffer, &bufferSize ) != 0 ) {
		pathBuffer = static_cast<char *>( malloc( bufferSize ) );
		if ( pathBuffer == NULL ) {
			return false;
		}
		allocatedPathBuffer = true;
		if ( _NSGetExecutablePath( pathBuffer, &bufferSize ) != 0 ) {
			free( pathBuffer );
			return false;
		}
	}

	char resolvedPath[PATH_MAX];
	const char *copyPath = pathBuffer;
	if ( realpath( pathBuffer, resolvedPath ) != NULL ) {
		copyPath = resolvedPath;
	}
	if ( copyPath == outPath ) {
		if ( allocatedPathBuffer ) {
			free( pathBuffer );
		}
		return outPath[0] != '\0';
	}
	if ( !Sys_CopyPathIfFits( outPath, outPathSize, copyPath ) ) {
		if ( allocatedPathBuffer ) {
			free( pathBuffer );
		}
		return false;
	}

	if ( allocatedPathBuffer ) {
		free( pathBuffer );
	}
	return outPath[0] != '\0';
}

static void Sys_NormalizeMacOSDirectoryPath( idStr &path ) {
	path.BackSlashesToSlashes();
	while ( path.Length() > 1 && path[ path.Length() - 1 ] == '/' ) {
		path.CapLength( path.Length() - 1 );
	}
}

static bool Sys_IsAbsoluteMacOSPath( const idStr &path ) {
	return path.Length() > 0 && path[0] == '/';
}

static bool Sys_DirectoryExists( const char *path ) {
	struct stat st;
	return path != NULL && path[0] != '\0' && stat( path, &st ) != -1 && S_ISDIR( st.st_mode );
}

static bool Sys_ExecutableFileExists( const char *path ) {
	struct stat st;
	return path != NULL && path[0] != '\0' && stat( path, &st ) != -1 && S_ISREG( st.st_mode ) && access( path, X_OK ) == 0;
}

static bool Sys_DirectoryIsWritable( const idStr &path ) {
	return Sys_DirectoryExists( path.c_str() ) && access( path.c_str(), W_OK | X_OK ) == 0;
}

static bool Sys_EnsureMacOSDirectoryTree( const idStr &path ) {
	if ( !Sys_IsAbsoluteMacOSPath( path ) ) {
		common->Printf( "WARNING: refusing to create non-absolute macOS directory path '%s'\n", path.c_str() );
		return false;
	}
	if ( Sys_DirectoryExists( path.c_str() ) ) {
		return true;
	}

	idStr parent = path;
	parent.StripFilename();
	Sys_NormalizeMacOSDirectoryPath( parent );
	if ( parent.Length() <= 0 || parent.Cmp( path.c_str() ) == 0 ) {
		return false;
	}
	if ( !Sys_DirectoryExists( parent.c_str() ) && !Sys_EnsureMacOSDirectoryTree( parent ) ) {
		return false;
	}

	if ( mkdir( path.c_str(), 0700 ) == -1 && errno != EEXIST ) {
		common->Printf( "WARNING: could not create macOS directory '%s': %s\n", path.c_str(), strerror( errno ) );
		return false;
	}
	if ( !Sys_DirectoryExists( path.c_str() ) ) {
		common->Printf( "WARNING: macOS path '%s' exists but is not a directory\n", path.c_str() );
		return false;
	}
	return true;
}

static bool Sys_SetUsableMacOSSavePath( const idStr &candidate, const char *label ) {
	savepath = candidate;
	Sys_NormalizeMacOSDirectoryPath( savepath );

	if ( !Sys_IsAbsoluteMacOSPath( savepath ) ) {
		common->Printf( "WARNING: skipping non-absolute macOS %s save path '%s'\n", label, savepath.c_str() );
		return false;
	}
	if ( !Sys_EnsureMacOSDirectoryTree( savepath ) ) {
		common->Printf( "WARNING: macOS %s save path '%s' could not be created\n", label, savepath.c_str() );
		return false;
	}
	if ( !Sys_DirectoryIsWritable( savepath ) ) {
		common->Printf( "WARNING: macOS %s save path '%s' is not writable\n", label, savepath.c_str() );
		return false;
	}
	return true;
}

static bool Sys_GetMacOSHomeDirectory( idStr &homePath ) {
	homePath.Clear();

	const char *home = [NSHomeDirectory() fileSystemRepresentation];
	if ( home == NULL || home[0] == '\0' ) {
		home = getenv( "HOME" );
	}
	if ( home == NULL || home[0] != '/' ) {
		return false;
	}

	char resolvedPath[PATH_MAX];
	if ( realpath( home, resolvedPath ) != NULL ) {
		homePath = resolvedPath;
	} else {
		homePath = home;
	}
	Sys_NormalizeMacOSDirectoryPath( homePath );
	return Sys_IsAbsoluteMacOSPath( homePath );
}

static int Sys_RoundSystemRamMegabytes( unsigned long long bytes, int fallbackMegabytes ) {
	if ( bytes == 0 ) {
		return fallbackMegabytes;
	}

	unsigned long long megabytes = bytes / ( 1024ULL * 1024ULL );
	megabytes = ( megabytes + 8ULL ) & ~15ULL;
	if ( megabytes > static_cast<unsigned long long>( idMath::INT_MAX ) ) {
		return idMath::INT_MAX;
	}
	return static_cast<int>( megabytes );
}

/*
=================
Sys_AsyncThread
=================
*/
void Sys_AsyncThread( void ) {
	while ( 1 ) {
		if ( Sys_IsCurrentThreadStopRequested() ) {
			return;
		}

		usleep( 1000 );

		const int previousTicNumber = com_ticNumber;
		common->Async();
		for ( int tic = previousTicNumber; tic < com_ticNumber; ++tic ) {
			Sys_TriggerEvent( TRIGGER_EVENT_ONE );
		}
	}
}

/*
==============
Sys_EXEPath
==============
*/
const char *Sys_EXEPath( void ) {
	static char exePath[PATH_MAX];
	if ( Sys_CopyExecutablePath( exePath, sizeof( exePath ) ) ) {
		return exePath;
	}

	const char *bundlePath = [[[NSBundle mainBundle] executablePath] fileSystemRepresentation];
	if ( Sys_CopyPathIfFits( exePath, sizeof( exePath ), bundlePath ) ) {
		return exePath;
	}

	exePath[0] = '\0';
	return exePath;
}

static bool Sys_DirectoryContainsGameDir( const idStr &candidate ) {
	struct stat st;
	idStr testbase;

	if ( candidate.Length() <= 0 ) {
		return false;
	}

	testbase = candidate;
	testbase.AppendPath( BASE_GAMEDIR );
	return stat( testbase.c_str(), &st ) != -1 && S_ISDIR( st.st_mode );
}

static bool Sys_GetAppBundlePackageRootFromExecutableDirectory( const idStr &exeDirectory, idStr &appDirectory, idStr &packageDirectory ) {
	static const char appExecutableDirectorySuffix[] = "/Contents/MacOS";
	const int suffixLength = sizeof( appExecutableDirectorySuffix ) - 1;

	appDirectory.Clear();
	packageDirectory.Clear();
	if ( exeDirectory.Length() <= suffixLength ) {
		return false;
	}

	idStr normalizedDirectory = exeDirectory;
	normalizedDirectory.BackSlashesToSlashes();
	Sys_NormalizeMacOSDirectoryPath( normalizedDirectory );
	const char *suffixStart = normalizedDirectory.c_str() + normalizedDirectory.Length() - suffixLength;
	if ( idStr::Cmp( suffixStart, appExecutableDirectorySuffix ) != 0 ) {
		return false;
	}

	appDirectory = normalizedDirectory;
	appDirectory.StripFilename(); // Contents
	appDirectory.StripFilename(); // openQ4.app
	idStr appName = appDirectory;
	appName.StripPath();
	if ( appName.Icmp( "openQ4.app" ) != 0 ) {
		return false;
	}

	packageDirectory = appDirectory;
	packageDirectory.StripFilename(); // extracted package root
	return packageDirectory.Length() > 0;
}

static const char *Sys_MacOSPackageRuntimeArchSuffix( void ) {
#if defined( __aarch64__ ) || defined( __arm64__ )
	return "arm64";
#elif defined( __x86_64__ )
	return "x64";
#elif defined( __i386__ )
	return "x86";
#else
	return "unknown";
#endif
}

static void Sys_AppendMissingMacOSPackageRootEntry( idStr &missingEntries, const char *entry ) {
	if ( missingEntries.Length() > 0 ) {
		missingEntries.Append( ", " );
	}
	missingEntries.Append( entry );
}

static void Sys_RequireMacOSPackageRootDirectory( const idStr &packageDirectory, const char *entry, idStr &missingEntries ) {
	idStr testPath = packageDirectory;
	testPath.AppendPath( entry );
	if ( !Sys_DirectoryExists( testPath.c_str() ) ) {
		Sys_AppendMissingMacOSPackageRootEntry( missingEntries, entry );
	}
}

static void Sys_RequireMacOSPackageRootExecutable( const idStr &packageDirectory, const char *entry, idStr &missingEntries ) {
	idStr testPath = packageDirectory;
	testPath.AppendPath( entry );
	if ( !Sys_ExecutableFileExists( testPath.c_str() ) ) {
		Sys_AppendMissingMacOSPackageRootEntry( missingEntries, entry );
	}
}

static idStr Sys_LocalizedMacOSPackageRootString( const char *key, const char *fallback ) {
	NSString *keyString = [NSString stringWithUTF8String:key != NULL ? key : ""];
	NSString *fallbackString = [NSString stringWithUTF8String:fallback != NULL ? fallback : ""];
	NSString *localizedString = [[NSBundle mainBundle] localizedStringForKey:keyString value:fallbackString table:@"OpenQ4PackageRoot"];
	if ( localizedString == nil || [localizedString length] == 0 ) {
		localizedString = fallbackString;
	}

	const char *utf8String = [localizedString UTF8String];
	if ( utf8String == NULL || utf8String[0] == '\0' ) {
		utf8String = fallback != NULL ? fallback : "";
	}
	return idStr( utf8String );
}

static void Sys_ErrorIfMacOSAppBundlePackageRootIncomplete( const idStr &appDirectory, const idStr &packageDirectory ) {
	idStr missingEntries;
	const char *arch = Sys_MacOSPackageRuntimeArchSuffix();
	char clientEntry[64];
	char dedicatedEntry[64];
	char spModuleEntry[96];
	char mpModuleEntry[96];

	idStr::snPrintf( clientEntry, sizeof( clientEntry ), "openQ4-client_%s", arch );
	idStr::snPrintf( dedicatedEntry, sizeof( dedicatedEntry ), "openQ4-ded_%s", arch );
	idStr::snPrintf( spModuleEntry, sizeof( spModuleEntry ), "%s/game-sp_%s.dylib", BASE_GAMEDIR, arch );
	idStr::snPrintf( mpModuleEntry, sizeof( mpModuleEntry ), "%s/game-mp_%s.dylib", BASE_GAMEDIR, arch );

	Sys_RequireMacOSPackageRootDirectory( packageDirectory, BASE_GAMEDIR, missingEntries );
	Sys_RequireMacOSPackageRootExecutable( packageDirectory, clientEntry, missingEntries );
	Sys_RequireMacOSPackageRootExecutable( packageDirectory, dedicatedEntry, missingEntries );
	Sys_RequireMacOSPackageRootExecutable( packageDirectory, spModuleEntry, missingEntries );
	Sys_RequireMacOSPackageRootExecutable( packageDirectory, mpModuleEntry, missingEntries );

	if ( missingEntries.Length() == 0 ) {
		return;
	}

	idStr title = Sys_LocalizedMacOSPackageRootString(
		"OpenQ4PackageRootMissingTitle",
		"openQ4.app adjacent package root is incomplete"
	);
	idStr body = Sys_LocalizedMacOSPackageRootString(
		"OpenQ4PackageRootMissingBody",
		"Keep openQ4.app, baseoq4/, openQ4-client_<arch>, and openQ4-ded_<arch> together in the same package folder. Moving only openQ4.app to /Applications is not supported yet."
	);

	Sys_Error(
		"%s\n\n%s\n\nExpected adjacent package-root contract: openQ4.app, loose binaries, and baseoq4/ together.\nPackage root: %s\nApp path: %s\nMissing or unusable entries: %s",
		title.c_str(),
		body.c_str(),
		packageDirectory.c_str(),
		appDirectory.c_str(),
		missingEntries.c_str()
	);
}

static bool Sys_UseBasePathCandidate( const idStr &candidate, const char *label ) {
	if ( !Sys_DirectoryContainsGameDir( candidate ) ) {
		common->Printf( "no '%s' directory in %s path %s, skipping\n", BASE_GAMEDIR, label, candidate.c_str() );
		return false;
	}

	basepath = candidate;
	return true;
}

static bool Sys_UseAppBundleParentBasePathCandidate( const idStr &exeDirectory ) {
	idStr appDirectory;
	idStr packageDirectory;
	if ( !Sys_GetAppBundlePackageRootFromExecutableDirectory( exeDirectory, appDirectory, packageDirectory ) ) {
		return false;
	}

	return Sys_UseBasePathCandidate( packageDirectory, "app parent" );
}

/*
==============
Sys_DefaultSavePath
==============
*/
const char *Sys_DefaultSavePath( void ) {
	idStr home;
	if ( Sys_GetMacOSHomeDirectory( home ) ) {
		idStr candidate = home;
		candidate.AppendPath( "Library" );
		candidate.AppendPath( "Application Support" );
#if defined( ID_DEMO_BUILD )
		candidate.AppendPath( "openQ4 Demo" );
#else
		candidate.AppendPath( "openQ4" );
#endif
		if ( Sys_SetUsableMacOSSavePath( candidate, "Application Support" ) ) {
			return savepath.c_str();
		}
	}

	idStr cwd = Posix_Cwd();
	if ( Sys_SetUsableMacOSSavePath( cwd, "cwd fallback" ) ) {
		return savepath.c_str();
	}

	common->Printf( "WARNING: using current directory as unverified macOS save path fallback\n" );
	savepath = Posix_Cwd();
	return savepath.c_str();
}

/*
==============
Sys_DefaultBasePath
==============
*/
const char *Sys_DefaultBasePath( void ) {
	idStr candidate;
	idStr exeDirectory;

	exeDirectory = Sys_EXEPath();
	if ( exeDirectory.Length() > 0 ) {
		exeDirectory.StripFilename();
		idStr appDirectory;
		idStr appPackageDirectory;
		if ( Sys_GetAppBundlePackageRootFromExecutableDirectory( exeDirectory, appDirectory, appPackageDirectory ) ) {
			Sys_ErrorIfMacOSAppBundlePackageRootIncomplete( appDirectory, appPackageDirectory );
			if ( Sys_UseBasePathCandidate( appPackageDirectory, "app parent" ) ) {
				return basepath.c_str();
			}
		}
		if ( Sys_UseBasePathCandidate( exeDirectory, "exe" ) ) {
			return basepath.c_str();
		}
		if ( Sys_UseAppBundleParentBasePathCandidate( exeDirectory ) ) {
			return basepath.c_str();
		}
	}

	candidate = Posix_Cwd();
	if ( candidate.Cmp( exeDirectory.c_str() ) != 0 && Sys_UseBasePathCandidate( candidate, "cwd" ) ) {
		return basepath.c_str();
	}

	NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
	NSString *bundleParentPath = [bundlePath stringByDeletingLastPathComponent];
	NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
	NSString *bundleProbePaths[] = { bundlePath, bundleParentPath, resourcePath };
	for ( int i = 0; i < 3; ++i ) {
		NSString *probePath = bundleProbePaths[i];
		if ( probePath == nil ) {
			continue;
		}
		const char *fileSystemPath = [probePath fileSystemRepresentation];
		if ( fileSystemPath == NULL || fileSystemPath[0] == '\0' ) {
			continue;
		}
		candidate = fileSystemPath;
		if ( Sys_UseBasePathCandidate( candidate, "bundle" ) ) {
			return basepath.c_str();
		}
	}

	common->Printf( "WARNING: using current directory as fallback base path\n" );
	basepath = Posix_Cwd();
	return basepath.c_str();
}

/*
===============
Sys_GetProcessorId
===============
*/
cpuid_t Sys_GetProcessorId( void ) {
	cpuid_t cpuid = CPUID_GENERIC;
#if defined( __i386__ ) || defined( __x86_64__ )
	cpuid = (cpuid_t)( cpuid | CPUID_INTEL | CPUID_MMX | CPUID_SSE | CPUID_SSE2 | CPUID_SSE3 | CPUID_HTT | CPUID_CMOV | CPUID_FTZ | CPUID_DAZ );
#elif defined( __ppc__ ) || defined( __ppc64__ )
	cpuid = (cpuid_t)( cpuid | CPUID_ALTIVEC );
#endif
	return cpuid;
}

/*
===============
Sys_GetProcessorString
===============
*/
const char *Sys_GetProcessorString( void ) {
#if defined( __aarch64__ ) || defined( __arm64__ )
	return "arm64 CPU";
#elif defined( __i386__ ) || defined( __x86_64__ )
	return "x86 CPU with MMX/SSE/SSE2/SSE3 extensions";
#elif defined( __ppc__ ) || defined( __ppc64__ )
	return "ppc CPU with AltiVec extensions";
#else
	return "generic CPU";
#endif
}

/*
===============
Sys_FPU_EnableExceptions
===============
*/
void Sys_FPU_EnableExceptions( int exceptions ) {
	(void)exceptions;
}

/*
===============
Sys_FPE_handler
===============
*/
void Sys_FPE_handler( int signum, siginfo_t *info, void *context ) {
	(void)signum;
	(void)info;
	(void)context;
}

/*
===============
Sys_GetClockTicks
===============
*/
double Sys_GetClockTicks( void ) {
	return (double)mach_absolute_time();
}

/*
===============
Sys_ClockTicksPerSecond
===============
*/
double Sys_ClockTicksPerSecond( void ) {
	static double ticksPerSecond = 0.0;
	if ( ticksPerSecond == 0.0 ) {
		mach_timebase_info_data_t timebase;
		timebase.numer = 0;
		timebase.denom = 0;
		if ( mach_timebase_info( &timebase ) == KERN_SUCCESS && timebase.numer != 0 && timebase.denom != 0 ) {
			const double nsPerTick = (double)timebase.numer / (double)timebase.denom;
			ticksPerSecond = 1000000000.0 / nsPerTick;
		} else {
			ticksPerSecond = 1000000000.0;
		}
	}
	return ticksPerSecond;
}

/*
===============
Sys_GetApproximateProcessorFrequencyHz

Display-only CPU frequency for the processor summary. Returns 0.0 when
unavailable (e.g. Apple Silicon does not report hw.cpufrequency).
===============
*/
double Sys_GetApproximateProcessorFrequencyHz( void ) {
	uint64_t frequencyHz = 0;
	size_t len = sizeof( frequencyHz );
	if ( sysctlbyname( "hw.cpufrequency", &frequencyHz, &len, NULL, 0 ) == 0 && frequencyHz > 0 ) {
		return (double)frequencyHz;
	}
	return 0.0;
}

/*
================
Sys_GetSystemRam
returns in megabytes
================
*/
int Sys_GetSystemRam( void ) {
	uint64_t memSizeBytes = 0;
	size_t len = sizeof( memSizeBytes );
	if ( sysctlbyname( "hw.memsize", &memSizeBytes, &len, NULL, 0 ) == 0 && memSizeBytes > 0 ) {
		return Sys_RoundSystemRamMegabytes( memSizeBytes, 1024 );
	}
	return 1024;
}

/*
================
Sys_GetVideoRam
returns in megabytes
================
*/
int Sys_GetVideoRam( void ) {
	CGLRendererInfoObj rendererInfo = NULL;
	GLint rendererCount = 0;
	unsigned long maxVRAM = 0;
	const CGDirectDisplayID display = Sys_DisplayToUse();

	if ( display == 0 ) {
		return 0;
	}

	CGLError err = CGLQueryRendererInfo(
		CGDisplayIDToOpenGLDisplayMask( display ),
		&rendererInfo,
		&rendererCount );
	if ( err != kCGLNoError || rendererInfo == NULL ) {
		return 0;
	}

	for ( GLint rendererIndex = 0; rendererIndex < rendererCount; rendererIndex++ ) {
		GLint accelerated = 0;
		err = CGLDescribeRenderer( rendererInfo, rendererIndex, kCGLRPAccelerated, &accelerated );
		if ( err != kCGLNoError || !accelerated ) {
			continue;
		}

		GLint vramMB = 0;
#if defined( kCGLRPVideoMemoryMegabytes )
		err = CGLDescribeRenderer( rendererInfo, rendererIndex, kCGLRPVideoMemoryMegabytes, &vramMB );
		if ( err == kCGLNoError && vramMB > 0 ) {
			if ( static_cast<unsigned long>( vramMB ) > maxVRAM ) {
				maxVRAM = static_cast<unsigned long>( vramMB );
			}
			continue;
		}
#endif
		GLint vramBytes = 0;
		err = CGLDescribeRenderer( rendererInfo, rendererIndex, kCGLRPVideoMemory, &vramBytes );
		if ( err == kCGLNoError && vramBytes > 0 ) {
			const unsigned long fallbackVRAM = static_cast<unsigned long>( vramBytes ) / ( 1024UL * 1024UL );
			if ( fallbackVRAM > maxVRAM ) {
				maxVRAM = fallbackVRAM;
			}
		}
	}

	(void)CGLDestroyRendererInfo( rendererInfo );
	return static_cast<int>( Min( maxVRAM, static_cast<unsigned long>( idMath::INT_MAX ) ) );
}

/*
========================
OSX_GetCPUIdentification
========================
*/
bool OSX_GetCPUIdentification( int& cpuId, bool& oldArchitecture ) {
	cpuId = (int)Sys_GetProcessorId();
	oldArchitecture = false;
	return true;
}

/*
================
OSX_GetVideoCard
================
*/
void OSX_GetVideoCard( int& outVendorId, int& outDeviceId ) {
	outVendorId = -1;
	outDeviceId = -1;
}

/*
=================
Sys_DoPreferences
=================
*/
void Sys_DoPreferences( void ) {
}

/*
==================
Sys_ShutdownSymbols
==================
*/
void Sys_ShutdownSymbols( void ) {
}

/*
====================
Sys_SetClipboardData
====================
*/
#if !defined( USE_SDL3 )
void Sys_SetClipboardData( const char *string ) {
	NSPasteboard *pasteboard = [NSPasteboard generalPasteboard];
	[pasteboard clearContents];

	NSString *clipboardString = @"";
	if ( string != NULL ) {
		clipboardString = [NSString stringWithUTF8String:string];
		if ( clipboardString == nil ) {
			clipboardString = [NSString stringWithCString:string encoding:NSISOLatin1StringEncoding];
		}
		if ( clipboardString == nil ) {
			clipboardString = @"";
		}
	}

	[pasteboard setString:clipboardString forType:NSPasteboardTypeString];
}
#endif

/*
===============
Sys_FPU_SetDAZ
===============
*/
void Sys_FPU_SetDAZ( bool enable ) {
	(void)enable;
}

/*
===============
Sys_FPU_SetFTZ
===============
*/
void Sys_FPU_SetFTZ( bool enable ) {
	(void)enable;
}
