/*
 * Implementation of prep_article(), which cleans up an article candidate
 *
 * Copyright (C) 2020 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 *
 * Based on Mozilla's Readability.js, available at:
 * https://github.com/mozilla/readability/
 * Original copyright notice:
 *
 * Copyright (c) 2010 Arc90 Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <math.h>
#include <libxml/tree.h>
#include "rdrview.h"

/**
 * Extract a reasonable positive number from the attribute; if unable, return 0
 */
static unsigned int attr_num(htmlNodePtr node, const char *attrname)
{
	xmlChar *value = xmlGetProp(node, (xmlChar *)attrname);
	int ret = 0;

	if (value)
		ret = atoi((char *)value);
	xmlFree(value);
	return ret; /* Insane values are harmless */
}

/**
 * Is this a nonempty table caption?
 */
static bool is_table_caption(htmlNodePtr node)
{
	return node_has_tag(node, "caption") && node->children;
}

/**
 * Does this node appear to be table data?
 */
static bool is_table_data(htmlNodePtr node)
{
	static const char * const tags[] = {
		"col", "colgroup", "tfoot", "thead", "th"
	};

	return node_has_tag_array(node, ARRAY_SIZE(tags), tags);
}

/**
 * Is this node a table?
 */
static bool is_table(htmlNodePtr node)
{
	return node_has_tag(node, "table");
}

/**
 * Count the number of times a given character appears in a node's content
 */
static int content_char_count(htmlNodePtr node, char c)
{
	xmlChar *text = xmlNodeGetContent(node);
	int count;

	count = char_count((char *)text, c);
	xmlFree(text);
	return count;
}

static const char * const PRESENTATIONAL_ATTRS[] = {
	"align", "background", "bgcolor", "border", "cellpadding", "cellspacing",
	"frame", "hspace", "rules", "style", "valign", "vspace",
};

static const char * const DEPRECATED_SIZE_ELEMS[] = {
	"table", "th", "td", "hr", "pre",
};

/**
 * fx: Remove the style attribute on every e and under.
 */
static void clean_styles(htmlNodePtr node)
{
	htmlNodePtr last, curr;

	if (!node)
		return;

	/* Firefox uses recursion here, we process all descendants in a loop */
	curr = node;
	last = skip_node_descendants(node);
	while (curr != last) {
		int i, len;

		if (curr->type != XML_ELEMENT_NODE || node_has_tag(curr, "svg")) {
			curr = skip_node_descendants(curr);
			continue;
		}

		len = ARRAY_SIZE(PRESENTATIONAL_ATTRS);
		for (i = 0; i < len; ++i)
			xmlRemoveProp(xmlHasProp(curr, (xmlChar *)PRESENTATIONAL_ATTRS[i]));

		len = ARRAY_SIZE(DEPRECATED_SIZE_ELEMS);
		if (node_has_tag_array(curr, len, DEPRECATED_SIZE_ELEMS)) {
			xmlRemoveProp(xmlHasProp(curr, (xmlChar *)"width"));
			xmlRemoveProp(xmlHasProp(curr, (xmlChar *)"height"));
		}

		curr = following_node(curr);
	}
}

struct table_size {
	unsigned int rows;
	unsigned int columns;
};

/**
 * Get the row and column count for a table
 */
static struct table_size get_table_size(htmlNodePtr table)
{
	struct table_size ts = {0};
	htmlNodePtr last, curr;

	curr = following_node(table);
	last = skip_node_descendants(table);
	while (curr != last) {
		unsigned int rowspan;
		unsigned int cols_in_row = 0;
		htmlNodePtr child;

		if (!node_has_tag(curr, "tr")) {
			curr = following_node(curr);
			continue;
		}
		rowspan = attr_num(curr, "rowspan");
		ts.rows += rowspan ? rowspan : 1;

		/* fx: Now look for column-related info */
		for (child = curr->children; child; child = child->next) {
			unsigned int colspan;

			if (!node_has_tag(child, "td"))
				continue;
			colspan = attr_num(curr, "colspan");
			cols_in_row += colspan ? colspan : 1;
		}
		ts.columns = MAX(ts.columns, cols_in_row);

		curr = skip_node_descendants(curr);
	}
	return ts;
}

