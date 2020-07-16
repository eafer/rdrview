/*
 * Functions to obtain or save information about an HTML node
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
#include "rdrview.h"

/**
 * If the node has no info struct, allocate and attach one; return the struct.
 */
struct node_info *allocate_node_info(htmlNodePtr node)
{
	if (node->_private) /* This node already has an info struct */
		return node->_private;
	node->_private = calloc(1, sizeof(struct node_info));
	if (!node->_private)
		fatal_errno();
	return node->_private;
}

/**
 * Free the info struct for the node (if any) and clear its field.
 */
static htmlNodePtr free_node_info(htmlNodePtr node)
{
	free(node->_private);
	node->_private = NULL;
	return node;
}

/**
 * Like xmlFreeNode(), but also free the info struct for each node.
 */
void free_node(htmlNodePtr node)
{
	if (!node)
		return;
	free_node_info(node);
	change_descendants(node, free_node_info);
	xmlFreeNode(node);
}

/**
 * Like xmlFreeDoc(), but also free the info struct for each node.
 */
void free_doc(htmlDocPtr doc)
{
	htmlNodePtr root = xmlDocGetRootElement(doc);

	if (root) {
		free_node_info(root);
		change_descendants(root, free_node_info);
	}
	xmlFreeDoc(doc);
}

/**
 * Check if a node has one of the (lowercase) tag names in the array
 *
 * It's usually cleaner to call this function through the node_has_tag() macro
 * wrapper, so that the tag count argument is not required.
 */
bool node_has_tag_array(htmlNodePtr node, int tagc, const char * const tagv[])
{
	if (!node || !node->name)
		return false;

	while (tagc--) {
		if (xmlStrcasecmp(node->name, (xmlChar *)*tagv++) == 0)
			return true;
	}
	return false;
}

/**
 * fx: Check if a given node has one of its ancestor tag name matching the
 * provided one.
 *
 * Return the closest ancestor (including the node itself), or NULL if none.
 */
htmlNodePtr has_ancestor_tag(htmlNodePtr node, const char *tag)
{
	for (; node; node = node->parent) {
		if (node_has_tag(node, tag))
			return node;
	}
	return NULL;
}

/**
 * Check if a style attribute sets 'display' to 'none'
 */
static bool is_display_none(const xmlChar *style)
{
	const char *display;
	char value[6];

	/*
	 * If 'display' is set twice, we check the first one. I don't think this is
	 * correct, but it doesn't matter much.
	 */
	display = (char *)xmlStrcasestr(style, (xmlChar *)"display");
	if (!display)
		return false;
	if (sscanf(display, "%*[^:]: %5[^; ]", value) != 1)
		return false;
	return strcasecmp(value, "none") == 0;
}

bool is_node_visible(htmlNodePtr node)
{
	xmlChar *style = xmlGetProp(node, (xmlChar *)"style");
	xmlChar *hidden = xmlGetProp(node, (xmlChar *)"hidden");
	xmlChar *aria_hidden = xmlGetProp(node, (xmlChar *)"aria-hidden");
	xmlChar *class = xmlGetProp(node, (xmlChar *)"class");
	bool is_visible;

	/*
	 * fx: Have to null-check node.style and node.className.indexOf to deal
	 * with SVG and MathML nodes.
	 */
	is_visible = false;
	if (style && is_display_none(style))
		goto out;
	if (hidden)
		goto out;

	/*
	 * fx: check for "fallback-image" so that wikimedia math images are
	 * displayed
	 */
	is_visible = true;
	if (!aria_hidden || strcmp((char *)aria_hidden, "true") != 0)
		goto out;
	if (class && strstr((char *)class, "fallback-image"))
		goto out;

	is_visible = false;
out:
	xmlFree(style);
	xmlFree(hidden);
	xmlFree(aria_hidden);
	xmlFree(class);
	return is_visible;
}

/**
 * Considering only the node's class and id, is it unlikely to be readable?
 */
bool node_has_unlikely_class_id(htmlNodePtr node)
{
	xmlChar *class, *id;
	bool is_unlikely = false;

	class = xmlGetProp(node, (xmlChar *)"class");
	id = xmlGetProp(node, (xmlChar *)"id");

	/* For the node to be unlikely, the class or the id must be on that list */
	if (!regex_matches(&unlikely_re, class) && !regex_matches(&unlikely_re, id))
		goto out;
	/* Also, neither the class nor the id can be on the candidate list */
	if (regex_matches(&candidate_re, class) || regex_matches(&candidate_re, id))
		goto out;
	is_unlikely = true;

out:
	xmlFree(class);
	xmlFree(id);
	return is_unlikely;
}

/**
 * fx: Get an elements class/id weight. Uses regular expressions to tell if
 * this element looks good or bad.
 */
int get_class_weight(htmlNodePtr node)
{
	xmlChar *class, *id;
	int weight = 0;

	if (!(options.flags & OPT_WEIGHT_CLASSES))
		return 0;

	class = xmlGetProp(node, (xmlChar *)"class");
	if (class) {
		if (regex_matches(&negative_re, class))
			weight -= 25;
		if (regex_matches(&positive_re, class))
			weight += 25;
	}

	id = xmlGetProp(node, (xmlChar *)"id");
	if (id) {
		if (regex_matches(&negative_re, id))
			weight -= 25;
		if (regex_matches(&positive_re, id))
			weight += 25;
	}

	xmlFree(class);
	xmlFree(id);
	return weight;
}

/**
 * Is this attribute's value equal to the given string?
 */
bool attrcmp(htmlNodePtr node, const char *attrname, const char *str)
{
	xmlChar *value = xmlGetProp(node, (xmlChar *)attrname);
	bool ret;

	ret = value && strcmp((char *)value, str) == 0;
	xmlFree(value);
	return ret;
}
