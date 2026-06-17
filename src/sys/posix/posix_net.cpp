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
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <errno.h>
#include <sys/select.h>
#include <net/if.h>
#if defined( MACOS_X ) || defined( __APPLE__ )
#include <ifaddrs.h>
#endif

#include "../../idlib/precompiled.h"

idPort clientPort, serverPort;

idCVar net_ip( "net_ip", "localhost", CVAR_SYSTEM, "local IP address" );
idCVar net_port( "net_port", "", CVAR_SYSTEM | CVAR_INTEGER, "local IP port number" );

typedef struct {
	unsigned long ip;
	unsigned long mask;
} net_interface;

#define 		MAX_INTERFACES	32
int				num_interfaces = 0;
net_interface	netint[MAX_INTERFACES];

static unsigned int Sys_SockaddrIPv4HostOrder( const struct sockaddr *address ) {
	const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( address );
	return ntohl( ipv4->sin_addr.s_addr );
}

static void Sys_PrintSockaddrIPv4( const struct sockaddr *address ) {
	const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( address );
	const unsigned char *ip = reinterpret_cast<const unsigned char *>( &ipv4->sin_addr.s_addr );
	common->Printf( "%u.%u.%u.%u",
		static_cast<unsigned int>( ip[0] ),
		static_cast<unsigned int>( ip[1] ),
		static_cast<unsigned int>( ip[2] ),
		static_cast<unsigned int>( ip[3] ) );
}

static int Sys_KeepSocketFdOutOfStdioRange( int socketFd ) {
	if ( socketFd < 0 || socketFd > STDERR_FILENO ) {
		return socketFd;
	}

	const int duplicateFd = fcntl( socketFd, F_DUPFD, STDERR_FILENO + 1 );
	const int duplicateError = errno;
	close( socketFd );
	if ( duplicateFd == -1 ) {
		common->Printf( "ERROR: socket fd duplicate failed: %s\n", strerror( duplicateError ) );
	}
	return duplicateFd;
}

static bool Sys_ParsePortText( const char *text, int *port ) {
	if ( text == NULL || text[0] == '\0' || port == NULL ) {
		return false;
	}

	char *end = NULL;
	errno = 0;
	const long parsedPort = strtol( text, &end, 10 );
	if ( errno != 0 || end == text || *end != '\0' || parsedPort < 0 || parsedPort > 65535 ) {
		return false;
	}

	*port = static_cast<int>( parsedPort );
	return true;
}

static bool Sys_SplitHostAndPort( const char *src, char *host, size_t hostSize, char *service, size_t serviceSize ) {
	if ( src == NULL || src[0] == '\0' || host == NULL || hostSize == 0 || service == NULL || serviceSize == 0 ) {
		return false;
	}

	service[0] = '\0';

	if ( src[0] == '[' ) {
		const char *endBracket = strchr( src, ']' );
		if ( endBracket == NULL || endBracket == src + 1 ) {
			return false;
		}

		const size_t hostLength = static_cast<size_t>( endBracket - src - 1 );
		if ( hostLength >= hostSize ) {
			return false;
		}
		memcpy( host, src + 1, hostLength );
		host[hostLength] = '\0';

		if ( endBracket[1] == '\0' ) {
			return true;
		}
		if ( endBracket[1] != ':' ) {
			return false;
		}

		int port = 0;
		if ( !Sys_ParsePortText( endBracket + 2, &port ) ) {
			return false;
		}
		idStr::snPrintf( service, serviceSize, "%d", port );
		return true;
	}

	const char *firstColon = strchr( src, ':' );
	const char *lastColon = strrchr( src, ':' );
	if ( firstColon != NULL && firstColon == lastColon ) {
		const size_t hostLength = static_cast<size_t>( firstColon - src );
		if ( hostLength == 0 || hostLength >= hostSize ) {
			return false;
		}
		memcpy( host, src, hostLength );
		host[hostLength] = '\0';

		int port = 0;
		if ( !Sys_ParsePortText( firstColon + 1, &port ) ) {
			return false;
		}
		idStr::snPrintf( service, serviceSize, "%d", port );
		return true;
	}

	idStr::Copynz( host, src, hostSize );
	return true;
}

static void Sys_SetSockaddrPort( struct sockaddr *address, int port ) {
	if ( address->sa_family == AF_INET ) {
		reinterpret_cast<struct sockaddr_in *>( address )->sin_port = htons( static_cast<unsigned short>( port ) );
	} else if ( address->sa_family == AF_INET6 ) {
		reinterpret_cast<struct sockaddr_in6 *>( address )->sin6_port = htons( static_cast<unsigned short>( port ) );
	}
}

