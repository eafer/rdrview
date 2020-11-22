/*
 * Functions to aid in traversing an HTML document tree
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

#include <libxml/tree.h>
#include "rdrview.h"

/**
 * Start listing all nodes in the document
 */
htmlNodePtr first_node(htmlDocPtr doc)
{
	return following_node(xmlDocGetRootElement(doc));
}

/**
 * Get the following node in the list, skipping descendants of the current one
 */
htmlNodePtr skip_node_descendants(htmlNodePtr node)
{
	while (node) {
		if (node->next)
			return node->next;
		node = node->parent;
	}
	return NULL;
}

/**
 * Get the following node in the document's node list
 */
htmlNodePtr following_node(htmlNodePtr node)
{
	if (node->children)
		return node->children;
	return skip_node_descendants(node); /* Skip the nonexistent children */
}

/**
 * Remove a node and get the following one from the document's node list
 */
htmlNodePtr remove_and_get_following(htmlNodePtr node)
{
	htmlNodePtr following;

	following = skip_node_descendants(node);
	xmlUnlinkNode(node);
	free_node(node);
	return following;
}

/**
 * Remove all descendants of a node that return true for the given condition.
 * The check may have a side effect as long as it only changes the node itself.
 */
void remove_descendants_if(htmlNodePtr node, condition_fn check)
{
	htmlNodePtr last, curr;

	curr = following_node(node);
	last = skip_node_descendants(node);
	while (curr != last) {
		if (check(curr))
			curr = remove_and_get_following(curr);
		else
			curr = following_node(curr);
	}
}

/**
 * Remove all nodes of a document that return true for the given condition.
 * The check may have a side effect as long as it only changes the node itself.
 */
void remove_nodes_if(htmlDocPtr doc, condition_fn check)
{
	remove_descendants_if(xmlDocGetRootElement(doc), check);
}

/**
 * Check if a condition matches a boolean value for all descendants of a node.
 */
static bool ckdesc(htmlNodePtr node, condition_fn check, bool val)
{
	htmlNodePtr first, last, curr;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr)) {
		if (check(curr) != val)
			return false;
	}
	return true;
}

/**
 * Check if a condition matches a boolean value for all descendants of a node.
 * The data argument is passed to the check function.
 */
static bool ckdesc4(htmlNodePtr node, condition_fn2 check, const void *data, bool val)
{
	htmlNodePtr first, last, curr;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr)) {
		if (check(curr, data) != val)
			return false;
	}
	return true;
}

/**
 * Check if a condition is true on all descendants of a node.
 */
bool forall_descendants(htmlNodePtr node, condition_fn check)
{
	return ckdesc(node, check, true);
}

/**
 * Check if the node has a descendant such that the condition is true. The data
 * argument is passed to the check function.
 */
bool such_desc_exists(htmlNodePtr node, condition_fn2 check, const void *data)
{
	return !ckdesc4(node, check, data, false);
}

/**
 * Check if there is a node in the document such that the condition is true.
 * The data argument is passed to the check function.
 */
bool such_node_exists(htmlDocPtr doc, condition_fn2 check, const void *data)
{
	return such_desc_exists(xmlDocGetRootElement(doc), check, data);
}

/**
 * Check if the node has a descendant that verifies the given condition
 */
bool has_such_descendant(htmlNodePtr node, condition_fn check)
{
	return !ckdesc(node, check, false);
}

/**
 * Run an action on all nodes of a document. The returned pointer is the last
 * non-NULL pointer returned by an action.
 *
 * The action may modify the tree, but it must not unlink the node itself.
 */
void *run_on_nodes(htmlDocPtr doc, action_fn act)
{
	htmlNodePtr node;
	void *ret = NULL;

	for (node = first_node(doc); node; node = following_node(node)) {
		void *tmp;

		tmp = act(node);
		if (tmp)
			ret = tmp;
	}
	return ret;
}

/**
 * Run a replacement function on all descendants of a node; that function must
 * return a pointer to the new node, so that the traversal can continue.
 */
