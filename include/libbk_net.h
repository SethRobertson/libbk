/*
 * $Id: libbk_net.h,v 1.3 2002/10/18 18:50:14 dupuy Exp $
 *
 * ++Copyright LIBBK++
 * 
 * Copyright (c) 2002 The Authors. All rights reserved.
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
#if BYTE_ORDER == LITTLE_ENDIAN
  u_int8_t  pkt_ip_hdr_len:4;			///< header length
  u_int8_t  pkt_ip_version:4;			///< version
#endif
#if BYTE_ORDER == BIG_ENDIAN
  u_int8_t  pkt_ip_version:4;			///< version
  u_int8_t  pkt_ip_hdr_len:4;			///< header length
#endif
  u_int8_t  pkt_ip_tos;				///< type of service
  u_int16_t pkt_ip_len;                         ///< Length of packet
  u_int16_t pkt_ip_id;                          ///< Packet identification
#define BAKA_IPHDR_OFFMASK      0x1fff          ///< Offset part of offset field
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
  u_int32_t pkt_ip_src;                         ///< Source address
  u_int32_t pkt_ip_dst;                         ///< Destination address
  u_int8_t  pkt_ip_zero;			///< Zero
  u_int8_t  pkt_ip_proto;			///< Protocol
  u_int16_t pkt_ip_len;                         ///< Length of packet
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
#define BAKA_TCPHDR_FLAGMASK    0x3f		///< Header bits (no reserved bits considered)
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
  u_int16_t pkt_udp_checksum;                   ///< Header+payload checksum
  u_int16_t pkt_udp_length;                     ///< Length of payload
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
 */
struct baka_arphdr
{
  u_int16_t pkt_arp_hw_type;                    ///< hardware type
  u_int16_t pkt_arp_proto_type;                 ///< protocol type
  u_int8_t  pkt_arp_hard_size;			///< hardware size
  u_int8_t  pkt_arp_proto_size;			///< protocol size
  u_int16_t pkt_arp_opcode;                     ///< operation
  u_char    pkt_arp_payload[0];                 ///< Four dynamically sized arrays
};

#endif /* _libbk_net_h_ */
