/*
 * $Id: libbkxml.h,v 1.1 2003/03/19 00:03:05 seth Exp $
 *
 * ++Copyright LIBBK++
 * 
 * Copyright (c) 2001,2002 The Authors. All rights reserved.
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
 * Public APIs for libbkxml
 */

#ifndef _LIBBKXML_h_
#define _LIBBKXML_h_
#include <libxml/globals.h>


extern int bkxml_nodesearch(bk_s B, xmlNodePtr node, xmlNodePtr *last, xmlNodePtr *found, xmlElementType findtype, u_char *findname, int depth, bk_flags flags);
#define BKXML_DONT_FIND_THIS_NODE	0x01	///< Don't attempt to look for the findname in the node being handed in
#define BKXML_BREADTH			0x02	///< Perform a breadth-first search
extern xmlAttrPtr bkxml_attrsearch(bk_s B, xmlNodePtr node, char *findname, bk_flags flags);
extern char *bkxml_attrnode_data(bk_s B, xmlDocPtr doc, xmlNodePtr node, bk_flags flags);



#endif /* _LIBBKXML_h_ */
