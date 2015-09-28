#include "TCPSocket.hpp"

#include <cstring>

#if M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
#	include <netinet/in.h>
#endif



using namespace setka;



void TCPSocket::Open(const IPAddress& ip, bool disableNaggle){
	if(*this){
		throw setka::Exc("TCPSocket::Open(): socket already opened");
	}

	//create event for implementing Waitable
#if M_OS == M_OS_WINDOWS
	this->createEventForWaitable();
#endif

	this->socket = ::socket(
			ip.host.isIPv4() ? PF_INET : PF_INET6,
			SOCK_STREAM,
			0
		);
	if(this->socket == DInvalidSocket()){
#if M_OS == M_OS_WINDOWS
		this->closeEventForWaitable();
#endif
		throw setka::Exc("TCPSocket::Open(): Couldn't create socket");
	}

	//Disable Naggle algorithm if required
	if(disableNaggle){
		this->disableNaggle();
	}

	this->setNonBlockingMode();

	this->clearAllReadinessFlags();

	//Connecting to remote host
	sockaddr_storage sockAddr;
	
	if(ip.host.isIPv4()){
		sockaddr_in &sa = reinterpret_cast<sockaddr_in&>(sockAddr);
		memset(&sa, 0, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = htonl(ip.host.getIPv4Host());
		sa.sin_port = htons(ip.port);
	}else{
		sockaddr_in6 &sa = reinterpret_cast<sockaddr_in6&>(sockAddr);
		memset(&sa, 0, sizeof(sa));
		sa.sin6_family = AF_INET6;
#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS || (M_OS == M_OS_LINUX && M_OS_NAME == M_OS_NAME_ANDROID)
		sa.sin6_addr.s6_addr[0] = ip.host.quad0() >> 24;
		sa.sin6_addr.s6_addr[1] = (ip.host.quad0() >> 16) & 0xff;
		sa.sin6_addr.s6_addr[2] = (ip.host.quad0() >> 8) & 0xff;
		sa.sin6_addr.s6_addr[3] = ip.host.quad0() & 0xff;
		sa.sin6_addr.s6_addr[4] = ip.host.quad1() >> 24;
		sa.sin6_addr.s6_addr[5] = (ip.host.quad1() >> 16) & 0xff;
		sa.sin6_addr.s6_addr[6] = (ip.host.quad1() >> 8) & 0xff;
		sa.sin6_addr.s6_addr[7] = ip.host.quad1() & 0xff;
		sa.sin6_addr.s6_addr[8] = ip.host.quad2() >> 24;
		sa.sin6_addr.s6_addr[9] = (ip.host.quad2() >> 16) & 0xff;
		sa.sin6_addr.s6_addr[10] = (ip.host.quad2() >> 8) & 0xff;
		sa.sin6_addr.s6_addr[11] = ip.host.quad2() & 0xff;
		sa.sin6_addr.s6_addr[12] = ip.host.quad3() >> 24;
		sa.sin6_addr.s6_addr[13] = (ip.host.quad3() >> 16) & 0xff;
		sa.sin6_addr.s6_addr[14] = (ip.host.quad3() >> 8) & 0xff;
		sa.sin6_addr.s6_addr[15] = ip.host.quad3() & 0xff;

#else
		sa.sin6_addr.__in6_u.__u6_addr32[0] = htonl(ip.host.quad0());
		sa.sin6_addr.__in6_u.__u6_addr32[1] = htonl(ip.host.quad1());
		sa.sin6_addr.__in6_u.__u6_addr32[2] = htonl(ip.host.quad2());
		sa.sin6_addr.__in6_u.__u6_addr32[3] = htonl(ip.host.quad3());
#endif
		sa.sin6_port = htons(ip.port);
	}

	// Connect to the remote host
	if(connect(
			this->socket,
			reinterpret_cast<sockaddr *>(&sockAddr),
			ip.host.isIPv4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6) //NOTE: on Mac OS for some reason the size should be exactly according to AF_INET/AF_INET6
		) == DSocketError())
	{
#if M_OS == M_OS_WINDOWS
		int errorCode = WSAGetLastError();
#elif M_OS == M_OS_LINUX || M_OS == M_OS_MACOSX || M_OS == M_OS_UNIX
		int errorCode = errno;
#else
#	error "Unsupported OS"
#endif
		if(errorCode == DEIntr()){
			//do nothing, for non-blocking socket the connection request still should remain active
		}else if(errorCode == DEInProgress()){
			//do nothing, this is not an error, we have non-blocking socket
		}else{
			std::stringstream ss;
			ss << "TCPSocket::Open(): connect() failed, error code = " << errorCode << ": ";
#if M_COMPILER == M_COMPILER_MSVC
			{
				const size_t msgbufSize = 0xff;
				char msgbuf[msgbufSize];
				strerror_s(msgbuf, msgbufSize, errorCode);
				msgbuf[msgbufSize - 1] = 0;//make sure the string is null-terminated
				ss << msgbuf;
			}
#else
			ss << strerror(errorCode);
#endif
			this->close();
			throw setka::Exc(ss.str());
		}
	}
}



