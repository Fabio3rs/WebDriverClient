#include <cstring> // For memset

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

inline auto free_port() -> int {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return -1;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) ==
        SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    if (listen(sock, 5) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    int addr_len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr *>(&addr), &addr_len) ==
        SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return -1;
    }

    int port = ntohs(addr.sin_port);
    closesocket(sock);
    WSACleanup();
    return port;
#else
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }

    sockaddr_in addr{};
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1) {
        close(sock);
        return -1;
    }

    if (listen(sock, 5) == -1) {
        close(sock);
        return -1;
    }

    socklen_t addr_len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr *>(&addr), &addr_len) ==
        -1) {
        close(sock);
        return -1;
    }

    int port = ntohs(addr.sin_port);
    close(sock);
    return port;
#endif
}
