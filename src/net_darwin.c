/* Copyright (c) 2021 Connected Way, LLC. All rights reserved.
 * Use of this source code is governed by a Creative Commons 
 * Attribution-NoDerivatives 4.0 International license that can be
 * found in the LICENSE file.
 */
#include <ofc_darwin/config.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "ofc/core.h"
#include "ofc/types.h"
#include "ofc/config.h"
#include "ofc/libc.h"
#include "ofc/heap.h"
#include "ofc/net.h"
#include "ofc/net_internal.h"

/**
 * \defgroup net_darwin Darwin Network Implementation
 * \ingroup darwin
 */

/** \{ */

OFC_VOID ofc_net_init_impl(OFC_VOID) {
    signal(SIGPIPE, SIG_IGN);
}

OFC_VOID ofc_net_register_config_impl(OFC_HANDLE hEvent) {
}

OFC_VOID ofc_net_unregister_config_impl(OFC_HANDLE hEvent) {
}

static OFC_BOOL match_families(struct ifaddrs *ifaddrp) {
    OFC_BOOL ret;

    ret = OFC_FALSE;
#if defined(OFC_DISCOVER_IPV4)
    if (ifaddrp->ifa_addr->sa_family == AF_INET)
        ret = OFC_TRUE;
#endif
#if defined(OFC_DISCOVER_IPV6)
    if (ifaddrp->ifa_addr->sa_family == AF_INET6) {
        ret = OFC_TRUE;
    }
#endif
    return (ret);
}

OFC_INT ofc_net_interface_count_impl(OFC_VOID) {
    int max_count;
    struct ifaddrs *ifap;
    struct ifaddrs *ifap_index;
    OFC_BOOL ignore;

    max_count = 0;
    if (getifaddrs(&ifap) == 0) {
        /*
         * Count the number of entries
         */
        for (ifap_index = ifap;
             ifap_index != NULL;
             ifap_index = ifap_index->ifa_next) {
            ignore = OFC_FALSE;
#if defined (OFC_DARWIN_IGNORE_EN5)
            if (ofc_strcmp(ifap_index->ifa_name, "en5") == 0)
                ignore = OFC_TRUE;
#endif
            if (!ignore) {
#if defined (OFC_LOOPBACK)
                if ((ifap_index->ifa_flags & IFF_UP) &&
                match_families (ifap_index))
              {
                max_count++ ;
              }
#else
                if (!(ifap_index->ifa_flags & IFF_LOOPBACK) &&
                    (ifap_index->ifa_flags & IFF_UP) &&
                    match_families(ifap_index)) {
                    max_count++;
                }
#endif
            }
        }
        freeifaddrs(ifap);
    }
    return (max_count);
}

OFC_VOID ofc_net_interface_addr_impl(OFC_INT index,
                                     OFC_IPADDR *pinaddr,
                                     OFC_IPADDR *pbcast,
                                     OFC_IPADDR *pmask) {
    int max_count;
    struct ifaddrs *ifap;
    struct ifaddrs *ifap_index;
    struct sockaddr_in *pAddrInet;
    struct sockaddr_in6 *pAddrInet6;
    OFC_BOOL found;
    OFC_INT i;
    OFC_BOOL ignore;

    max_count = 0;

    if (pinaddr != OFC_NULL) {
        pinaddr->ip_version = OFC_FAMILY_IP;
        pinaddr->u.ipv4.addr = OFC_INADDR_NONE;
    }
    if (pbcast != OFC_NULL) {
        pbcast->ip_version = OFC_FAMILY_IP;
        pbcast->u.ipv4.addr = OFC_INADDR_NONE;
    }
    if (pmask != OFC_NULL) {
        pmask->ip_version = OFC_FAMILY_IP;
        pmask->u.ipv4.addr = OFC_INADDR_NONE;
    }

    if (getifaddrs(&ifap) == 0) {
        /*
         * Count the number of entries
         */
        found = OFC_FALSE;
        for (ifap_index = ifap;
             ifap_index != NULL && !found;)
          {
            ignore = OFC_FALSE;
#if defined (OFC_DARWIN_IGNORE_EN5)
            if (ofc_strcmp(ifap_index->ifa_name, "en5") == 0)
                ignore = OFC_TRUE;
#endif
            if (!ignore)
              {
#if defined(OFC_LOOPBACK)
                if ((ifap_index->ifa_flags & IFF_UP) &&
                    match_families(ifap_index))
                  {
                    if (max_count == index)
                      found = OFC_TRUE ;
                    else
                      {
                        max_count++ ;
                        ifap_index = ifap_index->ifa_next ;
                      }
                  }
                else
                  ifap_index = ifap_index->ifa_next;
#else
                if (!(ifap_index->ifa_flags & IFF_LOOPBACK) &&
                    (ifap_index->ifa_flags & IFF_UP) &&
                    match_families(ifap_index))
                  {
                    if (max_count == index)
                      found = OFC_TRUE;
                    else
                      {
                        max_count++;
                        ifap_index = ifap_index->ifa_next;
                      }
                  }
                else
                  ifap_index = ifap_index->ifa_next;
#endif
              }
            else
              ifap_index = ifap_index->ifa_next;
          }

        if (found) {
            if (ifap_index->ifa_addr->sa_family == AF_INET) {
                if (pinaddr != OFC_NULL) {
                    pAddrInet = (struct sockaddr_in *) ifap_index->ifa_addr;
                    pinaddr->ip_version = OFC_FAMILY_IP;
                    pinaddr->u.ipv4.addr =
                            OFC_NET_NTOL (&pAddrInet->sin_addr.s_addr, 0);
                }
                if (pmask != OFC_NULL) {
                    pAddrInet = (struct sockaddr_in *) ifap_index->ifa_netmask;
                    pmask->ip_version = OFC_FAMILY_IP;
                    pmask->u.ipv4.addr =
                            OFC_NET_NTOL (&pAddrInet->sin_addr.s_addr, 0);
                }
                if (pbcast != OFC_NULL) {
                    pAddrInet = (struct sockaddr_in *) ifap_index->ifa_netmask;
                    pbcast->ip_version = OFC_FAMILY_IP;
                    if (!(ifap_index->ifa_flags & IFF_LOOPBACK)) {
                        pbcast->u.ipv4.addr = OFC_INADDR_BROADCAST;
                        pbcast->u.ipv4.addr &=
                                ~OFC_NET_NTOL (&pAddrInet->sin_addr.s_addr, 0);
                        pAddrInet = (struct sockaddr_in *) ifap_index->ifa_addr;
                        pbcast->u.ipv4.addr |=
                                OFC_NET_NTOL (&pAddrInet->sin_addr.s_addr, 0);
                    } else {
                        pbcast->u.ipv4.addr = OFC_INADDR_LOOPBACK;
                    }
                }
            } else if (ifap_index->ifa_addr->sa_family == AF_INET6) {
                OFC_INT scope;

                pAddrInet6 = (struct sockaddr_in6 *) ifap_index->ifa_addr;
                scope = pAddrInet6->sin6_scope_id;

                if (pinaddr != OFC_NULL) {
                    pAddrInet6 = (struct sockaddr_in6 *) ifap_index->ifa_addr;
                    pinaddr->ip_version = OFC_FAMILY_IPV6;
                    for (i = 0; i < 16; i++)
                        pinaddr->u.ipv6._s6_addr[i] =
                                pAddrInet6->sin6_addr.s6_addr[i];
                    pinaddr->u.ipv6.scope = scope;
                }

                if (pmask != OFC_NULL) {
                    pAddrInet6 =
                            (struct sockaddr_in6 *) ifap_index->ifa_netmask;
                    pmask->ip_version = OFC_FAMILY_IPV6;
                    for (i = 0; i < 16; i++)
                        pmask->u.ipv6._s6_addr[i] =
                                pAddrInet6->sin6_addr.s6_addr[i];
                    pmask->u.ipv6.scope = scope;
                }
                if (pbcast != OFC_NULL) {
                    pbcast->ip_version = OFC_FAMILY_IPV6;
                    if (!(ifap_index->ifa_flags & IFF_LOOPBACK)) {
                        pbcast->u.ipv6 = ofc_in6addr_bcast;
                    } else {
                        pbcast->u.ipv6 = ofc_in6addr_loopback;
                    }
                    pbcast->u.ipv6.scope = scope;
                }
            }
        }
        freeifaddrs(ifap);
    }
}