static bool Sys_ResolveSockaddr( const char *s, bool doDNSResolve, int family, int socktype, int defaultPort, struct sockaddr_storage *sadr, socklen_t *sadrLen ) {
	char host[NI_MAXHOST];
	char service[NI_MAXSERV];

	if ( sadr == NULL || sadrLen == NULL || !Sys_SplitHostAndPort( s, host, sizeof( host ), service, sizeof( service ) ) ) {
		return false;
	}

	if ( service[0] == '\0' ) {
		idStr::snPrintf( service, sizeof( service ), "%d", Max( 0, defaultPort ) );
	}

	struct addrinfo hints;
	memset( &hints, 0, sizeof( hints ) );
	hints.ai_family = family;
	hints.ai_socktype = socktype;
	hints.ai_flags = doDNSResolve ? 0 : AI_NUMERICHOST;

	struct addrinfo *results = NULL;
	const int error = getaddrinfo( host, service, &hints, &results );
	if ( error != 0 ) {
		return false;
	}

	bool resolved = false;
	for ( const struct addrinfo *result = results; result != NULL; result = result->ai_next ) {
		if ( result->ai_addr == NULL ) {
			continue;
		}
		if ( result->ai_family != AF_INET && result->ai_family != AF_INET6 ) {
			continue;
		}
		if ( result->ai_addrlen > sizeof( *sadr ) ) {
			continue;
		}

		memset( sadr, 0, sizeof( *sadr ) );
		memcpy( sadr, result->ai_addr, result->ai_addrlen );
		*sadrLen = static_cast<socklen_t>( result->ai_addrlen );
		resolved = true;
		break;
	}

	freeaddrinfo( results );
	return resolved;
}

static bool Sys_IsAnyInterfaceName( const char *net_interface ) {
	return net_interface == NULL || net_interface[0] == '\0' || !idStr::Icmp( net_interface, "localhost" );
}

static const char *Sys_SocketFamilyName( int family ) {
	return family == AF_INET6 ? "IPv6" : "IPv4";
}

/*
=============
NetadrToSockadr
=============
*/
static bool NetadrToSockadr( const netadr_t * a, struct sockaddr_storage *s, socklen_t *slen ) {
	if ( a == NULL || s == NULL || slen == NULL ) {
		return false;
	}

	memset( s, 0, sizeof( *s ) );

	if ( a->type == NA_BROADCAST ) {
		struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( s );
		ipv4->sin_family = AF_INET;
		ipv4->sin_port = htons( static_cast<unsigned short>( a->port ) );
		ipv4->sin_addr.s_addr = INADDR_BROADCAST;
		*slen = sizeof( *ipv4 );
		return true;
	} else if ( a->type == NA_IP || a->type == NA_LOOPBACK ) {
		struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( s );
		ipv4->sin_family = AF_INET;
		memcpy( &ipv4->sin_addr.s_addr, a->ip, sizeof( a->ip ) );
		if ( a->type == NA_LOOPBACK ) {
			ipv4->sin_addr.s_addr = htonl( INADDR_LOOPBACK );
		}
		ipv4->sin_port = htons( static_cast<unsigned short>( a->port ) );
		*slen = sizeof( *ipv4 );
		return true;
	} else if ( a->type == NA_IP6 ) {
		struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>( s );
		ipv6->sin6_family = AF_INET6;
		memcpy( &ipv6->sin6_addr, a->ip6, sizeof( a->ip6 ) );
		ipv6->sin6_scope_id = a->scopeId;
		ipv6->sin6_port = htons( static_cast<unsigned short>( a->port ) );
		*slen = sizeof( *ipv6 );
		return true;
	}

	return false;
}

/*
=============
SockadrToNetadr
=============
*/
static bool SockadrToNetadr( const struct sockaddr *s, netadr_t * a ) {
	if ( s == NULL || a == NULL ) {
		return false;
	}

	memset( a, 0, sizeof( *a ) );

	if ( s->sa_family == AF_INET ) {
		const struct sockaddr_in *ipv4 = reinterpret_cast<const struct sockaddr_in *>( s );
		const unsigned int ip = ipv4->sin_addr.s_addr;
		memcpy( a->ip, &ip, sizeof( a->ip ) );
		a->port = ntohs( ipv4->sin_port );
		a->type = ( ntohl( ip ) == INADDR_LOOPBACK ) ? NA_LOOPBACK : NA_IP;
		return true;
	}

	if ( s->sa_family == AF_INET6 ) {
		const struct sockaddr_in6 *ipv6 = reinterpret_cast<const struct sockaddr_in6 *>( s );
		memcpy( a->ip6, &ipv6->sin6_addr, sizeof( a->ip6 ) );
		a->scopeId = ipv6->sin6_scope_id;
		a->port = ntohs( ipv6->sin6_port );
		a->type = NA_IP6;
		return true;
	}

	return false;
}

