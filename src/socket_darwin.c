/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>

#if defined(TARGET_OS_MAC)
#include <net/route.h>
#endif

#include <poll.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>

#include "ofc/types.h"
#include "ofc/handle.h"
#include "ofc/libc.h"
#include "ofc/socket.h"
#include "ofc/impl/socketimpl.h"
#include "ofc/net.h"
#include "ofc/net_internal.h"

#include "ofc/heap.h"

/*
 * PSP_Socket - Create a Network Socket.
 *
 * This routine expects the socket to be managed by the statesocket module.
 * It also expects the socket handle to have been allocated.
 *
 * Accepts:
 *    hSock - The socket handle
 *    family - The network family (must be IP)
 *    socktype - SOCK_TYPE_STREAM or SOCK_TYPE_DGRAM
 *    protocol - Unused, must be STATE_NONE
 *
 * Returns:
 *    Status (STATE_SUCCESS or STATE_FAIL)
 */

typedef struct {
    int socket;
    OFC_FAMILY_TYPE family;
    OFC_UINT16 events;
    OFC_UINT16 revents;
    OFC_IPADDR ip;
} OFC_SOCKET_IMPL;

OFC_HANDLE ofc_socket_impl_create(OFC_FAMILY_TYPE family,
                                  OFC_SOCKET_TYPE socktype) {
    OFC_HANDLE hSocket;
    OFC_SOCKET_IMPL *sock;

    int stype;
    int fam;
    int proto;
    int on;

    hSocket = OFC_HANDLE_NULL;
    sock = ofc_malloc(sizeof(OFC_SOCKET_IMPL));

    if (sock != OFC_NULL) {
        sock->family = family;
        sock->revents = 0;
        sock->events = 0;
        if (sock->family == OFC_FAMILY_IP) {
            sock->ip.ip_version = OFC_FAMILY_IP;
            sock->ip.u.ipv4.addr = OFC_INADDR_ANY;
            fam = AF_INET;
        } else {
            sock->ip.ip_version = OFC_FAMILY_IPV6;
            sock->ip.u.ipv6 = ofc_in6addr_any;
            fam = AF_INET6;
        }

        if (socktype == SOCKET_TYPE_STREAM) {
            stype = SOCK_STREAM;
            proto = IPPROTO_TCP;
        } else if (socktype == SOCKET_TYPE_ICMP) {
            stype = SOCK_RAW;
            if (sock->ip.ip_version == OFC_FAMILY_IP)
                proto = IPPROTO_ICMP;
            else
                proto = IPPROTO_ICMPV6;
        } else {
            stype = SOCK_DGRAM;
            proto = IPPROTO_UDP;
        }

        sock->socket = socket(fam, stype, proto);

        if (sock->socket < 0) {
            ofc_printf("socket error: %s, errno %d\n",
                       fam == AF_INET ? "AF_INET" : "AF_INET6",
                       errno);
            ofc_free(sock);
        } else {
            on = OFC_TRUE;
            if (socktype == SOCKET_TYPE_DGRAM) {
                setsockopt(sock->socket, SOL_SOCKET, SO_BROADCAST,
                           (char *) &on, sizeof(on));
            }
            setsockopt(sock->socket, SOL_SOCKET, SO_NOSIGPIPE,
                       (char *) &on, sizeof(on));

            hSocket = ofc_handle_create(OFC_HANDLE_SOCKET_IMPL, sock);
        }
    }
    return (hSocket);
}

OFC_VOID ofc_socket_impl_destroy(OFC_HANDLE hSocket) {
    OFC_SOCKET_IMPL *sock;

    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        if (sock->socket >= 0)
            close(sock->socket);
        ofc_free(sock);
        ofc_handle_destroy(hSocket);
        ofc_handle_unlock(hSocket);
    }
}