/**
 * If the node is a data (non-layout) table, mark it as such.
 *
 * fx: [...] we use similar checks as
 * https://dxr.mozilla.org/mozilla-central/rev/71224049c0b52ab190564d3ea0eab089a159a4cf/accessible/html/HTMLTableAccessible.cpp#920
 */
static htmlNodePtr mark_if_data_table(htmlNodePtr node)
{
	struct table_size ts;

	if (node->type != XML_ELEMENT_NODE || !node_has_tag(node, "table"))
		return node;

	/* If not a data table, do nothing: the flag is unset by default */
	if (attrcmp(node, "role", "presentation"))
		return node;
	if (attrcmp(node, "datatable", "0"))
		return node;

	if (xmlHasProp(node, (xmlChar *)"summary"))
		goto hit;
	if (has_such_descendant(node, is_table_caption))
		goto hit;
	if (has_such_descendant(node, is_table_data))
		goto hit;

	/* fx: Nested tables indicate a layout table */
	if (has_such_descendant(node, is_table))
		return node;

	ts = get_table_size(node);
	if (ts.rows >= 10 || ts.columns > 4 || ts.rows * ts.columns > 10)
		goto hit;
	return node;

hit:
	mark_as_data_table(node);
	return node;
}

/**
 * Could this image's base64-encoded src be meaningless?
 */
static bool image_src_is_meaningless(htmlNodePtr img)
{
	xmlChar *src = xmlGetProp(img, (xmlChar *)"src");
	xmlAttr *attr;
	const xmlChar *base64;
	bool ret = false;

	if (!regex_matches(&b64_dataurl_re, src))
		goto out;

	/* fx: SVG can have a meaningful image in under 133 bytes. */
	if (xmlStrcasestr(src, (xmlChar *)"image/svg+xml"))
		goto out;

	/*
	 * fx: Make sure this element has other attributes which contains image.
	 * If it doesn't, then this src is important and shouldn't be removed.
	 */
	for (attr = img->properties; attr; attr = attr->next) {
		xmlChar *value;

		if (strcmp((char *)attr->name, "src") == 0)
			continue;

		value = xmlGetProp(img, attr->name);
		ret = regex_matches(&imgext_re, value);
		xmlFree(value);
		if (ret)
			break;
	}
	if (!ret)
		goto out;

	/*
	 * fx: Here we assume if image is less than 100 bytes (or 133B after
	 * encoded to base64) it will be too small, therefore it might be
	 * placeholder image.
	 */
	base64 = xmlStrcasestr(src, (xmlChar *)"base64");
	if (xmlStrlen(base64) - 7 >= 133)
		ret = false;

out:
	xmlFree(src);
	return ret;
}

/**
 * Will this image node be loaded by javascript?
 */
static bool is_image_lazy(htmlNodePtr img)
{
	bool has_src, has_srcset;
	xmlChar *class;
	bool ret;

	/*
	 * fx: In some sites (e.g. Kotaku), they put 1px square image as base64
	 * data uri in the src attribute. So, here we check if the data uri is
	 * too short, just might as well remove it.
	 */
	if (image_src_is_meaningless(img))
		xmlRemoveProp(xmlHasProp(img, (xmlChar *)"src"));

	has_src = xmlHasProp(img, (xmlChar *)"src");
	has_srcset = xmlHasProp(img, (xmlChar *)"srcset");
	if (!has_src && !has_srcset)
		return true;

	class = xmlGetProp(img, (xmlChar *)"class");
	if (!class)
		return false;
	ret = xmlStrcasestr(class, (xmlChar *)"lazy");
	xmlFree(class);
	return ret;
}

/**
 * Wrapper to use node_has_tag_array() as a condition_fn2 (for two tags only)
 */
static bool has_either_tag(htmlNodePtr node, const void *data)
{
	return node_has_tag_array(node, 2, (const char **)data);
}

/**
 * Does this node have a descendant with one of the two given tags?
 */
static bool has_descendant_tag(htmlNodePtr node, const char *t1, const char *t2)
{
	const char *tags[] = {t1, t2};

	return such_desc_exists(node, has_either_tag, tags);
}

/**
 * Convert a lazy image node into one that can be loaded without javascript
 */