/*
=============
Sys_StringToAdr
=============
*/
bool Sys_StringToNetAdr( const char *s, netadr_t * a, bool doDNSResolve ) {
	struct sockaddr_storage sadr;
	socklen_t sadrLen;

	if ( !Sys_ResolveSockaddr( s, doDNSResolve, AF_UNSPEC, SOCK_DGRAM, 0, &sadr, &sadrLen ) ) {
		return false;
	}

	return SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadr ), a );
}

/*
=============
Sys_NetAdrToString
=============
*/
const char *Sys_NetAdrToString( const netadr_t a ) {
	static char s[4][128];
	static int index = 0;
	char *buffer = s[index++ & 3];

	if ( a.type == NA_LOOPBACK ) {
		if ( a.port ) {
			idStr::snPrintf( buffer, sizeof( s[0] ), "localhost:%i", a.port );
		} else {
			idStr::snPrintf( buffer, sizeof( s[0] ), "localhost" );
		}
	} else if ( a.type == NA_IP ) {
		idStr::snPrintf( buffer, sizeof( s[0] ), "%i.%i.%i.%i:%i",
			a.ip[0], a.ip[1], a.ip[2], a.ip[3], a.port );
	} else if ( a.type == NA_IP6 ) {
		char addressText[INET6_ADDRSTRLEN];
		if ( inet_ntop( AF_INET6, a.ip6, addressText, sizeof( addressText ) ) == NULL ) {
			idStr::Copynz( addressText, "::", sizeof( addressText ) );
		}
		if ( a.port ) {
			if ( a.scopeId ) {
				idStr::snPrintf( buffer, sizeof( s[0] ), "[%s%%%u]:%i", addressText, a.scopeId, a.port );
			} else {
				idStr::snPrintf( buffer, sizeof( s[0] ), "[%s]:%i", addressText, a.port );
			}
		} else if ( a.scopeId ) {
			idStr::snPrintf( buffer, sizeof( s[0] ), "%s%%%u", addressText, a.scopeId );
		} else {
			idStr::snPrintf( buffer, sizeof( s[0] ), "%s", addressText );
		}
	} else {
		idStr::snPrintf( buffer, sizeof( s[0] ), "bad" );
	}
	return buffer;
}

/*
==================
Sys_IsLANAddress
==================
*/
bool Sys_IsLANAddress( const netadr_t adr ) {
	int i;
	unsigned long ip;

#if ID_NOLANADDRESS
	common->Printf( "Sys_IsLANAddress: ID_NOLANADDRESS\n" );
	return false;
#endif

	if ( adr.type == NA_LOOPBACK ) {
		return true;
	}

	if ( adr.type == NA_IP6 ) {
		struct in6_addr ipv6;
		memcpy( &ipv6, adr.ip6, sizeof( ipv6 ) );
		if ( IN6_IS_ADDR_LOOPBACK( &ipv6 ) || IN6_IS_ADDR_LINKLOCAL( &ipv6 ) ) {
			return true;
		}
#ifdef IN6_IS_ADDR_SITELOCAL
		if ( IN6_IS_ADDR_SITELOCAL( &ipv6 ) ) {
			return true;
		}
#endif
		return ( adr.ip6[0] & 0xfe ) == 0xfc;
	}

	if ( adr.type != NA_IP ) {
		return false;
	}

	if ( !num_interfaces ) {
		return false;	// well, if there's no networking, there are no LAN addresses, right
	}

	for ( i = 0; i < num_interfaces; i++ ) {
		unsigned int packedIP;
		memcpy( &packedIP, adr.ip, sizeof( packedIP ) );
		ip = ntohl( packedIP );
		if( ( netint[i].ip & netint[i].mask ) == ( ip & netint[i].mask ) ) {
			return true;
		}
	}

	return false;
}

/*
===================
Sys_CompareNetAdrBase

Compares without the port
===================
*/
bool Sys_CompareNetAdrBase( const netadr_t a, const netadr_t b ) {
	if ( a.type != b.type ) {
		return false;
	}

	if ( a.type == NA_LOOPBACK ) {
		return true;
	}

	if ( a.type == NA_IP ) {
		if ( a.ip[0] == b.ip[0] && a.ip[1] == b.ip[1] && a.ip[2] == b.ip[2] && a.ip[3] == b.ip[3] ) {
			return true;
		}
		return false;
	}

	if ( a.type == NA_IP6 ) {
		return memcmp( a.ip6, b.ip6, sizeof( a.ip6 ) ) == 0 && a.scopeId == b.scopeId;
	}

	common->Printf( "Sys_CompareNetAdrBase: bad address type\n" );
	return false;
}