/*
 * PSP_Bind - Bind an IP Address and port to a socket
 *
 * Accepts:
 *    hSock - Handle of socket
 *    ip - 32 bit representation of ip address in host order
 *    port - 16 bit representation of the port in host order
 *
 * Returns:
 *    status (STATE_SUCCESS or STATE_FAIL)
 */
static OFC_VOID make_sockaddr(struct sockaddr **mysockaddr,
                              socklen_t *mysocklen,
                              const OFC_IPADDR *ip,
                              OFC_UINT16 port) {
    struct sockaddr_in *mysockaddr_in;
    struct sockaddr_in6 *mysockaddr_in6;
    OFC_INT i;

    if (ip->ip_version == OFC_FAMILY_IP) {
        mysockaddr_in = ofc_malloc(sizeof(struct sockaddr_in));
        ofc_memset(mysockaddr_in, '\0', sizeof(struct sockaddr_in));

        mysockaddr_in->sin_family = AF_INET;
        OFC_NET_STON (&mysockaddr_in->sin_port, 0, port);
        OFC_NET_LTON (&mysockaddr_in->sin_addr.s_addr, 0,
                      ip->u.ipv4.addr);
        *mysockaddr = (struct sockaddr *) mysockaddr_in;
        *mysocklen = sizeof(struct sockaddr_in);
    } else {
        mysockaddr_in6 = ofc_malloc(sizeof(struct sockaddr_in6));
        ofc_memset(mysockaddr_in6, '\0', sizeof(struct sockaddr_in6));

        mysockaddr_in6->sin6_len = sizeof(struct sockaddr_in6);
        mysockaddr_in6->sin6_family = AF_INET6;
        mysockaddr_in6->sin6_scope_id = ip->u.ipv6.scope;

        OFC_NET_STON (&mysockaddr_in6->sin6_port, 0, port);
        for (i = 0; i < 16; i++)
            mysockaddr_in6->sin6_addr.s6_addr[i] =
                    ip->u.ipv6._s6_addr[i];
        *mysockaddr = (struct sockaddr *) mysockaddr_in6;
        *mysocklen = sizeof(struct sockaddr_in6);
    }
}

OFC_VOID unmake_sockaddr(struct sockaddr *mysockaddr,
                         OFC_IPADDR *ip,
                         OFC_UINT16 *port) {
    struct sockaddr_in *mysockaddr_in;
    struct sockaddr_in6 *mysockaddr_in6;
    OFC_INT i;

    if (mysockaddr->sa_family == AF_INET) {
        mysockaddr_in = (struct sockaddr_in *) mysockaddr;
        ip->ip_version = OFC_FAMILY_IP;
        *port = OFC_NET_NTOS (&mysockaddr_in->sin_port, 0);
        ip->u.ipv4.addr = OFC_NET_NTOL (&mysockaddr_in->sin_addr.s_addr, 0);
    } else {
        mysockaddr_in6 = (struct sockaddr_in6 *) mysockaddr;
        if (ip != OFC_NULL) {
            ip->ip_version = OFC_FAMILY_IPV6;
            for (i = 0; i < 16; i++)
                ip->u.ipv6._s6_addr[i] =
                        mysockaddr_in6->sin6_addr.s6_addr[i];
        }
        if (port != OFC_NULL)
            *port = OFC_NET_NTOS (&mysockaddr_in6->sin6_port, 0);
    }
}

