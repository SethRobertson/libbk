/*
 * $Id: libbk_net.h,v 1.15 2004/12/27 23:59:34 dupuy Exp $
 *
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2003 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Mail <projectbaka@baka.org> for further information
 *
 * --Copyright LIBBK--
 */

/**
 * @file
 * Versions of common network structures
 */

#ifndef _libbk_net_h_
#define _libbk_net_h_

// get OS-dependent versions of headers, plus common defines, etc.

#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/ip_icmp.h>
#include <net/route.h>


#define BK_INET_NET_MATCH(addr1, addr2, mask) (((ntohl(((struct in_addr *)(addr1))->s_addr)) & (ntohl(((struct in_addr *)(mask))->s_addr))) == ((ntohl(((struct in_addr *)(addr2))->s_addr)) & (ntohl(((struct in_addr *)(mask))->s_addr))))



/**
 * OS independent version of ethernet header (thanks linux)
 */
struct baka_etherhdr
{
  u_int8_t  pkt_eth_dst[6];			///< Ethernet Source address
  u_int8_t  pkt_eth_src[6];			///< Ethernet destination address
  u_int16_t pkt_eth_ltype;                      ///< Ethernet network layer type
};



/**
 * OS independent version of IP header (thanks linux)
 */
struct baka_iphdr
{
#ifndef WORDS_BIGENDIAN
  u_int8_t  pkt_ip_hdr_len:4;			///< header length
  u_int8_t  pkt_ip_version:4;			///< version
#else
  u_int8_t  pkt_ip_version:4;			///< version
  u_int8_t  pkt_ip_hdr_len:4;			///< header length
#endif
  u_int8_t  pkt_ip_tos;				///< type of service
  u_int16_t pkt_ip_len;                         ///< Length of packet
  u_int16_t pkt_ip_id;                          ///< Packet identification
#define BAKA_IPHDR_OFFMASK      0x1fff          ///< Offset part of offset field
#define	BAKA_IPHDR_EVIL		0x8000		///< Evil flag
#define	BAKA_IPHDR_DF		0x4000		///< Don't fragment flag
#define	BAKA_IPHDR_MF		0x2000		///< More fragments flag

#define BAKA_IPHDR_FLAGSHIFT    13              ///< How many bits are flags offset
  u_int16_t pkt_ip_frag_offset;                 ///< Fragment offset
  u_int8_t  pkt_ip_ttl;				///< Time to live
  u_int8_t  pkt_ip_proto;			///< Protocol
  u_int16_t pkt_ip_checksum;                    ///< Header checksum
  u_int32_t pkt_ip_src;                         ///< Source address
  u_int32_t pkt_ip_dst;                         ///< Destination address
  u_char pkt_ip_options[0];                     ///< IP options (if any)
};



/**
 * OS independent version of IP header overlay (thanks linux)
 */
struct baka_iphdr_overlay
{
  u_int8_t  pkt_ip_zero;			///< Zero
  u_int8_t  pkt_ip_proto;			///< Protocol
  u_int16_t pkt_ip_len;                         ///< Length of packet
  u_int32_t pkt_ip_src;                         ///< Source address
  u_int32_t pkt_ip_dst;                         ///< Destination address
};



/**
 * OS independent version of TCP header (thanks linux)
 */
struct baka_tcphdr
{
  u_int16_t pkt_tcp_srcport;                    ///< Source port
  u_int16_t pkt_tcp_dstport;                    ///< Destination port
  u_int32_t pkt_tcp_seq;                        ///< Sequence number
  u_int32_t pkt_tcp_ack;                        ///< Acknowledgment number
  u_int16_t pkt_tcp_flags;                      ///< TCP flag field
#define BAKA_TCPHDR_FLAGFIN  0x01               ///< FIN flag
#define BAKA_TCPHDR_FLAGSYN  0x02               ///< SYN flag
#define BAKA_TCPHDR_FLAGRST  0x04               ///< RST flag
#define BAKA_TCPHDR_FLAGPUSH 0x08               ///< PUSH flag
#define BAKA_TCPHDR_FLAGACK  0x10               ///< ACK flag
#define BAKA_TCPHDR_FLAGURG  0x20               ///< Urgent flag
#define BAKA_TCPHDR_FLAGECE  0x40               ///< Explicit Congestion Notification Echo flag
#define BAKA_TCPHDR_FLAGCWR  0x80               ///< Congestion Window Reduce flag
#define BAKA_TCPHDR_FLAGMASK    0xff		///< Header bits (no reserved bits considered)
  // <TODO> Do we really need the following </TODO>
#define BAKA_TCPHDR_LENIENT_FLAGMASK    0x0fff	///< Header bits (includes some reserved)
#define BAKA_TCPHDR_HLENSHIFT   12              ///< Header bits
  u_int16_t pkt_tcp_window_size;                ///< Window size
  u_int16_t pkt_tcp_checksum;                   ///< Header+payload checksum
  u_int16_t pkt_tcp_urgent_pointer;             ///< Urgent Pointer
  u_char    pkt_tcp_options[0];                 ///< Possible tcp options
};