/*
====================
NET_InitNetworking
====================
*/
void Sys_InitNetworking(void)
{
	// haven't been able to clearly pinpoint which standards or RFCs define SIOCGIFCONF, SIOCGIFADDR, SIOCGIFNETMASK ioctls
	// it seems fairly widespread, in Linux kernel ioctl, and in BSD .. so let's assume it's always available on our targets

#if defined( MACOS_X ) || defined( __APPLE__ )
	unsigned int ip, mask;
	struct ifaddrs *ifap, *ifp;
	
	num_interfaces = 0;
	
	if( getifaddrs( &ifap ) < 0 ) {
		common->FatalError( "InitNetworking: SIOCGIFCONF error - %s\n", strerror( errno ) );
		return;
	}
	
	for( ifp = ifap; ifp; ifp = ifp->ifa_next ) {
		if ( !( ifp->ifa_flags & IFF_UP ) )
			continue;

		if ( !ifp->ifa_addr )
			continue;

		if ( ifp->ifa_addr->sa_family != AF_INET )
			continue;

		if ( !ifp->ifa_netmask )
			continue;
		
		ip = Sys_SockaddrIPv4HostOrder( ifp->ifa_addr );
		mask = Sys_SockaddrIPv4HostOrder( ifp->ifa_netmask );
		
		if ( ip == INADDR_LOOPBACK ) {
			common->Printf( "loopback\n" );
		} else {
			common->Printf( "IP: " );
			Sys_PrintSockaddrIPv4( ifp->ifa_addr );
			common->Printf( "\nNetMask: " );
			Sys_PrintSockaddrIPv4( ifp->ifa_netmask );
			common->Printf( "\n" );
		}
		if ( num_interfaces < MAX_INTERFACES ) {
			netint[ num_interfaces ].ip = ip;
			netint[ num_interfaces ].mask = mask;
			num_interfaces++;
		} else {
			common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit.\n", MAX_INTERFACES );
		}
	}
	freeifaddrs( ifap );
#else
	int		s;
	char	buf[ MAX_INTERFACES*sizeof( ifreq ) ];
	ifconf	ifc;
	ifreq	*ifr;
	int		ifindex;
	unsigned int ip, mask;

	num_interfaces = 0;

	s = socket( AF_INET, SOCK_DGRAM, 0 );
	if ( s == -1 ) {
		common->Printf( "Sys_InitNetworking: socket failed - %s\n", strerror( errno ) );
		return;
	}
	ifc.ifc_len = MAX_INTERFACES*sizeof( ifreq );
	ifc.ifc_buf = buf;
	if ( ioctl( s, SIOCGIFCONF, &ifc ) < 0 ) {
		close( s );
		common->FatalError( "InitNetworking: SIOCGIFCONF error - %s\n", strerror( errno ) );
		return;
	}
	ifindex = 0;
	while ( ifindex < ifc.ifc_len ) {
		common->Printf( "found interface %s - ", ifc.ifc_buf + ifindex );
		// find the type - ignore interfaces for which we can find we can't get IP and mask ( not configured )
		ifr = (ifreq*)( ifc.ifc_buf + ifindex );
		if ( ioctl( s, SIOCGIFADDR, ifr ) < 0 ) {
			common->Printf( "SIOCGIFADDR failed: %s\n", strerror( errno ) );			
		} else {
			if ( ifr->ifr_addr.sa_family != AF_INET ) {
				common->Printf( "not AF_INET\n" );
			} else {
				ip = Sys_SockaddrIPv4HostOrder( &ifr->ifr_addr );
				if ( ip == INADDR_LOOPBACK ) {
					common->Printf( "loopback\n" );
				} else {
					Sys_PrintSockaddrIPv4( &ifr->ifr_addr );
				}
				if ( ioctl( s, SIOCGIFNETMASK, ifr ) < 0 ) {
					common->Printf( " SIOCGIFNETMASK failed: %s\n", strerror( errno ) );
				} else {
					mask = Sys_SockaddrIPv4HostOrder( &ifr->ifr_addr );
					if ( ip != INADDR_LOOPBACK ) {
						common->Printf( "/" );
						Sys_PrintSockaddrIPv4( &ifr->ifr_addr );
						common->Printf( "\n" );
					}
					if ( num_interfaces < MAX_INTERFACES ) {
						netint[ num_interfaces ].ip = ip;
						netint[ num_interfaces ].mask = mask;
						num_interfaces++;
					} else {
						common->Printf( "Sys_InitNetworking: MAX_INTERFACES(%d) hit.\n", MAX_INTERFACES );
					}
				}
			}
		}
		ifindex += sizeof( ifreq );
	}
	close( s );
#endif
}