OFC_BOOL ofc_socket_impl_bind(OFC_HANDLE hSocket, const OFC_IPADDR *ip,
                              OFC_UINT16 port) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;

    int status;
    struct sockaddr *mysockaddr;
    socklen_t mysocklen;

    ret = OFC_FALSE;

    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        make_sockaddr(&mysockaddr, &mysocklen, ip, port);

        status = bind(sock->socket, mysockaddr, mysocklen);

        if (status == 0)
            ret = OFC_TRUE;
        else {
            OFC_CHAR ip_str[IP6STR_LEN];
            OFC_CHAR errstr[80];
            strerror_r(errno, errstr, 80);
            ofc_printf("Bind Error: %.80s\n", errstr);
            if (mysockaddr->sa_family == AF_INET) {
                struct sockaddr_in *mysockaddr_in;
                mysockaddr_in = (struct sockaddr_in *) mysockaddr;
                inet_ntop(AF_INET, &mysockaddr_in->sin_addr,
                          ip_str, IP6STR_LEN);
                ofc_printf("  family: %d\n"
                           "  port: %d\n"
                           "  addr: %s\n",
                           mysockaddr_in->sin_family,
                           ntohs(mysockaddr_in->sin_port),
                           ip_str);
            } else {
                struct sockaddr_in6 *mysockaddr_in6;
                mysockaddr_in6 = (struct sockaddr_in6 *) mysockaddr;
                inet_ntop(AF_INET6, &mysockaddr_in6->sin6_addr,
                          ip_str, IP6STR_LEN);
                ofc_printf("  len: %d\n"
                           "  family: %d\n"
                           "  port: %d\n"
                           "  flowinfo: 0x%08x\n"
                           "  addr: %s\n"
                           "  scope: 0x%08x\n",
                           mysockaddr_in6->sin6_len,
                           mysockaddr_in6->sin6_family,
                           ntohs(mysockaddr_in6->sin6_port),
                           mysockaddr_in6->sin6_flowinfo,
                           ip_str,
                           mysockaddr_in6->sin6_scope_id);
            }
        }

        ofc_free(mysockaddr);

        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

/*
 * PSP_close - Close a socket
 *
 * Accepts:
 *    hSock - Socket handle to close
 *
 * Returns:
 *    status (STATE_SUCCESS or STATE_FAIL)
 */
OFC_BOOL ofc_socket_impl_close(OFC_HANDLE hSocket) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        close(sock->socket);
        sock->socket = -1;
        ofc_handle_unlock(hSocket);
        ret = OFC_TRUE;
    }

    return (ret);
}

/*
 * PSP_Connect - Connect to a remote ip and port
 *
 * Accepts:
 *    hSock - Handle of socket to connect
 *    ip - 32 bit ip in host order
 *    port - 16 bit port in host order
 *
 * Returns:
 *    status (STATE_SUCCESS or STATE_FAIL)
 */
OFC_BOOL ofc_socket_impl_connect(OFC_HANDLE hSocket,
                                 const OFC_IPADDR *ip, OFC_UINT16 port) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;

    int status;
    struct sockaddr *mysockaddr;
    socklen_t mysocklen;
#if 0
    OFC_CHAR ip_str[IP6STR_LEN] ;
#endif

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        make_sockaddr(&mysockaddr, &mysocklen, ip, port);

        status = connect(sock->socket, mysockaddr, mysocklen);

        if (((status != 0) && (errno == EINPROGRESS)) || (status == 0))
            ret = OFC_TRUE;
#if 0
        else
      if (mysockaddr->sa_family == AF_INET)
        {
          struct sockaddr_in *sockaddrp ;
          sockaddrp = (struct sockaddr_in *) mysockaddr ;

          ofc_printf ("connect error: %s %s(%d), errno %d\n",
               "AF_INET",
               inet_ntop (AF_INET, &sockaddrp->sin_addr,
                      ip_str, IP6STR_LEN),
               sockaddrp->sin_port,
               errno) ;
        }
#endif

        ofc_free(mysockaddr);

        ofc_handle_unlock(hSocket);
    }

    return (ret);
}

/*
 * PSP_Listen - Listen for a connection from remote
 *
 * Accepts:
 *    hSock - Handle of Socket to set to listen
 *    backlog - Number of simultaneous open connections to accept
 *
 * Returns:
 *    status (STATE_SUCCESS or STATE_FAIL)
 */
