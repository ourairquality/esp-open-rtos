/* LWIP interface to the ESP WLAN MAC layer driver.

   Based on the sample driver in ethernetif.c. Tweaks based on
   examining esp-lwip eagle_if.c and observed behaviour of the ESP
   RTOS liblwip.a.

   libnet80211.a calls into ethernetif_init & ethernetif_input.

 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 *
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Original Author: Simon Goldschmidt
 * Modified by Angus Gratton based on work by @kadamski/Espressif via esp-lwip project.
 */
#include <string.h>
#include "lwip/opt.h"

#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include <lwip/stats.h>
#include <lwip/snmp.h>
#include "netif/etharp.h"
#include "sysparam.h"

/* declared in libnet80211.a */
int8_t sdk_ieee80211_output_pbuf(struct netif *ifp, struct pbuf* pb);

static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
  struct pbuf *q;

  if (p->next) {
      q = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
      if (q == NULL) {
          return ERR_MEM;
      }
      p = q;
  }

  sdk_ieee80211_output_pbuf(netif, p);

  LINK_STATS_INC(link.xmit);

  return ERR_OK;
}


err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  char *hostname = NULL;
  sysparam_get_string("hostname", &hostname);
  if (hostname && strlen(hostname) == 0) {
      free(hostname);
      hostname = NULL;
  }
  netif->hostname = hostname;
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

  // don't touch netif->state here, the field is used internally in the ESP SDK layers
  netif->name[0] = 'e';
  netif->name[1] = 'n';
  netif->output = etharp_output;
  netif->linkoutput = low_level_output;

  /* low_level_init components */
  netif->hwaddr_len = 6;
  /* The hwaddr is set by sdk_wifi_station_start or sdk_wifi_softap_start. */
  netif->mtu = 1500;
  netif->flags = NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

  return ERR_OK;
}

/* called from ieee80211_deliver_data with new IP frames */
void ethernetif_input(struct netif *netif, struct pbuf *p)
{
    struct eth_hdr *ethhdr = p->payload;
  /* examine packet payloads ethernet header */


    switch(htons(ethhdr->type)) {
	/* IP or ARP packet? */
    case ETHTYPE_IP:
    case ETHTYPE_ARP:
//  case ETHTYPE_IPV6:
	/* full packet send to tcpip_thread to process */
	if (netif->input(p, netif)!=ERR_OK)
	{
	    LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
	    pbuf_free(p);
	    p = NULL;
	}
	break;

    default:
	pbuf_free(p);
	p = NULL;
	break;
    }
}

/* Since the pbuf_type definition has changed in lwip v2 and it is used by the
 * sdk when calling pbuf_alloc, the SDK libraries have been modified to rename
 * their references to pbuf_alloc to _pbufalloc allowing the pbuf_type to be
 * rewritten here. Doing this here keeps this hack out of the lwip code, and
 * ensures that this re-writing is only applied to the sdk calls to pbuf_alloc.
 *
 * The only pbuf types used by the SDK are type 0 for PBUF_RAM when writing
 * data, and type 2 for the received data. The receive data path references
 * internal buffer objects that need to be freed with custom code so a custom
 * pbuf allocation type is used for these.
 */
struct pbuf *_pbufalloc(pbuf_layer l, u16_t length, pbuf_type type) {
    if (type == 0) {
        return pbuf_alloc(l, length, PBUF_RAM);
    } else if (type == 2) {
        return pbuf_alloc_reference(NULL, length, PBUF_ALLOC_FLAG_RX | PBUF_TYPE_ALLOC_SRC_MASK_ESP_RX);
    } else {
        LWIP_ASSERT("Unexpected pbuf_alloc type", 0);
        for (;;);
    }
}
