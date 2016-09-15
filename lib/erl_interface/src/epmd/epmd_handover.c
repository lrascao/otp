/*
 * %CopyrightBegin%
 * 
 * Copyright Ericsson AB 1998-2011. All Rights Reserved.
 * 
 * The contents of this file are subject to the Erlang Public License,
 * Version 1.1, (the "License"); you may not use this file except in
 * compliance with the License. You should have received a copy of the
 * Erlang Public License along with this software. If not, it can be
 * retrieved online at http://www.erlang.org/.
 * 
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 * 
 * %CopyrightEnd%
 */

#include "eidef.h"

#ifdef __WIN32__
#include <winsock2.h>
#include <windows.h>
#include <winbase.h>

#elif  VXWORKS
#include <vxWorks.h>
#include <ifLib.h>
#include <sockLib.h>
#include <inetLib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#endif

#include <stdlib.h>
#include <string.h>

#include "ei.h"
#include "ei_internal.h"
#include "ei_epmd.h"
#include "ei_portio.h"
#include "putget.h"

static int ei_epmd_r4_handover (struct in_addr *addr, const char *alive,
			    unsigned ms)
{
  char buf[EPMDBUF];
  char *s = buf;
  int len = strlen(alive) + 1;
  int fd;
  int ntype;
  int port;
  int dist_high, dist_low, proto;
  int res;
#if defined(VXWORKS)
  char ntoabuf[32];
#endif

  if (len > sizeof(buf) - 3)
  {
      erl_errno = ERANGE;
      return -1;
  }

  put16be(s,len);
  put8(s,EI_EPMD_HANDOVER_REQ);
  strcpy(s,alive);

  /* connect to epmd */
  if ((fd = ei_epmd_connect_tmo(addr,ms)) < 0)
  {
      return -1;
  }

  if ((res = ei_write_fill_t(fd, buf, len+2, ms)) != len+2) {
    closesocket(fd);
    erl_errno = (res == -2) ? ETIMEDOUT : EIO;
    return -1;
  }

#ifdef VXWORKS
  /* FIXME use union/macro for level. Correct level? */
  if (ei_tracelevel > 2) {
    inet_ntoa_b(*addr,ntoabuf);
    EI_TRACE_CONN2("ei_epmd_r4_handover",
		   "-> PORT2_REQ alive=%s ip=%s",alive,ntoabuf);
  }
#else
  EI_TRACE_CONN2("ei_epmd_r4_handover",
		 "-> PORT2_REQ alive=%s ip=%s",alive,inet_ntoa(*addr));
#endif

  /* read first two bytes (response type, response) */
  if ((res = ei_read_fill_t(fd, buf, 2, ms)) != 2) {
    EI_TRACE_ERR0("ei_epmd_r4_handover","<- CLOSE");
    erl_errno = (res == -2) ? ETIMEDOUT : EIO;
    closesocket(fd);
    return -2;			/* version mismatch */
  }

  s = buf;
  res = get8(s);

  if (res != EI_EPMD_HANDOVER_RESP) { /* response type */
    EI_TRACE_ERR1("ei_epmd_r4_handover","<- unknown (%d)",res);
    EI_TRACE_ERR0("ei_epmd_r4_handover","-> CLOSE");
    closesocket(fd);
    erl_errno = EIO;
    return -1;
  }

  /* got negative response */
  if ((res = get8(s))) {
    /* got negative response */
    EI_TRACE_ERR1("ei_epmd_r4_handover","<- HANDOVER_RESP result=%d (failure)",res);
    closesocket(fd);
    erl_errno = EIO;
    return -1;
  }

  EI_TRACE_CONN1("ei_epmd_r4_handover","<- HANDOVER_RESP result=%d (ok)",res);

  /* expecting remaining 8 bytes */
  if ((res = ei_read_fill_t(fd,buf,8,ms)) != 8) {
    EI_TRACE_ERR0("ei_epmd_r4_handover","<- CLOSE");
    erl_errno = (res == -2) ? ETIMEDOUT : EIO;
    closesocket(fd);
    return -1;
  }

  s = buf;

  port = get16be(s);
  ntype = get8(s);
  proto = get8(s);
  dist_high = get16be(s);
  dist_low = get16be(s);

  EI_TRACE_CONN5("ei_epmd_r4_handover",
		"   port=%d ntype=%d proto=%d dist-high=%d dist-low=%d",
		port,ntype,proto,dist_high,dist_low);

  /* right network protocol? */
  if (EI_MYPROTO != proto)
  {
      erl_errno = EIO;
      return -1;
  }

  /* is there overlap in our distribution versions? */
  if ((EI_DIST_HIGH < dist_low) || (EI_DIST_LOW > dist_high))
  {
      erl_errno = EIO;
      return -1;
  }

  /* ignore the remaining fields */
  return fd;
}

int ei_epmd_handover(struct in_addr *inaddr, const char *alive)
{
    return ei_epmd_handover_tmo (inaddr, alive, 0);
}

int ei_epmd_handover_tmo(struct in_addr *inaddr, const char *alive, unsigned ms)
{
    return ei_epmd_r4_handover(inaddr, alive, ms);
}