OFC_BOOL ofc_socket_impl_listen(OFC_HANDLE hSocket, OFC_INT backlog) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;
    int status;

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        status = listen(sock->socket, (int) backlog);
        if (status != -1)
            ret = OFC_TRUE;
        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

/*
 * PSP_Accept - Accept a connection on socket
 * 
 * Accepts:
 *    sock - Pointer to socket structure of listening socket
 *    newsock - Pointer to new socket structure
 *
 * Returns:
 *    status (STATE_SUCCESS or STATE_FAIL)
 */
OFC_HANDLE ofc_socket_impl_accept(OFC_HANDLE hSocket,
                                  OFC_IPADDR *ip, OFC_UINT16 *port) {
    OFC_SOCKET_IMPL *sock;
    OFC_SOCKET_IMPL *newsock;
    OFC_HANDLE hNewSock;

    socklen_t addrlen;
    struct sockaddr *mysockaddr;

    hNewSock = OFC_HANDLE_NULL;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        newsock = ofc_malloc(sizeof(OFC_SOCKET_IMPL));

        addrlen = (OFC_MAX (sizeof(struct sockaddr_in6),
                            sizeof(struct sockaddr_in)));

        mysockaddr = ofc_malloc(addrlen);
        newsock->socket = accept(sock->socket, mysockaddr, &addrlen);

        if (newsock->socket != -1) {
            int on;

            on = OFC_TRUE;
            setsockopt(sock->socket, SOL_SOCKET, SO_NOSIGPIPE,
                       (char *) &on, sizeof(on));
            unmake_sockaddr(mysockaddr, ip, port);
            hNewSock = ofc_handle_create(OFC_HANDLE_SOCKET_IMPL, newsock);
        } else
            ofc_free(newsock);

        ofc_free(mysockaddr);
        ofc_handle_unlock(hSocket);
    }
    return (hNewSock);
}

/*
 * PSP_Reuseaddr - Clean up socket so we use it again
 * 
 * Accepts:
 *    hSock - Socket to set the port reuseable on
 *    onoff - TRUE for on, FALSE for off
 */
OFC_BOOL ofc_socket_impl_reuse_addr(OFC_HANDLE hSocket, OFC_BOOL onoff) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;

    int status;
    int on;

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        on = onoff;
        status = setsockopt(sock->socket, SOL_SOCKET, SO_REUSEPORT,
                            (const char *) &on, sizeof(on));
        if (status != -1)
            ret = OFC_TRUE;
        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

/*
 * PSP_Is_Connected - Test if the socket is connected or not
 *
 * Accepts:
 *     hSock - Socket to test for
 *
 * Returns:
 *   True if connected, false otherwise
 */
OFC_BOOL ofc_socket_impl_connected(OFC_HANDLE hSocket) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;

    int status;
    socklen_t namelen;
    struct sockaddr sa;

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        namelen = sizeof(sa);
        status = getpeername(sock->socket, &sa, &namelen);
        if (status != -1)
            ret = OFC_TRUE;
        ofc_handle_unlock(hSocket);
    }

    return (ret);
}

/*
 * PSP_NoBlock - Set socket block mode
 *
 * Accepts:
 *    hSock - Socket to set the function on 
 *    onoff - Whether setting blocking on or off
 *    option - The option for the command
 *
 * Returns:
 *    status - Success of failure
 */
OFC_BOOL ofc_socket_impl_no_block(OFC_HANDLE hSocket, OFC_BOOL onoff) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;

    int flags;

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        flags = fcntl(sock->socket, F_GETFL);
        if (flags >= 0) {
            fcntl(sock->socket, F_SETFL, flags | O_NONBLOCK);
            ret = OFC_TRUE;
        }
        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

/*
 * PSP_Send - Send data on a socket
 *
 * Accepts:
 *    hSock - Socket to send data on
 *    buf - Pointer to buffer to write
 *    len - Number of bytes to write
 *
 * Returns:
 *    Number of bytes written
 */