static void fix_lazy_image(htmlNodePtr img)
{
	xmlAttr *attr;
	xmlChar *value = NULL;

	for (attr = img->properties; attr; attr = attr->next) {
		char *dest;

		xmlFree(value);
		value = NULL;

		if (strcmp((char *)attr->name, "src") == 0)
			continue;
		if (strcmp((char *)attr->name, "srcset") == 0)
			continue;

		value = xmlGetProp(img, attr->name);
		if (regex_matches(&srcset_re, value))
			dest = "srcset";
		else if (regex_matches(&src_re, value))
			dest = "src";
		else
			continue;

		/* fx: if this is an img or picture, set the attribute directly */
		if (node_has_tag(img, "img", "picture")) {
			xmlSetProp(img, (xmlChar *)dest, value);
		} else if (!has_descendant_tag(img, "img", "picture")) {
			htmlNodePtr new;

			/*
			 * fx: if the item is a <figure> that does not contain an image or
			 * picture, create one and place it inside the figure see the
			 * nytimes-3 testcase for an example
			 */
			new = xmlNewNode(NULL, (xmlChar *)"img");
			if (!new || !xmlAddChild(img, new))
				fatal();
			xmlSetProp(new, (xmlChar *)dest, value);
		}
	}
	xmlFree(value);
}

/**
 * fx: convert images and figures that have properties like data-src into
 * images that can be loaded without JS
 */
static htmlNodePtr fix_if_lazy_image(htmlNodePtr node)
{
	if (node_has_tag(node, "img", "picture", "figure") && is_image_lazy(node))
		fix_lazy_image(node);
	return node;
}

/**
 * Is this node a data table, or inside of one?
 */
static bool inside_data_table(htmlNodePtr node)
{
	htmlNodePtr table_ancestor = has_ancestor_tag(node, "table");

	return table_ancestor && is_data_table(table_ancestor);
}

/**
 * Wrapper to use node_has_tag() as a condition_fn2 (for one tag only)
 */
static bool has_this_tag(htmlNodePtr node, const void *data)
{
	return node_has_tag(node, data);
}

/**
 * Count the number of descendants with a given tag
 */
static int tag_count(htmlNodePtr node, const char *tag)
{
	return count_such_descs(node, has_this_tag, tag);
}

/**
 * Is this node an embed?
 */
static inline bool is_embed(htmlNodePtr node)
{
	return node_has_tag(node, "object", "embed", "iframe");
}

/**
 * Is this node an embed with a video?
 */
static bool is_embed_with_video(htmlNodePtr node)
{
	xmlAttr *attr;
	xmlBufferPtr buffer;
	bool found_vid = false;

	if (!is_embed(node))
		return false;

	for (attr = node->properties; attr && !found_vid; attr = attr->next) {
		xmlChar *value;

		/* TODO: can we avoid this awkward re-search, here and elsewhere? */
		value = xmlGetProp(node, attr->name);

		/* fx: If this embed has attribute that matches video regex... */
		if (regex_matches(&videos_re, value))
			found_vid = true;
		xmlFree(value);
	}
	if (found_vid)
		return true;

	/* fx: For embed with <object> tag, check inner HTML as well */
	if (!node_has_tag(node, "object"))
		return false;

	buffer = xmlBufferCreate();
	if (htmlNodeDump(buffer, node->doc, node) < 0)
		fatal();
	if (regex_matches(&videos_re, buffer->content))
		found_vid = true;
	xmlBufferFree(buffer);
	return found_vid;
}

/**
 * Go through all embeds inside a node to check if they can be removed. If the
 * answer is yes, return the embed count as well.
 */
static bool check_embeds_for_removal(htmlNodePtr node, int *count)
{
	htmlNodePtr first, last, curr;
	int tmpcount = 0;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr)) {
		if (is_embed_with_video(curr))
			return false;
		tmpcount += is_embed(curr) ? 1 : 0;
	}
	*count = tmpcount;
	return true;
}

/**
 * Check if the node looks "fishy", for the sake of clean_conditionally()
 */