/*
====================
IPSocketForFamily
====================
*/
static int IPSocketForFamily( const char *net_interface, int port, int family, netadr_t *bound_to = NULL, bool quiet = false ) {
	if ( family != AF_INET && family != AF_INET6 ) {
		return 0;
	}

	const int bindPort = port == PORT_ANY ? 0 : port;
	const char *interfaceText = Sys_IsAnyInterfaceName( net_interface ) ? "localhost" : net_interface;
	if ( !quiet ) {
		common->Printf( "Opening %s UDP socket: %s:%i\n", Sys_SocketFamilyName( family ), interfaceText, port );
	}

	struct sockaddr_storage address;
	socklen_t addressLen = 0;
	memset( &address, 0, sizeof( address ) );

	if ( Sys_IsAnyInterfaceName( net_interface ) ) {
		if ( family == AF_INET ) {
			struct sockaddr_in *ipv4 = reinterpret_cast<struct sockaddr_in *>( &address );
			ipv4->sin_family = AF_INET;
			ipv4->sin_addr.s_addr = INADDR_ANY;
			ipv4->sin_port = htons( static_cast<unsigned short>( bindPort ) );
			addressLen = sizeof( *ipv4 );
		} else {
			struct sockaddr_in6 *ipv6 = reinterpret_cast<struct sockaddr_in6 *>( &address );
			ipv6->sin6_family = AF_INET6;
			ipv6->sin6_addr = in6addr_any;
			ipv6->sin6_port = htons( static_cast<unsigned short>( bindPort ) );
			addressLen = sizeof( *ipv6 );
		}
	} else {
		if ( !Sys_ResolveSockaddr( net_interface, true, family, SOCK_DGRAM, bindPort, &address, &addressLen ) ) {
			if ( !quiet ) {
				common->Printf( "ERROR: IPSocketForFamily: bad %s interface address '%s'\n", Sys_SocketFamilyName( family ), net_interface );
			}
			return 0;
		}
		Sys_SetSockaddrPort( reinterpret_cast<struct sockaddr *>( &address ), bindPort );
	}

	int newsocket = socket( family, SOCK_DGRAM, IPPROTO_UDP );
	if ( newsocket == -1 ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: %s socket: %s\n", Sys_SocketFamilyName( family ), strerror( errno ) );
		}
		return 0;
	}
	newsocket = Sys_KeepSocketFdOutOfStdioRange( newsocket );
	if ( newsocket == -1 ) {
		return 0;
	}

	int on = 1;
	if ( ioctl( newsocket, FIONBIO, &on ) == -1 ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: %s ioctl FIONBIO: %s\n", Sys_SocketFamilyName( family ), strerror( errno ) );
		}
		close( newsocket );
		return 0;
	}

	if ( family == AF_INET ) {
		if ( setsockopt( newsocket, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char *>( &on ), sizeof( on ) ) == -1 ) {
			if ( !quiet ) {
				common->Printf( "ERROR: IPSocketForFamily: setsockopt SO_BROADCAST: %s\n", strerror( errno ) );
			}
			close( newsocket );
			return 0;
		}
	}

#ifdef IPV6_V6ONLY
	if ( family == AF_INET6 ) {
		if ( setsockopt( newsocket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char *>( &on ), sizeof( on ) ) == -1 ) {
			if ( !quiet ) {
				common->Printf( "ERROR: IPSocketForFamily: setsockopt IPV6_V6ONLY: %s\n", strerror( errno ) );
			}
			close( newsocket );
			return 0;
		}
	}
#endif

	if ( bind( newsocket, reinterpret_cast<const struct sockaddr *>( &address ), addressLen ) == -1 ) {
		if ( !quiet ) {
			common->Printf( "ERROR: IPSocketForFamily: %s bind: %s\n", Sys_SocketFamilyName( family ), strerror( errno ) );
		}
		close( newsocket );
		return 0;
	}

	if ( quiet ) {
		common->Printf( "Opening %s UDP socket: %s:%i\n", Sys_SocketFamilyName( family ), interfaceText, port );
	}

	if ( bound_to ) {
		struct sockaddr_storage boundAddress;
		memset( &boundAddress, 0, sizeof( boundAddress ) );
		socklen_t boundAddressLen = sizeof( boundAddress );
		if ( getsockname( newsocket, reinterpret_cast<struct sockaddr *>( &boundAddress ), &boundAddressLen ) == -1 ) {
			common->Printf( "ERROR: IPSocketForFamily: getsockname: %s\n", strerror( errno ) );
			close( newsocket );
			return 0;
		}
		if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &boundAddress ), bound_to ) ) {
			common->Printf( "ERROR: IPSocketForFamily: unsupported bound address family\n" );
			close( newsocket );
			return 0;
		}
	}

	return newsocket;
}

