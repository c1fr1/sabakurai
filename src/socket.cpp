#include "socket.hpp"

#include <iostream>

#ifdef _WIN32
	int initSocket() {
		WSADATA wsaData;
		return WSAStartup(MAKEWORD(1, 1), &wasData);
	}

	int quitSocket() {
		return WSACleanup();
	}

	int closeSocket(CSocket socket) {
		int status = shutdown(socket, SD_BOTH);
		if (status == 0) {
			status = closesocket(socket);
		}
		return status;
	}
#else
	int initSocket() { return 0; }
	int quitSocket() { return 0; }

	int closeSocket(CSocket socket) {
		int status = shutdown(socket, SHUT_RDWR);
		if (status == 0) {
			status = close(socket);
		}
		return status;
	}
#endif

Socket::Socket(int domain, int type, int protocol) :
	socket(::socket(domain, type, protocol)),
	mAddressInfo({}),
	connected(false),
	message(),
	mFlags(),
	mWidth(NONE),
	blocking(true) {
	if (socket == -1) {
		throw std::string("Failed to create socket");
	}
}

auto Socket::addressInfo(const sockaddr_in & addressInfo) -> Socket& {
	mAddressInfo = addressInfo;
	return *this;
}

auto Socket::flags(Flags flags) -> Socket& {
	mFlags = flags;
	return *this;
}

auto Socket::width(Width width) -> Socket& {
	mWidth = width;
	return *this;
}

auto Socket::connect() -> void {
	if (::connect(socket, (sockaddr *) &mAddressInfo, sizeof(sockaddr_in)) == -1) {
		throw std::string("Failed to connect to server");
	}
	connected = true;
}

auto Socket::close() -> void {
	::close(socket);
	connected = false;
}

auto Socket::hasData() -> bool {
	fd_set input;
	FD_ZERO(&input);
	FD_SET(socket, &input);
	return FD_ISSET(socket, &input);
}

auto Socket::read() -> uint8_t {
	char c;
	recv(socket, &c, 1, mFlags.flags);
	return c;
}

auto Socket::operator>>(std::string & string) -> Socket& {
	auto vec = std::vector<char>();
	*this >> vec;
	vec.push_back('\0');
	string = &vec[0];
	return *this;
}

auto Socket::operator>>(std::vector<char> & vector) -> Socket& {
	char buffer[4];
	recv(socket, buffer, mWidth + 1, mFlags.flags);
	auto length = uint32_t( buffer[0] );
	for (auto i = 1; i <= mWidth; ++i) {
		length <<= 8;
		length |= buffer[i];
	}
	std::cout << length << std::endl;
	vector.resize(length);
	recv(socket, &vector[0], length, mFlags.flags);
	return *this;
}

Socket::~Socket() {
	close();
}