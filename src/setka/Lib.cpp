#include <utki/config.hpp>

#include "Lib.hpp"
#include "Exc.hpp"
#include "HostNameResolver.hpp"


#if M_OS == M_OS_WINDOWS
#	include <winsock2.h>
#	include <utki/windows.hpp>

#elif M_OS == M_OS_LINUX || M_OS == M_OS_UNIX || M_OS == M_OS_MACOSX
#	include <signal.h>

#else
#	error "Unsupported OS"
#endif



using namespace ting::net;



utki::IntrusiveSingleton<Lib>::T_Instance Lib::instance;



Lib::Lib(){
#if M_OS == M_OS_WINDOWS
	WORD versionWanted = MAKEWORD(2,2);
	WSADATA wsaData;
	if(WSAStartup(versionWanted, &wsaData) != 0 ){
		throw net::Exc("SocketLib::SocketLib(): Winsock 2.2 initialization failed");
	}
#elif M_OS == M_OS_LINUX || M_OS == M_OS_UNIX || M_OS == M_OS_MACOSX
	// SIGPIPE is generated when a remote socket is closed
	void (*handler)(int);
	handler = signal(SIGPIPE, SIG_IGN);
	if(handler != SIG_DFL){
		signal(SIGPIPE, handler);
	}
#else
	#error "Unknown OS"
#endif
}



Lib::~Lib()noexcept{
	//check that there are no active dns lookups and finish the DNS request thread
	HostNameResolver::CleanUp();
	
#if M_OS == M_OS_WINDOWS
	// Clean up windows networking
	if(WSACleanup() == SOCKET_ERROR){
		if(WSAGetLastError() == WSAEINPROGRESS){
			WSACancelBlockingCall();
			WSACleanup();
		}
	}
#elif M_OS == M_OS_LINUX || M_OS == M_OS_UNIX || M_OS == M_OS_MACOSX
	// Restore the SIGPIPE handler
	void (*handler)(int);
	handler = signal(SIGPIPE, SIG_DFL);
	if(handler != SIG_IGN){
		signal(SIGPIPE, handler);
	}
#else
	#error "Unknown OS"
#endif
}
