/*
 * Implementation of is_probably_readerable(), a quick readability check
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

#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <libxml/HTMLtree.h>
#include <libxml/tree.h>
#include "rdrview.h"

/**
 * Does the node match the 'li p' selector?
 */
static bool is_node_paragraph_in_list(htmlNodePtr node)
{
	return node_has_tag(node, "p") && has_ancestor_tag(node, "li");
}

/**
 * Assign a readability score to a node
 */
static double node_score(htmlNodePtr node)
{
	int length;

	if (!is_node_visible(node))
		return 0;
	if (node_has_unlikely_class_id(node))
		return 0;
	if (is_node_paragraph_in_list(node))
		return 0;

	length = text_content_length(node);
	return length < 140 ? 0 : sqrt(length - 140);
}

/**
 * fx: Decides whether or not the document is reader-able without parsing the
 * whole thing.
 */
bool is_probably_readerable(htmlDocPtr doc)
{
	htmlNodePtr node;
	double score = 0;

	/*
	 * Consider all <p> and <pre> nodes.
	 * fx: consider <div> nodes which have <br> node(s) as well.
	 * Some articles' DOM structures might look like
	 * <div>
	 *   Sentences<br>
	 *   <br>
	 *   Sentences<br>
	 *</div>
	 */
	node = first_node(doc);
	while (node) {
		htmlNodePtr parent = node->parent;

		if (node_has_tag(node, "p", "pre")) {
			score += node_score(node);
			node = following_node(node);
		} else if (node_has_tag(node, "br") && node_has_tag(parent, "div")) {
			score += node_score(parent);
			/* We measured the whole parent, so skip its other children */
			node = skip_node_descendants(parent);
		} else {
			node = following_node(node);
			continue;
		}

		if (score > 20)
			return true;
	}
	return false;
}