OFC_SIZET ofc_socket_impl_send(OFC_HANDLE hSocket, const OFC_VOID *buf,
                               OFC_SIZET len) {
    OFC_SOCKET_IMPL *sock;
    OFC_SIZET ret;

    OFC_SIZET status;

    ret = -1;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        status = send(sock->socket, (const char *) buf, (int) len, 0);
        if ((status == -1) && (errno == EAGAIN))
            ret = 0;
        else if (status >= 0)
            ret = status;
        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

/*
 * PSP_Send_To - Send Data on a datagram socket
 *
 * Accepts:
 *    hSock - socket to write data on
 *    buf - buffer to write
 *    len - number of bytes to write
 *    port - port number to write to (host order)
 *    ip - ip to write to (host order)
 *
 * Returns:
 *    Number of bytes written
 */
OFC_SIZET ofc_socket_impl_sendto(OFC_HANDLE hSocket, const OFC_VOID *buf,
                                 OFC_SIZET len,
                                 const OFC_IPADDR *ip,
                                 OFC_UINT16 port) {
    OFC_SOCKET_IMPL *sock;
    OFC_SIZET ret;

    OFC_SIZET status;
    struct sockaddr *mysockaddr;
    socklen_t mysocklen;

    ret = -1;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        make_sockaddr(&mysockaddr, &mysocklen, ip, port);

        status = sendto(sock->socket, (const char *) buf, (int) len, 0,
                        mysockaddr, mysocklen);

        if ((status == -1) && (errno == EAGAIN))
            ret = 0;
        else if (status >= 0)
            ret = status;
        else {
            OFC_CHAR local_ip[IP6STR_LEN];
            OFC_CHAR remote_ip[IP6STR_LEN];
            OFC_SOCKADDR local;
            OFC_SOCKADDR remote;
            OFC_CHAR errstr[80];

            strerror_r(errno, errstr, 80);
            ofc_printf("Sendto Error: %.80s\n", errstr);
            ofc_socket_impl_get_addresses(hSocket, &local, &remote);

            ofc_ntop(&local.sin_addr, local_ip, IP6STR_LEN);
            ofc_ntop(ip, remote_ip, IP6STR_LEN);

            ofc_printf("  status: %d\n"
                       "  len: %d\n"
                       "  errno: %d\n"
                       "  local ip: %s\n"
                       "  remote ip: %s\n",
                       status, len, errno, local_ip, remote_ip);
        }
        ofc_free(mysockaddr);
        ofc_handle_unlock(hSocket);
    }

    return (ret);
}

/*
 * PSP_Recv - Receive bytes from a socket
 *
 * Accepts:
 *    hSock - Socket to read from
 *    buf - pointer to buffer to store data
 *    len - size of buffer
 *
 * Returns:
 *    number of bytes read
 */
OFC_SIZET ofc_socket_impl_recv(OFC_HANDLE hSocket,
                               OFC_VOID *buf,
                               OFC_SIZET len) {
    OFC_SOCKET_IMPL *sock;
    OFC_SIZET ret;

    OFC_SIZET status;

    sock = ofc_handle_lock(hSocket);
    ret = -1;
    if (sock != OFC_NULL) {
        status = recv(sock->socket, (char *) buf, (int) len, 0);

        if ((status == -1) && (errno == EAGAIN))
            ret = 0;
        else if (status >= 0) {
            ret = status;
        }
        ofc_handle_unlock(hSocket);
    }

    return (ret);
}

/*
 * PSP_Recv_From - Receive bytes from socket, return ip address
 *
 * Accepts:
 *    hSock - socket to read from
 *    buf - where to read data to
 *    len - size of buffer
 *    port - pointer to where to store the port (stored in host order)
 *    ip - Pointer to where to store the ip (stored in host order)
 *
 * Returns:
 *    number of bytes read
 */