size_t TCPSocket::Send(utki::Buf<const std::uint8_t> buf){
	if(!*this){
		throw setka::Exc("TCPSocket::Send(): socket is not opened");
	}

	this->clearCanWriteFlag();

#if M_OS == M_OS_WINDOWS
	int len;
#else
	ssize_t len;
#endif

	while(true){
		len = send(
				this->socket,
				reinterpret_cast<const char*>(&*buf.begin()),
				buf.size(),
				0
			);
		if(len == DSocketError()){
#if M_OS == M_OS_WINDOWS
			int errorCode = WSAGetLastError();
#else
			int errorCode = errno;
#endif
			if(errorCode == DEIntr()){
				continue;
			}else if(errorCode == DEAgain()){
				//can't send more bytes, return 0 bytes sent
				len = 0;
			}else{
				std::stringstream ss;
				ss << "TCPSocket::Send(): send() failed, error code = " << errorCode << ": ";
#if M_COMPILER == M_COMPILER_MSVC
				{
					const size_t msgbufSize = 0xff;
					char msgbuf[msgbufSize];
					strerror_s(msgbuf, msgbufSize, errorCode);
					msgbuf[msgbufSize - 1] = 0;//make sure the string is null-terminated
					ss << msgbuf;
				}
#else
				ss << strerror(errorCode);
#endif
				throw setka::Exc(ss.str());
			}
		}
		break;
	}//~while

	ASSERT(len >= 0)
	return size_t(len);
}



size_t TCPSocket::Recv(utki::Buf<std::uint8_t> buf){
	//the 'can read' flag shall be cleared even if this function fails to avoid subsequent
	//calls to Recv() because it indicates that there's activity.
	//So, do it at the beginning of the function.
	this->clearCanReadFlag();

	if(!*this){
		throw setka::Exc("TCPSocket::Recv(): socket is not opened");
	}

#if M_OS == M_OS_WINDOWS
	int len;
#else
	ssize_t len;
#endif

	while(true){
		len = recv(
				this->socket,
				reinterpret_cast<char*>(&*buf.begin()),
				buf.size(),
				0
			);
		if(len == DSocketError()){
#if M_OS == M_OS_WINDOWS
			int errorCode = WSAGetLastError();
#else
			int errorCode = errno;
#endif

			if(errorCode == DEIntr()){
				continue;
			}else if(errorCode == DEAgain()){
				//no data available, return 0 bytes received
				len = 0;
			}else{
				std::stringstream ss;
				ss << "TCPSocket::Recv(): recv() failed, error code = " << errorCode << ": ";
#if M_COMPILER == M_COMPILER_MSVC
				{
					const size_t msgbufSize = 0xff;
					char msgbuf[msgbufSize];
					strerror_s(msgbuf, msgbufSize, errorCode);
					msgbuf[msgbufSize - 1] = 0;//make sure the string is null-terminated
					ss << msgbuf;
				}
#else
				ss << strerror(errorCode);
#endif
				throw setka::Exc(ss.str());
			}
		}
		break;
	}//~while

	ASSERT(len >= 0)
	return size_t(len);
}



