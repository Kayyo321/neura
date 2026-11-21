#pragma once

// The one place that includes the Windows SDK headers. Winsock must come
// before <windows.h>, which otherwise pulls in the obsolete <winsock.h>, and
// <afunix.h> and <mswsock.h> need the Winsock definitions. Include this from
// .cpp files only: public headers of Neura never expose <windows.h>.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <afunix.h>
#include <mswsock.h>
#include <windows.h>
// clang-format on

#endif