OFC_SIZET ofc_socket_impl_recv_from(OFC_HANDLE hSocket,
                                    OFC_VOID *buf,
                                    OFC_SIZET len,
                                    OFC_IPADDR *ip,
                                    OFC_UINT16 *port) {
    OFC_SOCKET_IMPL *sock;
    OFC_SIZET ret;

    struct sockaddr *mysockaddr;
    socklen_t mysize;
    OFC_SIZET status;

    ret = -1;
    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        mysize = (OFC_MAX (sizeof(struct sockaddr_in6),
                           sizeof(struct sockaddr_in)));

        mysockaddr = ofc_malloc(mysize);
        ofc_memset(mysockaddr, '\0', mysize);

        status = recvfrom(sock->socket, (char *) buf, (int) len, 0,
                          mysockaddr, &mysize);

        if ((status == -1) && (errno == EAGAIN))
            ret = 0;
        else if (status >= 0) {
            unmake_sockaddr(mysockaddr, ip, port);
            ret = status;
        }
        ofc_free(mysockaddr);
        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

OFC_VOID ofc_socket_impl_set_event(OFC_HANDLE hSocket,
                                   OFC_UINT16 revents) {
    OFC_SOCKET_IMPL *pSocket;

    pSocket = ofc_handle_lock(hSocket);
    if (pSocket != OFC_NULL) {
        pSocket->revents = revents;
        ofc_handle_unlock(hSocket);
    }
}

OFC_UINT16 ofc_socket_impl_get_event(OFC_HANDLE hSocket) {
    OFC_SOCKET_IMPL *pSocket;
    OFC_UINT16 ret;

    pSocket = ofc_handle_lock(hSocket);
    ret = 0;
    if (pSocket != OFC_NULL) {
        ret = pSocket->events;
        ofc_handle_unlock(hSocket);
    }
    return (ret);
}

int ofc_socket_impl_get_fd(OFC_HANDLE hSocket) {
    OFC_SOCKET_IMPL *pSocket;
    int fd;

    fd = -1;

    pSocket = ofc_handle_lock(hSocket);
    if (pSocket != OFC_NULL) {
        fd = pSocket->socket;
        ofc_handle_unlock(hSocket);
    }
    return (fd);
}

OFC_SOCKET_EVENT_TYPE ofc_socket_impl_test(OFC_HANDLE hSocket) {
    OFC_SOCKET_IMPL *pSocket;
    OFC_SOCKET_EVENT_TYPE EventTest;

    EventTest = 0;

    pSocket = ofc_handle_lock(hSocket);
    if (pSocket != OFC_NULL) {

        if (pSocket->revents & POLLHUP)
            EventTest |= OFC_SOCKET_EVENT_CLOSE;
        if (pSocket->revents & POLLIN)
            EventTest |= (OFC_SOCKET_EVENT_ACCEPT | OFC_SOCKET_EVENT_READ);
        if (pSocket->revents & POLLERR)
            EventTest |= OFC_SOCKET_EVENT_ADDRESSCHANGE;
        if (pSocket->revents & POLLPRI)
            EventTest |= OFC_SOCKET_EVENT_QOS;
        if (pSocket->revents & (POLLRDBAND | POLLWRBAND))
            EventTest |= OFC_SOCKET_EVENT_QOB;
        if (pSocket->revents & POLLOUT)
            EventTest |= OFC_SOCKET_EVENT_WRITE;

        ofc_handle_unlock(hSocket);
    }

    return (EventTest);
}

OFC_BOOL ofc_socket_impl_enable(OFC_HANDLE hSocket,
                                OFC_SOCKET_EVENT_TYPE type) {
    OFC_SOCKET_IMPL *pSocket;
    OFC_INT EventTest;
    OFC_BOOL ret;

    ret = OFC_FALSE;
    pSocket = ofc_handle_lock(hSocket);
    if (pSocket != OFC_NULL) {
        EventTest = 0;

        if (type & OFC_SOCKET_EVENT_CLOSE)
            EventTest |= POLLHUP;
        if (type & OFC_SOCKET_EVENT_ACCEPT)
            EventTest |= POLLIN;
        if (type & OFC_SOCKET_EVENT_ADDRESSCHANGE)
            EventTest |= POLLERR;
        if (type & OFC_SOCKET_EVENT_QOS)
            EventTest |= POLLPRI;
        if (type & OFC_SOCKET_EVENT_QOB)
            EventTest |= (POLLRDBAND | POLLWRBAND);
        if (type & OFC_SOCKET_EVENT_READ)
            EventTest |= POLLIN;
        if (type & OFC_SOCKET_EVENT_WRITE)
            EventTest |= POLLOUT;

        pSocket->events = EventTest;
        ofc_handle_unlock(hSocket);
        ret = OFC_TRUE;
    }

    return (ret);
}

OFC_VOID ofc_socket_impl_set_send_size(OFC_HANDLE hSocket, OFC_INT size) {
    OFC_SOCKET_IMPL *sock;

    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        setsockopt(sock->socket, SOL_SOCKET, SO_SNDBUF,
                   (const char *) &size, sizeof(size));
        ofc_handle_unlock(hSocket);
    }
}