static bool node_looks_fishy(htmlNodePtr node)
{
	int content_length, weight, p_count, img_count, li_count, input_count;
	int embed_count = 0;
	double link_density;
	bool is_list;

	if (inside_data_table(node))
		return false;

	weight = get_class_weight(node);
	if (weight < 0)
		return true;

	if (content_char_count(node, ',') >= 10)
		return false;

	/*
	 * fx: If there are not very many commas, and the number of non-paragraph
	 * elements is more than paragraphs or other ominous signs, remove the
	 * element.
	 */
	p_count = tag_count(node, "p");
	img_count = tag_count(node, "img");
	li_count = tag_count(node, "li") - 100; /* No idea what this is about */
	input_count = tag_count(node, "input");

	if (!check_embeds_for_removal(node, &embed_count))
		return false;
	link_density = get_link_density(node);
	content_length = text_normalized_content_length(node);

	is_list = node_has_tag(node, "ul", "ol");

	if (!has_ancestor_tag(node, "figure")) {
		if (img_count > 1 && p_count < img_count / 2.0)
			return true;
		if (!is_list && content_length < 25 && (!img_count || img_count > 2))
			return true;
	}
	if (!is_list && li_count > p_count)
		return true;
	if (input_count > p_count / 3)
		return true;
	if (!is_list && weight < 25 && link_density > 0.2)
		return true;
	if (weight >= 25 && link_density > 0.5)
		return true;
	if ((embed_count == 1 && content_length < 75) || embed_count > 1)
		return true;
	return false;
}

/**
 * Is this node a fishy-looking element with the given tag?
 */
static bool is_fishy_with_tag(htmlNodePtr node, const void *tag)
{
	return node_has_tag(node, tag) && node_looks_fishy(node);
}

/**
 * fx: Clean an element of all tags of type "tag" if they look fishy. "Fishy"
 * is an algorithm based on content length, classnames, link density, number
 * of images & embeds, etc.
 */
static void clean_conditionally(htmlNodePtr article, const char *tag)
{
	/*
	 * Traversal direction is important here, because deleting fishy children
	 * may affect the parent's fishiness.
	 */
	if (options.flags & OPT_CLEAN_CONDITIONALLY)
		bw_remove_descendants_if(article, is_fishy_with_tag, tag);
}

/**
 * Check that the node has a given tag, and that it's not an embed with a video
 */
static bool has_tag_not_video_embed(htmlNodePtr node, const void *tag)
{
	return node_has_tag(node, tag) && !is_embed_with_video(node);
}

/**
 * fx: Clean a node of all elements of type "tag". (Unless it's a youtube/vimeo
 * video. People love movies.)
 */
static void clean_all(htmlNodePtr article, const char *tag)
{
	bw_remove_descendants_if(article, has_tag_not_video_embed, tag);
}

/**
 * Does this node have "share" in its id or class?
 */
static bool is_share(htmlNodePtr node)
{
	xmlChar *class = xmlGetProp(node, (xmlChar *)"class");
	xmlChar *id = xmlGetProp(node, (xmlChar *)"id");
	bool ret;

	ret = regex_matches(&share_re, class) || regex_matches(&share_re, id);
	xmlFree(class);
	xmlFree(id);
	return ret;
}

/**
 * Is this an element with little content that has "share" in id/class?
 */
static bool is_small_share_node(htmlNodePtr node)
{
	if (!is_share(node))
		return false;
	return text_content_length(node) < DEFAULT_CHAR_THRESHOLD;
}

/**
 * If the article has a single "h2" element, return it; otherwise, return NULL
 */
static htmlNodePtr has_single_h2(htmlNodePtr article)
{
	htmlNodePtr first, last, curr;
	htmlNodePtr h2 = NULL;

	first = following_node(article);
	last = skip_node_descendants(article);
	for (curr = first; curr != last; curr = following_node(curr)) {
		if (node_has_tag(curr, "h2")) {
			if (h2)
				return NULL;
			h2 = curr;
		}
	}
	return h2;
}

/**
 * fx: If there is only one h2 and its text content substantially equals
 * article title, they are probably using it as a header and not a subheader,
 * so remove it since we already extract the title separately.
 */