/*
==================
idPort::idPort
==================
*/
idPort::idPort() {
	netSocket = 0;
	netSocket6 = 0;
	packetsRead = 0;
	bytesRead = 0;
	packetsWritten = 0;
	bytesWritten = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );
}

/*
==================
idPort::~idPort
==================
*/
idPort::~idPort() {
	Close();
}

/*
==================
idPort::Close
==================
*/
void idPort::Close() {
	if ( netSocket ) {
		close( netSocket );
		netSocket = 0;
	}
	if ( netSocket6 ) {
		close( netSocket6 );
		netSocket6 = 0;
	}
	memset( &bound_to, 0, sizeof( bound_to ) );
}

/*
==================
idPort::GetPacket
==================
*/
static bool Net_GetPacketFromSocket( int socketFd, const char *context, netadr_t &net_from, void *data, int &size, int maxSize ) {
	int ret;
	struct sockaddr_storage from;
	socklen_t fromlen = sizeof( from );

	if ( socketFd <= 0 ) {
		return false;
	}

	ret = recvfrom( socketFd, data, maxSize, 0, reinterpret_cast<struct sockaddr *>( &from ), &fromlen );

	if ( ret == -1 ) {
		if (errno == EWOULDBLOCK || errno == ECONNREFUSED) {
			// those commonly happen, don't verbose
			return false;
		}
		common->DPrintf( "%s recvfrom(): %s\n", context, strerror( errno ) );
		return false;
	}

	assert( ret <= maxSize );

	if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &from ), &net_from ) ) {
		common->DPrintf( "%s: unsupported address family\n", context );
		return false;
	}
	size = ret;
	return true;
}

bool idPort::GetPacket( netadr_t &net_from, void *data, int &size, int maxSize ) {
	if ( !netSocket && !netSocket6 ) {
		return false;
	}
	if ( data == NULL || maxSize <= 0 ) {
		size = 0;
		return false;
	}

	if ( Net_GetPacketFromSocket( netSocket, "idPort::GetPacket IPv4", net_from, data, size, maxSize ) ||
			Net_GetPacketFromSocket( netSocket6, "idPort::GetPacket IPv6", net_from, data, size, maxSize ) ) {
		packetsRead++;
		bytesRead += size;
		return true;
	}
	return false;
}

/*
==================
idPort::GetPacketBlocking
==================
*/
bool idPort::GetPacketBlocking( netadr_t &net_from, void *data, int &size, int maxSize, int timeout ) {
	fd_set				set;
	struct timeval		tv;
	int					ret;
	
	if ( !netSocket && !netSocket6 ) {
		return false;
	}
	if ( data == NULL || maxSize <= 0 ) {
		size = 0;
		return false;
	}

	if ( timeout < 0 ) {
		return GetPacket( net_from, data, size, maxSize );
	}

	FD_ZERO( &set );
	int maxSocket = -1;
	if ( netSocket ) {
		FD_SET( netSocket, &set );
		maxSocket = netSocket;
	}
	if ( netSocket6 ) {
		FD_SET( netSocket6, &set );
		if ( netSocket6 > maxSocket ) {
			maxSocket = netSocket6;
		}
	}

	tv.tv_sec = timeout / 1000;
	tv.tv_usec = ( timeout % 1000 ) * 1000;
	ret = select( maxSocket + 1, &set, NULL, NULL, &tv );
	if ( ret == -1 ) {
		if ( errno == EINTR ) {
			common->DPrintf( "idPort::GetPacketBlocking: select EINTR\n" );
			return false;
		} else {
			common->Error( "idPort::GetPacketBlocking: select failed: %s\n", strerror( errno ) );
		}
	}

	if ( ret == 0 ) {
		// timed out
		return false;
	}

	if ( netSocket && FD_ISSET( netSocket, &set ) ) {
		if ( Net_GetPacketFromSocket( netSocket, "idPort::GetPacketBlocking IPv4", net_from, data, size, maxSize ) ) {
			packetsRead++;
			bytesRead += size;
			return true;
		}
	}
	if ( netSocket6 && FD_ISSET( netSocket6, &set ) ) {
		if ( Net_GetPacketFromSocket( netSocket6, "idPort::GetPacketBlocking IPv6", net_from, data, size, maxSize ) ) {
			packetsRead++;
			bytesRead += size;
			return true;
		}
	}
	return false;
}

