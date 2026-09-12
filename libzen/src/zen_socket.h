/*
** zen_socket.h — thin portability layer over POSIX sockets / winsock2.
**
** Used by builtin_net.cpp and builtin_http.cpp. Socket handles are kept as
** plain int (Windows SOCKET values fit in 32 bits in practice; the cast is
** the same pragmatic choice curl makes).
*/
#ifndef ZEN_SOCKET_H
#define ZEN_SOCKET_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

namespace zen
{
    typedef int zen_ssize;

    /* WSAStartup exactly once, torn down at process exit. */
    inline void zen_sock_startup()
    {
        static bool done = false;
        if (!done)
        {
            WSADATA wsa;
            WSAStartup(MAKEWORD(2, 2), &wsa);
            done = true;
        }
    }

    inline int zen_sock_close(int fd) { return closesocket((SOCKET)fd); }

    inline int zen_sock_set_nonblock(int fd, bool nonblock)
    {
        u_long mode = nonblock ? 1 : 0;
        return ioctlsocket((SOCKET)fd, FIONBIO, &mode);
    }

    /* poll for readability; timeout in ms. >0 = readable, 0 = timeout, <0 = error */
    inline int zen_sock_poll_read(int fd, int timeout_ms)
    {
        WSAPOLLFD pfd;
        pfd.fd = (SOCKET)fd;
        pfd.events = POLLRDNORM;
        pfd.revents = 0;
        return WSAPoll(&pfd, 1, timeout_ms);
    }

    /* SO_RCVTIMEO/SO_SNDTIMEO take a DWORD of milliseconds on Windows */
    inline void zen_sock_set_timeouts(int fd, int timeout_ms)
    {
        DWORD ms = (DWORD)timeout_ms;
        setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof(ms));
        setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&ms, sizeof(ms));
    }
}

/* setsockopt option values are (const char*) on Windows */
#define ZEN_SOCKOPT_CAST(p) ((const char *)(p))
/* no SIGPIPE on Windows */
#define ZEN_MSG_NOSIGNAL 0

#else /* POSIX */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/time.h>

namespace zen
{
    typedef ssize_t zen_ssize;

    inline void zen_sock_startup() {}

    inline int zen_sock_close(int fd) { return ::close(fd); }

    inline int zen_sock_set_nonblock(int fd, bool nonblock)
    {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0)
            return -1;
        if (nonblock)
            flags |= O_NONBLOCK;
        else
            flags &= ~O_NONBLOCK;
        return fcntl(fd, F_SETFL, flags);
    }

    inline int zen_sock_poll_read(int fd, int timeout_ms)
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        return ::poll(&pfd, 1, timeout_ms);
    }

    inline void zen_sock_set_timeouts(int fd, int timeout_ms)
    {
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
}

#define ZEN_SOCKOPT_CAST(p) (p)
#ifdef MSG_NOSIGNAL
#define ZEN_MSG_NOSIGNAL MSG_NOSIGNAL
#else
#define ZEN_MSG_NOSIGNAL 0
#endif

#endif /* _WIN32 */

#endif /* ZEN_SOCKET_H */