OFC_VOID ofc_socket_impl_set_recv_size(OFC_HANDLE hSocket, OFC_INT size) {
    OFC_SOCKET_IMPL *sock;

    sock = ofc_handle_lock(hSocket);
    if (sock != OFC_NULL) {
        setsockopt(sock->socket, SOL_SOCKET, SO_RCVBUF,
                   (const char *) &size, sizeof(size));
        ofc_handle_unlock(hSocket);
    }
}

OFC_BOOL ofc_socket_impl_get_addresses(OFC_HANDLE hSock,
                                       OFC_SOCKADDR *local,
                                       OFC_SOCKADDR *remote) {
    OFC_SOCKET_IMPL *sock;
    OFC_BOOL ret;
    int darwin_status;
    struct sockaddr *local_sockaddr;
    struct sockaddr *remote_sockaddr;
    socklen_t local_sockaddr_size;
    socklen_t remote_sockaddr_size;

    ret = OFC_FALSE;
    sock = ofc_handle_lock(hSock);
    if (sock != OFC_NULL) {
        local_sockaddr_size = (OFC_MAX (sizeof(struct sockaddr_in6),
                                        sizeof(struct sockaddr_in)));
        local_sockaddr = ofc_malloc(local_sockaddr_size);
        darwin_status = getsockname(sock->socket, local_sockaddr,
                                    &local_sockaddr_size);
        if (darwin_status == 0) {
            if (local_sockaddr->sa_family == AF_INET)
                local->sin_family = OFC_FAMILY_IP;
            else
                local->sin_family = OFC_FAMILY_IPV6;
            unmake_sockaddr(local_sockaddr,
                            &local->sin_addr, &local->sin_port);

            remote_sockaddr_size = (OFC_MAX (sizeof(struct sockaddr_in6),
                                             sizeof(struct sockaddr_in)));
            remote_sockaddr = ofc_malloc(remote_sockaddr_size);

            darwin_status = getpeername(sock->socket,
                                        remote_sockaddr,
                                        &remote_sockaddr_size);
            if (darwin_status == 0) {
                if (remote_sockaddr->sa_family == AF_INET)
                    remote->sin_family = OFC_FAMILY_IP;
                else
                    remote->sin_family = OFC_FAMILY_IPV6;
                unmake_sockaddr(remote_sockaddr, &remote->sin_addr,
                                &remote->sin_port);
                ret = OFC_TRUE;
            }
            ofc_free(remote_sockaddr);
        }
        ofc_free(local_sockaddr);
        ofc_handle_unlock(hSock);
    }
    return (ret);
}

/** \} */