/*
==================
idPort::SendPacket
==================
*/
void idPort::SendPacket( const netadr_t to, const void *data, int size ) {
	int ret;
	struct sockaddr_storage addr;
	socklen_t addrLen;

	if ( to.type == NA_BAD ) {
		common->Warning( "idPort::SendPacket: bad address type NA_BAD - ignored" );
		return;
	}

	if ( size < 0 || ( data == NULL && size > 0 ) ) {
		common->Warning( "idPort::SendPacket: invalid packet buffer - ignored" );
		return;
	}

	if ( !NetadrToSockadr( &to, &addr, &addrLen ) ) {
		common->Warning( "idPort::SendPacket: bad address type - ignored" );
		return;
	}

	const int socketFd = addr.ss_family == AF_INET6 ? netSocket6 : netSocket;
	if ( !socketFd ) {
		common->Warning( "idPort::SendPacket: no %s socket for %s - ignored", Sys_SocketFamilyName( addr.ss_family ), Sys_NetAdrToString( to ) );
		return;
	}

	ret = sendto( socketFd, data, size, 0, reinterpret_cast<struct sockaddr *>( &addr ), addrLen );
	if ( ret == -1 ) {
		common->Printf( "idPort::SendPacket ERROR: to %s: %s\n", Sys_NetAdrToString( to ), strerror( errno ) );
		return;
	}
	packetsWritten++;
	bytesWritten += size;
}

/*
==================
idPort::InitForPort
==================
*/
bool idPort::InitForPort( int portNumber ) {
	Close();

	const char *interfaceName = net_ip.GetString();
	if ( Sys_IsAnyInterfaceName( interfaceName ) ) {
		netadr_t bound4;
		netadr_t bound6;
		memset( &bound4, 0, sizeof( bound4 ) );
		memset( &bound6, 0, sizeof( bound6 ) );

		netSocket = IPSocketForFamily( interfaceName, portNumber, AF_INET, &bound4 );
		int ipv6Port = portNumber;
		if ( netSocket > 0 ) {
			bound_to = bound4;
			if ( portNumber == PORT_ANY ) {
				ipv6Port = bound4.port;
			}
		}

		netSocket6 = IPSocketForFamily( interfaceName, ipv6Port, AF_INET6, &bound6 );
		if ( netSocket <= 0 && netSocket6 > 0 ) {
			bound_to = bound6;
		}

		if ( netSocket <= 0 && netSocket6 <= 0 ) {
			netSocket = 0;
			netSocket6 = 0;
			memset( &bound_to, 0, sizeof( bound_to ) );
			return false;
		}
		return true;
	}

	const bool prefersIPv6 = strchr( interfaceName, ':' ) != NULL;
	const int firstFamily = prefersIPv6 ? AF_INET6 : AF_INET;
	const int secondFamily = prefersIPv6 ? AF_INET : AF_INET6;

	netadr_t explicitBound;
	memset( &explicitBound, 0, sizeof( explicitBound ) );

	const int firstSocket = IPSocketForFamily( interfaceName, portNumber, firstFamily, &explicitBound, true );
	if ( firstSocket > 0 ) {
		if ( firstFamily == AF_INET6 ) {
			netSocket6 = firstSocket;
		} else {
			netSocket = firstSocket;
		}
		bound_to = explicitBound;
		return true;
	}

	const int secondSocket = IPSocketForFamily( interfaceName, portNumber, secondFamily, &explicitBound );
	if ( secondSocket > 0 ) {
		if ( secondFamily == AF_INET6 ) {
			netSocket6 = secondSocket;
		} else {
			netSocket = secondSocket;
		}
		bound_to = explicitBound;
		return true;
	}

	netSocket = 0;
	netSocket6 = 0;
	memset( &bound_to, 0, sizeof( bound_to ) );
	return false;
}

//=============================================================================

/*
==================
idTCP::idTCP
==================
*/
idTCP::idTCP() {
	fd = 0;
	memset(&address, 0, sizeof(address));
}

/*
==================
idTCP::~idTCP
==================
*/
idTCP::~idTCP() {
	Close();
}