void change_descendants(htmlNodePtr node, replace_fn replace)
{
	htmlNodePtr first, last, curr;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr))
		curr = replace(curr);
}

/**
 * Run a calculation on all descendants of a node, return the sum of all
 * results.
 */
double total_for_descendants(htmlNodePtr node, calc_fn calc)
{
	htmlNodePtr first, last, curr;
	double total = 0;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr))
		total += calc(curr);
	return total;
}

/**
 * Count all descendants of the node that satisfy the condition. The data
 * argument is passed to the check function.
 */
int count_such_descs(htmlNodePtr node, condition_fn2 check, const void *data)
{
	htmlNodePtr first, last, curr;
	int count = 0;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr)) {
		if (check(curr, data))
			++count;
	}
	return count;
}

/**
 * Return the first descendant that has a given tag, or NULL if none
 */
htmlNodePtr first_descendant_with_tag(htmlNodePtr node, const char *tag)
{
	htmlNodePtr first, last, curr;

	first = following_node(node);
	last = skip_node_descendants(node);
	for (curr = first; curr != last; curr = following_node(curr)) {
		if (node_has_tag(curr, tag))
			return curr;
	}
	return NULL;
}

/**
 * Return the first node in the document that has a given tag, or NULL if none
 */
htmlNodePtr first_node_with_tag(htmlDocPtr doc, const char *tag)
{
	return first_descendant_with_tag(xmlDocGetRootElement(doc), tag);
}

/**
 * Start listing all descendants of root, in reverse order to first_node()
 */
static htmlNodePtr last_node(htmlNodePtr root)
{
	htmlNodePtr curr = root;
	htmlNodePtr next = following_node(curr);

	while (next) {
		curr = next;
		next = following_node(curr);
	}
	return curr;
}

/**
 * Get the previous node in the list of descendants of root, in reverse order
 * to following_node().
 */
static htmlNodePtr previous_node(htmlNodePtr node)
{
	htmlNodePtr prev;

	if (node->prev) {
		prev = node->prev;
		while (prev->children) {
			prev = prev->children;
			while (prev->next)
				prev = prev->next;
		}
	} else {
		prev = node->parent;
	}

	assert(following_node(prev) == node);
	return prev;
}

/**
 * Remove a node and get the previous one from the list of descendants of root
 */
static htmlNodePtr remove_and_get_previous(htmlNodePtr node)
{
	htmlNodePtr previous;

	previous = previous_node(node);
	xmlUnlinkNode(node);
	free_node(node);
	return previous;
}

/**
 * Remove all descendants of a node that return true for the given condition.
 * Compared to remove_descendants_if(), the tree is traversed backwards, so the
 * children are cleaned first. The data argument is passed to the check
 * function.
 */
void bw_remove_descendants_if(htmlNodePtr node, condition_fn2 check, const void *data)
{
	htmlNodePtr last, curr;

	curr = last_node(node);
	last = node;
	while (curr != last) {
		if (check(curr, data))
			curr = remove_and_get_previous(curr);
		else
			curr = previous_node(curr);
	}
}

/**
 * fx: Finds the next element, starting from the given node, and ignoring
 * whitespace in between.
 *
 * Return NULL if a nonempty text node is found first, or if there are no
 * more element siblings.
 */
htmlNodePtr next_element(htmlNodePtr node)
{
	htmlNodePtr next;

	for (next = node->next; next; next = next->next) {
		if (next->type == XML_ELEMENT_NODE)
			return next;
		if (text_content_length(next))
			return NULL;
	}
	return NULL;
}

/**
 * Finds the previous element, starting from the given node, and ignoring
 * whitespace in between.
 *
 * Return NULL if a nonempty text node is found first, or if there are no
 * more element siblings.
 */
htmlNodePtr prev_element(htmlNodePtr node)
{
	htmlNodePtr prev;

	for (prev = node->prev; prev; prev = prev->prev) {
		if (prev->type == XML_ELEMENT_NODE)
			return prev;
		if (text_content_length(prev))
			return NULL;
	}
	return NULL;
}