static void remove_title(htmlNodePtr article)
{
	htmlNodePtr h2;
	char *h2_text;
	double title_len, h2_len, diff;
	bool is_match = false;

	/* Do nothing if we have no title, to avoid division by zero */
	if (!metadata.title || !*metadata.title)
		return;

	h2 = has_single_h2(article);
	if (!h2)
		return;
	h2_text = (char *)xmlNodeGetContent(h2);
	if (!h2_text) /* This should never happen, ignore it */
		return;

	title_len = strlen(metadata.title);
	h2_len = strlen(h2_text);
	diff = (h2_len - title_len) / title_len;

	if (fabs(diff) < 0.5) {
		if (diff > 0)
			is_match = strstr(h2_text, metadata.title);
		else
			is_match = strstr(metadata.title, h2_text);
	}

	xmlFree((xmlChar *)h2_text);
	if (is_match) {
		xmlUnlinkNode(h2);
		free_node(h2);
	}
}

/**
 * Is this node a spurious header?
 * fx: Checks things like classnames and link density.
 */
static bool is_spurious_header(htmlNodePtr node)
{
	return node_has_tag(node, "h1", "h2") && get_class_weight(node) < 0;
}

/**
 * Is this a paragraph with no valuable content?
 */
static bool is_extra_paragraph(htmlNodePtr node)
{
	if (!node_has_tag(node, "p"))
		return false;
	if (has_descendant_tag(node, "img", "embed"))
		return false;
	/*
	 * fx: At this point, nasty iframes have been removed, only remain embedded
	 * video ones.
	 */
	if (has_descendant_tag(node, "object", "iframe"))
		return false;
	return text_content_length(node) == 0;
}

/**
 * Is this node a line break and is the next element a paragraph?
 */
static bool is_line_break_before_paragraph(htmlNodePtr node)
{
	return node_has_tag(node, "br") && node_has_tag(next_element(node), "p");
}

/**
 * If the given node is a single-cell table, replace it with its content;
 * either way, return the new node in this place.
 */
static htmlNodePtr unwrap_if_single_cell_table(htmlNodePtr node)
{
	htmlNodePtr tbody, row, cell;

	if (!node_has_tag(node, "table"))
		return node;

	tbody = has_single_tag_inside(node, "tbody");
	if (!tbody)
		tbody = node;
	row = has_single_tag_inside(tbody, "tr");
	if (!row)
		return node;
	cell = has_single_tag_inside(row, "td");
	if (!cell)
		return node;

	if (forall_descendants(cell, is_phrasing_content))
		xmlNodeSetName(cell, (xmlChar *)"p");
	else
		xmlNodeSetName(cell, (xmlChar *)"div");

	xmlReplaceNode(node, cell);
	free_node(node);
	return cell;
}

/**
 * fx: Prepare the article node for display. Clean out any inline styles,
 * iframes, forms, strip extraneous <p> tags, etc.
 */
void prep_article(htmlNodePtr article)
{
	clean_styles(article);

	/*
	 * fx: Check for data tables before we continue, to avoid removing items in
	 * those tables, which will often be isolated even though they're visually
	 * linked to other content-ful elements (text, images, etc.).
	 */
	change_descendants(article, mark_if_data_table);

	change_descendants(article, fix_if_lazy_image);

	/* fx: Clean out junk from the article content */
	clean_conditionally(article, "form");
	clean_conditionally(article, "fieldset");
	clean_all(article, "object");
	clean_all(article, "embed");
	clean_all(article, "h1");
	clean_all(article, "footer");
	clean_all(article, "link");
	clean_all(article, "aside");
	remove_descendants_if(article, is_small_share_node);
	remove_title(article);
	clean_all(article, "iframe");
	clean_all(article, "input");
	clean_all(article, "textarea");
	clean_all(article, "select");
	clean_all(article, "button");
	remove_descendants_if(article, is_spurious_header);
	/*
	 * fx: Do these last as the previous stuff may have removed junk that will
	 * affect these.
	 */
	clean_conditionally(article, "table");
	clean_conditionally(article, "ul");
	clean_conditionally(article, "div");

	/* fx: Remove extra paragraphs */
	remove_descendants_if(article, is_extra_paragraph);

	remove_descendants_if(article, is_line_break_before_paragraph);
	change_descendants(article, unwrap_if_single_cell_table);
}