/*
==================
idTCP::Init
==================
*/
bool idTCP::Init( const char *host, short port ) {
	struct sockaddr_storage sadr;
	socklen_t sadrLen;

	if ( !Sys_ResolveSockaddr( host, true, AF_UNSPEC, SOCK_STREAM, port, &sadr, &sadrLen ) ) {
		common->Printf( "Couldn't resolve server name \"%s\"\n", host ? host : "" );
		return false;
	}
	if ( !SockadrToNetadr( reinterpret_cast<const struct sockaddr *>( &sadr ), &address ) ) {
		common->Printf( "Couldn't resolve server name \"%s\" to a supported address family\n", host ? host : "" );
		return false;
	}
	common->Printf( "\"%s\" resolved to %s\n", host ? host : "", Sys_NetAdrToString( address ) );

	if (fd) {
		common->Warning("idTCP::Init: already initialized?\n");
		Close();
	}
		
	if ((fd = socket( sadr.ss_family, SOCK_STREAM, IPPROTO_TCP )) == -1) {
		fd = 0;
		common->Printf("ERROR: idTCP::Init: socket: %s\n", strerror(errno));
		return false;
	}
	fd = Sys_KeepSocketFdOutOfStdioRange( fd );
	if ( fd == -1 ) {
		fd = 0;
		return false;
	}
	
	if ( connect( fd, reinterpret_cast<const sockaddr *>( &sadr ), sadrLen ) == -1 ) {
		common->Printf( "ERROR: idTCP::Init: connect: %s\n", strerror( errno ) );		
		close( fd );
		fd = 0;
		return false;
	}
	
	int status;
	if ((status = fcntl(fd, F_GETFL, 0)) != -1) {
	    status |= O_NONBLOCK; /* POSIX */
	    status = fcntl(fd, F_SETFL, status);
	}
	if (status == -1) {
		common->Printf("ERROR: idTCP::Init: fcntl / O_NONBLOCK: %s\n", strerror(errno));
		close(fd);
		fd = 0;
		return false;
	}
	
	common->DPrintf("Opened TCP connection\n");
	return true;
}

/*
==================
idTCP::Close
==================
*/
void idTCP::Close() {
	if (fd) {
		close(fd);
	}
	fd = 0;
}

/*
==================
idTCP::Read
==================
*/
int idTCP::Read(void *data, int size) {
	int nbytes;
	
	if (!fd) {
		common->Printf("idTCP::Read: not initialized\n");
		return -1;
	}
	if ( size <= 0 ) {
		return 0;
	}
	if ( data == NULL ) {
		common->Printf("idTCP::Read: invalid buffer\n");
		return -1;
	}

#if defined(_GNU_SOURCE)
	// handle EINTR interrupted system call with TEMP_FAILURE_RETRY -  this is probably GNU libc specific
	if ( ( nbytes = TEMP_FAILURE_RETRY( read( fd, data, size ) ) ) == -1 ) {
#else
	do {
	  nbytes = read( fd, data, size );
	} while ( nbytes == -1 && errno == EINTR );
	if ( nbytes == -1 ) {
#endif
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}
		common->Printf("ERROR: idTCP::Read: %s\n", strerror(errno));
		Close();
		return -1;
	}
	
	// a successful read of 0 bytes indicates remote has closed the connection
	if ( nbytes == 0 ) {
		common->DPrintf( "idTCP::Read: read 0 bytes - assume connection closed\n" );
		Close();
		return -1;
	}
	
	return nbytes;
}

/*
==================
idTCP::Write
==================
*/

int	idTCP::Write(void *data, int size) {
	int nbytes;
	
	if ( !fd ) {
		common->Printf( "idTCP::Write: not initialized\n");
		return -1;
	}
	if ( size <= 0 ) {
		return 0;
	}
	if ( data == NULL ) {
		common->Printf( "idTCP::Write: invalid buffer\n" );
		return -1;
	}

#if defined(_GNU_SOURCE)	
	// handle EINTR interrupted system call with TEMP_FAILURE_RETRY -  this is probably GNU libc specific
	#if defined( MSG_NOSIGNAL )
	if ( ( nbytes = TEMP_FAILURE_RETRY( send( fd, data, size, MSG_NOSIGNAL ) ) ) == -1 ) {
	#else
	if ( ( nbytes = TEMP_FAILURE_RETRY( write( fd, data, size ) ) ) == -1 ) {
	#endif
#else
	  do {
	#if defined( MSG_NOSIGNAL )
	    nbytes = send( fd, data, size, MSG_NOSIGNAL );
	#else
	    nbytes = write( fd, data, size );
	#endif
	  } while ( nbytes == -1 && errno == EINTR );
	  if ( nbytes == -1 ) {
#endif
		if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
			return 0;
		}
		common->Printf( "ERROR: idTCP::Write: %s\n", strerror( errno ) );
		Close();
		return -1;
	}

	return nbytes;	
}