/**
 * OS independent version of UDP header (thanks linux)
 */
struct baka_udphdr
{
  u_int16_t pkt_udp_srcport;                    ///< Source port
  u_int16_t pkt_udp_dstport;                    ///< Destination port
  u_int16_t pkt_udp_length;                     ///< Length of payload
  u_int16_t pkt_udp_checksum;                   ///< Header+payload checksum
};



/**
 * OS independent version of ICMP header (thanks linux)
 */
struct baka_icmphdr
{
  u_int8_t  pkt_icmp_type;			///< Message type
  u_int8_t  pkt_icmp_code;			///< Type sub-code
  u_int16_t pkt_icmp_checksum;                  ///< Header+payload checksum
  u_char    pkt_icmp_payload[0];                ///< Possible ICMP additional data
};

#define	BAKA_ICMP_MINLEN	8				// abs minimum
#define	BAKA_ICMP_TSLEN		(8 + 3 * sizeof (int32_t))	// timestamp
#define	BAKA_ICMP_MASKLEN	12				// address mask

#define BAKA_ICMP_ECHOREPLY	0	// echo reply
#define	BAKA_ICMP_UNREACH	3	// dest unreachable, codes:
#define	BAKA_ICMP_SOURCEQUENCH	4	// packet lost, slow down
#define BAKA_ICMP_REDIRECT	5	// redirect (change route)
#define BAKA_ICMP_ECHO		8	// echo request
#define	BAKA_ICMP_ROUTERADVERT	9	// router advertisement
#define	BAKA_ICMP_ROUTERSOLICIT	10	// router solicitation
#define	BAKA_ICMP_TIMXCEED	11	// time exceeded, code:
#define	BAKA_ICMP_PARAMPROB	12	// ip header bad
#define	BAKA_ICMP_TSTAMP	13	// timestamp request
#define	BAKA_ICMP_TSTAMPREPLY	14	// timestamp reply
#define	BAKA_ICMP_IREQ		15	// information request
#define	BAKA_ICMP_IREQREPLY	16	// information reply
#define	BAKA_ICMP_MASKREQ	17	// address mask request
#define	BAKA_ICMP_MASKREPLY	18	// address mask reply

// UNREACH codes
#define	BAKA_ICMP_UNREACH_NET	            0	// bad net
#define	BAKA_ICMP_UNREACH_HOST	            1	// bad host
#define	BAKA_ICMP_UNREACH_PROTOCOL	    2	// bad protocol
#define	BAKA_ICMP_UNREACH_PORT	            3	// bad port
#define	BAKA_ICMP_UNREACH_NEEDFRAG	    4	// DF caused drop
#define	BAKA_ICMP_UNREACH_SRCFAIL	    5	// src route failed
#define	BAKA_ICMP_UNREACH_NET_UNKNOWN       6	// unknown net
#define	BAKA_ICMP_UNREACH_HOST_UNKNOWN      7	// unknown host
#define	BAKA_ICMP_UNREACH_ISOLATED	    8	// src host isolated
#define	BAKA_ICMP_UNREACH_NET_PROHIB	    9	// net denied
#define	BAKA_ICMP_UNREACH_HOST_PROHIB       10	// host denied
#define	BAKA_ICMP_UNREACH_TOSNET	    11	// bad tos for net
#define	BAKA_ICMP_UNREACH_TOSHOST	    12	// bad tos for host
#define	BAKA_ICMP_UNREACH_FILTER_PROHIB     13	// admin prohib
#define	BAKA_ICMP_UNREACH_HOST_PRECEDENCE   14	// host prec violation
#define	BAKA_ICMP_UNREACH_PRECEDENCE_CUTOFF 15	// prec cutoff

// REDIRECT codes
#define	BAKA_ICMP_REDIRECT_NET		0	// for network
#define	BAKA_ICMP_REDIRECT_HOST		1	// for host
#define	BAKA_ICMP_REDIRECT_TOSNET	2	// for tos and net
#define	BAKA_ICMP_REDIRECT_TOSHOST	3	// for tos and host

// TIMEXCEED codes
#define	BAKA_ICMP_TIMXCEED_INTRANS	0	// ttl==0 in transit
#define	BAKA_ICMP_TIMXCEED_REASS	1	// ttl==0 in reass

// PARAMPROB code
#define	BAKA_ICMP_PARAMPROB_OPTABSENT 1		// req. opt. absent


/**
 * OS independent version of ARP header (thanks linux)
 *
 * Note dynamically sized arrays are:
 * sender_mac (hard_size)
 * sender_ip (proto_size)
 * target_mac (hard_size)
 * target_ip (proto_size)
 */
