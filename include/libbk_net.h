/*
 * $Id: libbk_net.h,v 1.1 2002/04/18 13:42:59 seth Exp $
 *
 * ++Copyright LIBBK++
 *
 * Copyright (c) 2001 The Authors.  All rights reserved.
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

#ifndef _LIBBK_NET_h_
#define _LIBBK_NET_h_



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
#define BAKA_TCPHDR_FLAGMASK    0x0fff          ///< Header bits
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




#endif /* _LIBBK_NET_h_ */
