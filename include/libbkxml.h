/*
 *
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
 * Public APIs for libbkxml
 */

#ifndef _LIBBKXML_h_
#define _LIBBKXML_h_
#include <libxml/globals.h>


extern int bkxml_nodesearch(bk_s B, xmlNodePtr node, xmlNodePtr *last, xmlNodePtr *found, xmlElementType findtype, const u_char *findname, int depth, bk_flags flags);
#define BKXML_DONT_FIND_THIS_NODE	0x01	///< Don't attempt to look for the findname in the node being handed in
#define BKXML_BREADTH			0x02	///< Perform a breadth-first search
extern xmlAttrPtr bkxml_attrsearch(bk_s B, xmlNodePtr node, const char *findname, bk_flags flags);
extern char *bkxml_attrnode_data(bk_s B, xmlDocPtr doc, xmlNodePtr node, bk_flags flags);
#define BKXML_MISSING_TEXT_ARE_NULL	0x1	///< Missing text for attribute is null (default empty string)
#define BKXML_EXPAND_VARS		0x2	///< Expand environment variables
extern xmlDocPtr bkxml_minimize_doc(bk_s B, xmlDocPtr input, bk_flags flags);
extern int bkxml_md5_doc(bk_s B, xmlDocPtr input, char **digestp, char **min_textp, bk_flags flags);
extern char *bkxml_attrnode_valbyname(bk_s B, xmlNodePtr node, const char *findname, bk_flags flags);
extern int bkxml_check_next_node_name(bk_s B, xmlNodePtr node, const xmlChar *name, xmlNodePtr *nodep, bk_flags flags);
#define BKXML_CHECK_NEXT_NODE_NAME_CHECK_THIS_NODE 0x1 ///< Should we consider this node
#endif /* _LIBBKXML_h_ */
