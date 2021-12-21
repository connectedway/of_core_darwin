
/*
 * This is not real code but is code that I don't want th throw away
 */
static int routefd ;
#define BLUE_ROUTE_MAX_BUF 1024 ;
static char route_buf[BLUE_ROUTE_MAX_BUF]

BLUE_VOID BlueRouteInit (BLUE_VOID)
{
  routefd = socket (PF_ROUTE, SOCK_RAW, AF_INET) ;
}

static  BLUE_SIZET route_read (BLUE_INT routefd, BLUE_CHAR type, 
			       struct rt_msghdr *rtm, BLUE_SIZET size)
{
  BLUE_SIZET len ;

  do
      {
	len = read (routefd, rtm, size) ;
      } 
  while (len >= sizeof (struct rt_msghdr) &&
	 (rtm->rtm_type != type || rtm->rtm_pid != getpid())) ;

  return (len) ;
}

#define ROUNDUP(a, size) (((a) & ((size)-1)) ? (1 + ((a) | ((size)-1))) : (a))
#define NEXT_SA(ap) ap = \
    (struct sockaddr *) ((caddr_t) ap + \
			 (ap->sa_len ? ROUNDUP (ap->sa_len, sizeof (u_long)) : \
			  sizeof(u_long)))

static void BlueGetRtAddrs (BLUE_INT addrs, struct sockaddr *sa, 
			    struct sockaddr **rti_info)
{
  BLUE_INT i ;
  for (i = 0 ; i < RTAX_MAX; i++)
    {
      if (addrs & (1 << i))
	{
	  rti_info[i] = sa ;
	  NEXT_SA(sa) ;
	}
      else
	rti_info[i] = NULL ;
    }
}

BLUE_VOID BlueRouteGetInterface (BLUE_INADDR *dst, BLUE_INADDR *iface) 
{
  BLUE_INT len ;
  struct sockaddr_in *sin ;
  struct sockaddr *sa ;
  struct rt_msghdr *rtm ;
  struct sockaddr *rti_info[RTAX_MAX] ;

  rtm = (struct rt_msghdr *) route_buf ;

  rtm->rtm_msglen = (sizeof (struct rt_msghdr) + sizeof (struct sockaddr_in)) ;
  rtm->rtm_version = RTM_VERSION ;
  rtm->rtm_type = RTM_GET ;
  rtm->rtm_addrs = RTA_DST ;
  rtm->rtm_pid = getpid() ;
  sin = (struct sockaddr_in *) (rtm + 1) ;
  sin->sin_len = sizeof (struct sockaddr_in) ;
  sin->sin_family = AF_INET ;
  sin->sin_addr.s_addr = dst->addr ;
  write (routefd, rtm, rtm->rtm_msglen) ;
  /*
   * Let's read
   */
  len = route_read (routefd, RTM_GET, getpid(), rtm, BLUE_ROUTE_MAX_BUF) ;
  if (len > sizeof (struct sockaddr))
    {
      sa = (struct sockaddr *) (rtm + 1) ;

      BlueGetRtAddrs (rtm->rtm_addrs, sa, rti_info) ;

      /*
       * So I have the gateway info in rti_info[RTAX_GATEWAY].
       * If this was an AF_INET structure, I'd be done, but 
       * it's not.  It's an AF_LINK structure. I want to return the
       * ip address of that interface.  
       *
       * Or, another way I could do this is say I was given the ip address
       * of the interface that I wanted to check for a match.  How do I
       * convert an IP address of an interface to the AF_LINK address of
       * the interface?
       */
    }
}