namespace{

IPAddress CreateIPAddressFromSockaddrStorage(const sockaddr_storage& addr){
	if(addr.ss_family == AF_INET){
		const sockaddr_in &a = reinterpret_cast<const sockaddr_in&>(addr);
		return IPAddress(
			std::uint32_t(ntohl(a.sin_addr.s_addr)),
			std::uint16_t(ntohs(a.sin_port))
		);
	}else{
		ASSERT(addr.ss_family == AF_INET6)
		
		const sockaddr_in6 &a = reinterpret_cast<const sockaddr_in6&>(addr);
		
		return IPAddress(
				IPAddress::Host(
#if M_OS == M_OS_MACOSX || M_OS == M_OS_WINDOWS || (M_OS == M_OS_LINUX && M_OS_NAME == M_OS_NAME_ANDROID)
						(std::uint32_t(a.sin6_addr.s6_addr[0]) << 24) | (std::uint32_t(a.sin6_addr.s6_addr[1]) << 16) | (std::uint32_t(a.sin6_addr.s6_addr[2]) << 8) | std::uint32_t(a.sin6_addr.s6_addr[3]),
						(std::uint32_t(a.sin6_addr.s6_addr[4]) << 24) | (std::uint32_t(a.sin6_addr.s6_addr[5]) << 16) | (std::uint32_t(a.sin6_addr.s6_addr[6]) << 8) | std::uint32_t(a.sin6_addr.s6_addr[7]),
						(std::uint32_t(a.sin6_addr.s6_addr[8]) << 24) | (std::uint32_t(a.sin6_addr.s6_addr[9]) << 16) | (std::uint32_t(a.sin6_addr.s6_addr[10]) << 8) | std::uint32_t(a.sin6_addr.s6_addr[11]),
						(std::uint32_t(a.sin6_addr.s6_addr[12]) << 24) | (std::uint32_t(a.sin6_addr.s6_addr[13]) << 16) | (std::uint32_t(a.sin6_addr.s6_addr[14]) << 8) | std::uint32_t(a.sin6_addr.s6_addr[15])
#else
						std::uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[0])),
						std::uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[1])),
						std::uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[2])),
						std::uint32_t(ntohl(a.sin6_addr.__in6_u.__u6_addr32[3]))
#endif
					),
				std::uint16_t(ntohs(a.sin6_port))
			);
	}
}

}//~namespace



IPAddress TCPSocket::GetLocalAddress(){
	if(!*this){
		throw setka::Exc("Socket::GetLocalAddress(): socket is not valid");
	}

	sockaddr_storage addr;

#if M_OS == M_OS_WINDOWS
	int len = sizeof(addr);
#else
	socklen_t len = sizeof(addr);
#endif

	if(getsockname(this->socket, reinterpret_cast<sockaddr*>(&addr), &len)  == DSocketError()){
		throw setka::Exc("Socket::GetLocalAddress(): getsockname() failed");
	}	

	return CreateIPAddressFromSockaddrStorage(addr);
}



IPAddress TCPSocket::GetRemoteAddress(){
	if(!*this){
		throw setka::Exc("TCPSocket::GetRemoteAddress(): socket is not valid");
	}

	sockaddr_storage addr;

#if M_OS == M_OS_WINDOWS
	int len = sizeof(addr);
#else
	socklen_t len = sizeof(addr);
#endif

	if(getpeername(this->socket, reinterpret_cast<sockaddr*>(&addr), &len) == DSocketError()){
		std::stringstream ss;
		ss << "TCPSocket::GetRemoteAddress(): getpeername() failed: ";
#if M_COMPILER == M_COMPILER_MSVC
		{
			const size_t msgbufSize = 0xff;
			char msgbuf[msgbufSize];
			strerror_s(msgbuf, msgbufSize, WSAGetLastError());
			msgbuf[msgbufSize - 1] = 0;//make sure the string is null-terminated
			ss << msgbuf;
		}
#else
		ss << strerror(errno);
#endif
		throw setka::Exc(ss.str());
	}

	return CreateIPAddressFromSockaddrStorage(addr);
}



#if M_OS == M_OS_WINDOWS
//override
void TCPSocket::SetWaitingEvents(std::uint32_t flagsToWaitFor){
	long flags = FD_CLOSE;
	if((flagsToWaitFor & Waitable::READ) != 0){
		flags |= FD_READ;
		//NOTE: since it is not a TCPServerSocket, FD_ACCEPT is not needed here.
	}
	if((flagsToWaitFor & Waitable::WRITE) != 0){
		flags |= FD_WRITE | FD_CONNECT;
	}
	this->setWaitingEventsForWindows(flags);
}
#endif