OFC_CORE_LIB OFC_VOID
ofc_net_interface_wins_impl(OFC_INT index, OFC_INT *num_wins,
                            OFC_IPADDR **winslist) {
    /*
     * This is not provided by the platform
     */
    if (num_wins != OFC_NULL)
        *num_wins = 0;
    if (winslist != OFC_NULL)
        *winslist = OFC_NULL;
}

OFC_VOID ofc_net_resolve_dns_name_impl(OFC_LPCSTR name,
                                       OFC_UINT16 *num_addrs,
                                       OFC_IPADDR *ip) {
    struct addrinfo *res;
    struct addrinfo *p;
    struct addrinfo hints;
    int ret;

    OFC_INT i;
    OFC_INT j;
    OFC_IPADDR temp;

    ofc_memset((OFC_VOID *) &hints, 0, sizeof(hints));

#if defined(OFC_DISCOVER_IPV6)
#if defined(OFC_DISCOVER_IPV4)
    hints.ai_family = AF_UNSPEC;
#else
    hints.ai_family = AF_INET6 ;
#endif
#else
#if defined(OFC_DISCOVER_IPV4)
    hints.ai_family = AF_INET ;
#else
#error "Neither IPv4 nor IPv6 Configured"
#endif
#endif
    hints.ai_socktype = 0;
    hints.ai_flags = AI_ADDRCONFIG;

    if (ofc_pton(name, &temp) != 0)
        hints.ai_flags |= AI_NUMERICHOST;

    res = NULL;
    ret = getaddrinfo(name, NULL, &hints, &res);

    if (ret != 0)
        *num_addrs = 0;
    else {
        for (i = 0, p = res; p != NULL && i < *num_addrs; i++, p = p->ai_next) {
            if (p->ai_family == AF_INET) {
                struct sockaddr_in *sa;
                sa = (struct sockaddr_in *) p->ai_addr;

                ip[i].ip_version = OFC_FAMILY_IP;
                ip[i].u.ipv4.addr = OFC_NET_NTOL (&sa->sin_addr.s_addr, 0);
            } else if (p->ai_family == AF_INET6) {
                struct sockaddr_in6 *sa6;
                sa6 = (struct sockaddr_in6 *) p->ai_addr;

                ip[i].ip_version = OFC_FAMILY_IPV6;
                for (j = 0; j < 16; j++) {
                    ip[i].u.ipv6._s6_addr[j] =
                            sa6->sin6_addr.s6_addr[j];
                }
                ip[i].u.ipv6.scope = sa6->sin6_scope_id;
            }
        }
        freeaddrinfo(res);
        *num_addrs = i;
    }
}

OFC_CORE_LIB OFC_VOID
ofc_net_set_handle_impl(OFC_UINT64 network_handle)
{
}
/** \} */