struct baka_arphdr
{
  u_int16_t pkt_arp_hw_type;                    ///< hardware type
  u_int16_t pkt_arp_proto_type;                 ///< protocol type
  u_int8_t  pkt_arp_hard_size;			///< hardware size
  u_int8_t  pkt_arp_proto_size;			///< protocol size
  u_int16_t pkt_arp_opcode;                     ///< operation
#define BAKA_ARP_OPCODE_REQUEST		1	///< ARP request
#define BAKA_ARP_OPCODE_REPLY		2	///< ARP reply
#define BAKA_ARP_OPCODE_RREQUEST	3	///< RARP request
#define BAKA_ARP_OPCODE_RREPLY		4	///< RARP reply
#define BAKA_ARP_OPCODE_InREQUEST	8	///< InARP request
#define BAKA_ARP_OPCODE_InREPLY		9	///< InARP reply
#define BAKA_ARP_OPCODE_NAK		10	///< (ATM)ARP NAK
  u_char    pkt_arp_payload[0];                 ///< Four dynamically sized arrays
};


/**
 * OS independent if_req structure.
 * Initially supports only those fields which appear to be supported by all OS's
 * <TODO> Get hardware information (at least type and address). </TODO>
 *
 * The bii_avail fields may appear redundant since there is nothing
 * contained there which is not computable from the bii_flags field, but
 * once the hardware information is inserted there will likely be several
 * fields which are not available for any given bii instance but which
 * cannot have that fact determined from bii_fields.
 *
 */
struct bk_interface_info
{
  bk_flags		bii_avail;		///< What non-required fields are available
#define BK_INTINFO_FIELD_BROADCAST	0x1	///< The broadcast field has been filled out
#define BK_INTINFO_FIELD_DSTADDR	0x2	///< The dstaddr field has been filled out.
#define BK_INTINFO_FIELD_HARDWARE	0x4	///< The hardware address field is filled out
  char *		bii_name;		///< Interface name
  struct sockaddr	bii_addr;		///< Protocol address.
  union
  {
    struct sockaddr	bau_dstaddr;		///< Peer protocol address
    struct sockaddr	bau_broadaddr;		///< Protocol broadcast address
  } bii_addr_un;
  struct sockaddr	bii_netmask;		///< Protocol netmask
  struct sockaddr	bii_hwaddr;		///< Hardware address
  short			bii_flags;		///< Basic interface flags
  int 			bii_mtu;		///< Maximum tx pkt size.
  int 			bii_metric;		///< Metric.
};


#define bii_dstaddr 	bii_addr_un.bau_dstaddr
#define bii_broadaddr 	bii_addr_un.bau_broadaddr

typedef void *bk_intinfo_list_t;


/**
 * OS Independent (or, more accurately, All-OS shared) route features
 */
struct bk_route_info
{
  struct sockaddr	bri_dst;		///< Route destination.
  struct sockaddr	bri_mask;		///< Mask to apply to route destination.
  struct sockaddr	bri_gateway;		///< Gateway address;
  u_short		bri_flags;		///< Route flags (eg: RTF_UP)
  const char *		bri_if_name;		///< Interface name.
  int			bri_mask_len;		///< Length of netmask.
};

typedef void *bk_rtinfo_list_t;


/* b_intinfo.c */
extern bk_intinfo_list_t bk_intinfo_list_create(bk_s B, int pos_filter, int neg_filter, bk_flags flags);
extern void bk_intinfo_list_destroy(bk_s B, bk_intinfo_list_t list);
struct bk_interface_info *bk_intinfo_list_minimum(bk_s B, bk_intinfo_list_t list);
struct bk_interface_info *bk_intinfo_list_successor(bk_s B, bk_intinfo_list_t list, struct bk_interface_info *bii);
struct bk_interface_info *bk_intinfo_list_search(bk_s B, bk_intinfo_list_t list, const char *name, bk_flags flags);

/* b_rtinfo.c */
extern bk_rtinfo_list_t bk_rtinfo_list_create(bk_s B, bk_flags flags);
extern void bk_rtinfo_list_destroy(bk_s B, bk_rtinfo_list_t list);
extern struct bk_route_info * bk_rtinfo_list_minimum(bk_s B, bk_rtinfo_list_t list, bk_flags flags);
extern struct bk_route_info *bk_rtinfo_list_successor(bk_s B, bk_rtinfo_list_t list, struct bk_route_info *bri, bk_flags flags);
extern struct bk_route_info *bk_rtinfo_get_route(bk_s B, bk_rtinfo_list_t rtlist, struct in_addr *dst, bk_flags flags);
extern struct bk_route_info *bk_rtinfo_get_route_by_string(bk_s B, bk_rtinfo_list_t rtlist, const char *dst_str, bk_flags flags);


#endif /* _libbk_net_h_ */
