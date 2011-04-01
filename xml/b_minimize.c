#if !defined(lint)
static const char libbk__copyright[] = "Copyright © 2003-2011";
static const char libbk__contact[] = "<projectbaka@baka.org>";
#endif /* not lint */
/*
 * ++Copyright BAKA++
 *
 * Copyright © 2003-2011 The Authors. All rights reserved.
 *
 * This source code is licensed to you under the terms of the file
 * LICENSE.TXT in this release for further details.
 *
 * Send e-mail to <projectbaka@baka.org> for further information.
 *
 * - -Copyright BAKA- -
 */

/**
 * @file
 * Random useful xml search functions, works with libxml2
 */

#include <libbk.h>
#include <libbkxml.h>

static xmlNodePtr minimize_doc(bk_s B, xmlDocPtr doc, xmlNodePtr input, bk_flags flags);

/**
 * Create normalized feature tree.
 *
 * A recursive function which attempts to duplicate the non-decorative
 * parts of the input XML tree.  Rules:
 *
 * - Comments get deleted
 * - Text nodes consisting of whitespace are deleted
 *
 * This function is incomplete and thus TOTALLY BROKEN. It completely ignores
 * attributes (or property lists), so any attributes in the input tree wind up
 * deleted. Fixing this is, now that jtt thinks about it, not actually all the
 * difficult, but since you are probably just trying to "minimize" an xml
 * string, you might want to consider awb_xml_minimize_str() instead.
 *
 * @param B Baka thread/global environment
 * @param input Original featuretree document
 * @param flags Fun for the future
 * @param <i>NULL</i> on error or node and relations are all irrelevant
 * @param <br><i>New doc</i> if node or relations are relevant
 */
xmlDocPtr bkxml_minimize_doc(bk_s B, xmlDocPtr input, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  xmlDocPtr doc = NULL;

  if (!input)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, NULL);
  }

  if (!(doc = xmlNewDoc(input->version)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not create new XML document header\n");
    goto error;
  }

  if (!(doc->children = minimize_doc(B, input, input->children, flags)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not minimize xml document\n");
    goto error;
  }

  BK_RETURN(B,doc);

 error:
  if (doc)
    xmlFreeDoc(doc);

  BK_RETURN(B,NULL);
}





/**
 * Do the work of creating a normalized feature tree.
 *
 * @param B Baka thread/global environment
 * @param input Original featuretree
 * @param flags Fun for the future
 * @param <i>NULL</i> on error or node and relations are all irrelevant
 * @param <br><i>New node</i> if node or relations are relevant
 */
static xmlNodePtr minimize_doc(bk_s B, xmlDocPtr doc, xmlNodePtr input, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  xmlNodePtr new = NULL;
  xmlNodePtr sub = NULL;
  xmlAttrPtr attr;

  if (!input)
  {
    bk_error_printf(B, BK_ERR_ERR, "Invalid argument\n");
    BK_RETURN(B, NULL);
  }

  // Check to see if this node should be nuked or not
  if (input->type == XML_COMMENT_NODE ||
      (input->type == XML_TEXT_NODE &&
       input->content &&
       (int)strlen(input->content) == (int)strspn(input->content, " \t\r\n")))
  {
    // <WARNING>We are assuming these can have no children</WARNING>
    if (input->next)
      BK_RETURN(B, minimize_doc(B, doc, input->next, flags));
    BK_RETURN(B, NULL);
  }

  // Node is relevant, copy it.
  if (!(new = xmlCopyNode(input, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not duplicate node: %s\n",
		    strerror(errno));
    BK_RETURN(B, NULL);
  }

  if (input->children && (sub = minimize_doc(B, doc, input->children, flags)))
  {
    if (!xmlAddChild(new, sub))
    {
      bk_error_printf(B, BK_ERR_ERR, "Could not add child node: %s\n",
		      strerror(errno));
      BK_RETURN(B, NULL);
    }
  }

  if (attr = input->properties)
  {
    while(attr)
    {
      char *tmp;
      if (!(tmp = bkxml_attrnode_data(B, doc, (xmlNodePtr)attr, 0)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not extract attribute node data\n");
	BK_RETURN(B,NULL);
      }

      if (!(xmlNewProp(new, attr->name, tmp)))
      {
	bk_error_printf(B, BK_ERR_ERR, "Could not create attribute copy\n");
	BK_RETURN(B,NULL);
      }

      free(tmp);
      attr = attr->next;
    }
  }

  if (input->next && (sub = minimize_doc(B, doc, input->next, flags)))
  {
    /*
     * Grr, libxml2 doesn't have xmlAddSibList, so we have hacked this up.
     * Note that this is not fully general, we are taking advantage of the fact
     * that we do not have a parent to avoid the parent fixups.  We also are
     * depending that we will never have any existing siblings to worry about.
     */
    new->prev = sub->prev;
    sub->prev = new;
    new->next = sub;
  }

  BK_RETURN(B, new);
}


/**
 * Convert a feature tree to xml (acutally converts *any* doc, but it uses the fetu
 *
 *	@param B BAKA thread/global state.
 *	@param flags Flags for future use.
 *	@return <i>-1</i> on failure.<br>
 *	@return <i>0</i> on success.
 */
int
bkxml_md5_doc(bk_s B, xmlDocPtr input, char **digestp, char **min_textp, bk_flags flags)
{
  BK_ENTRY(B, __FUNCTION__, __FILE__, "libbkxml");
  bk_MD5_CTX context;
  char *min_text = NULL;
  char *digest = NULL;
  xmlDocPtr min_doc = NULL;

  if (!input)
  {
    bk_error_printf(B, BK_ERR_ERR,"Illegal arguments\n");
    BK_RETURN(B, -1);
  }

  if (digestp)
    *digestp = NULL;

  if (min_textp)
    *min_textp = NULL;

  if (!(min_doc = bkxml_minimize_doc(B, input, 0)))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not minimize xml doc\n");
    goto error;
  }

  xmlDocDumpMemory(min_doc, (xmlChar **)&min_text, NULL);
  xmlFreeDoc(min_doc);
  min_doc = NULL;

  if (!min_text)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not convert minimized xml tree to text\n");
    goto error;
  }

  if (!BK_MALLOC_LEN(digest, sizeof(context.digest)*2+1))
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not allocate space for hash output\n");
    goto error;
  }

  bk_MD5Init(B, &context);
  bk_MD5Update(B, &context, min_text, strlen(min_text));
  bk_MD5Final(B, &context);

  if (bk_MD5_extract_printable(B, digest, &context, 0) < 0)
  {
    bk_error_printf(B, BK_ERR_ERR, "Could not extract hash output: %s\n", strerror(errno));
    goto error;
  }

  if (digestp)
    *digestp = digest;
  else
    free(digest);
  digest = NULL;

  if (min_textp)
    *min_textp = min_text;
  else
    xmlFree(min_text);
  min_text = NULL;

  BK_RETURN(B,0);

 error:
  if (min_doc)
    xmlFreeDoc(min_doc);

  if (digest)
    free(digest);

  if (min_text)
    xmlFree(min_text);

  BK_RETURN(B,-1);
}
