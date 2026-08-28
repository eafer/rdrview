/*
 * Functions for the manipulation of strings from HTML content
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

#include <ctype.h>
#include <string.h>
#include "rdrview.h"

/**
 * Check if a string has a given head
 */
static bool is_str_head(const char *str, const char *head)
{
	for (; *str && *head; ++str, ++head) {
		if (*str != *head)
			return false;
	}
	return !*head;
}

/**
 * Replace a string with a trimmed version and unescape html characters
 */
void trim_and_unescape(char **str)
{
	char *start;
	char *src, *dest;
	char *trimmed;

	if (!*str)
		return;

	for (start = *str; isspace(*start); ++start);
	trimmed = malloc(strlen(start) + 1);

	for (src = start, dest = trimmed; *src; ++src, ++dest) {
		if (*src != '&') {
			*dest = *src;
			continue;
		}

		if (is_str_head(src, "&amp;")) {
			*dest = '&';
			src += strlen("&amp;") - 1;
		} else if (is_str_head(src, "&quot;")) {
			*dest = '"';
			src += strlen("&quot;") - 1;
		} else if (is_str_head(src, "&apos;")) {
			*dest = '\'';
			src += strlen("&apos;") - 1;
		} else if (is_str_head(src, "&lt;")) {
			*dest = '<';
			src += strlen("&lt;") - 1;
		} else if (is_str_head(src, "&gt;")) {
			*dest = '>';
			src += strlen("&gt;") - 1;
		} else if (*(src + 1) == '#') {
			/* TODO: hex support */
			src += 2;
			*dest = atoi(src);
			src = strchr(src, ';');
			if (!src)
				break;
		} else {
			*dest = *src;
		}
	}
	*dest = '\0';

	free(*str);
	*str = trimmed;
}

/**
 * Copy a string, stripping excess whitespace
 */
void strcpy_normalize(xmlChar *dest, const xmlChar *src)
{
	for (; *src; ++src, ++dest) {
		*dest = *src;

		/* Handle non-breaking space because it's too common to ignore */
		while (isspace(*src) || (*src && memcmp(src, "\xc2\xa0", 2) == 0)) {
			*dest = ' ';
			src += isspace(*src) ? 1 : 2;
		}
		/* Skip zero-width space (U+200B) */
		if (memcmp(src, "\xe2\x80\x8b", 3) == 0) {
			--dest;   // Don't copy it
			src += 2; // Skip the extra bytes
		}
		if (*dest == ' ')
			--src;
	}
	*dest = '\0';
}

/**
 * Get the text content of a node, after stripping excess whitespace; the
 * caller must use xmlFree() to release the returned buffer.
 */
xmlChar *node_get_normalized_content(htmlNodePtr node)
{
	xmlChar *content = xmlNodeGetContent(node);
	xmlChar *stripped;

	if (!content)
		return NULL;
	stripped = xmlMalloc(strlen((char *)content) + 1);
	if (!stripped)
		fatal_errno();

	strcpy_normalize(stripped, content);
	xmlFree(content);
	return stripped;
}

/**
 * Get length of a node's text content, after stripping excess whitespace
 */
int text_normalized_content_length(htmlNodePtr node)
{
	xmlChar *content = node_get_normalized_content(node);
	int len, utf8len;

	if (!content)
		return 0;

	/* Finding the length twice is ugly, could it be improved? */
	len = xmlStrlen(content);
	utf8len = xmlUTF8Strlen(content);
	if (!len)
		goto out;

	if (content[0] == ' ')
		--utf8len;
	if (len > 1 && content[len - 1] == ' ')
		--utf8len;

out:
	xmlFree(content);
	return utf8len;
}

/**
 * Like node_get_normalized_content(), but respect whitespace for preformatted
 * text. This function must only be called for text nodes.
 */
xmlChar *node_get_normalized_or_preformatted(htmlNodePtr node)
{
	assert(node->type == XML_TEXT_NODE);
	if (has_ancestor_tag(node, "code") || has_ancestor_tag(node, "pre"))
		return xmlNodeGetContent(node);
	return node_get_normalized_content(node);
}

/**
 * Get length of a node's text content, ignoring leading and trailing whitespace
 *
 * TODO: many calls to this function should be replaced with the utf8 version
 */
int text_content_length(htmlNodePtr node)
{
	xmlChar *content, *curr;
	int length = 0;
	int white_length = 0;
	bool on_head = true;

	content = xmlNodeGetContent(node);
	if (!content)
		return 0;

	for (curr = content; *curr; ++curr) {
		/* Ignore the leading whitespace. TODO: unicode spaces like nbsp? */
		if (on_head) {
			if (isspace(*curr))
				continue;
			else
				on_head = false;
		}

		/* Keep track of the trailing whitespace */
		if (isspace(*curr))
			++white_length;
		else
			white_length = 0;

		++length;
	}

	xmlFree(content);
	return length - white_length;
}

/**
 * Count the number of times a given character appears in the string
 */
int char_count(const char *str, char c)
{
	int count = 0;

	if (!str)
		return 0;

	for (; *str; str++) {
		if (*str == c)
			++count;
	}
	return count;
}

/**
 * If the node is a link, return the length of its content; return 0 otherwise
 */
static double length_if_link(htmlNodePtr node)
{
	return node_has_tag(node, "a") ? text_normalized_content_length(node) : 0;
}

/**
 * fx: Get the density of links as a percentage of the content. This is the
 * amount of text that is inside a link divided by the total text in the node.
 */
double get_link_density(htmlNodePtr node)
{
	double textlen;

	textlen = text_normalized_content_length(node);
	if (!textlen)
		return 0;
	return total_for_descendants(node, length_if_link) / textlen;
}

static const char * const PHRASING_ELEMS[] = {
	"abbr", "audio", "b", "bdo", "br", "button", "cite", "code", "data",
	"datalist", "dfn", "em", "embed", "i", "img", "input", "kbd", "label",
	"mark", "math", "meter", "noscript", "object", "output", "progress", "q",
	"ruby", "samp", "script", "select", "small", "span", "strong", "sub",
	"sup", "textarea", "time", "var", "wbr",
};

/**
 * Is this node guaranteed to be phrasing content, regardless of its children?
 */
static bool is_definitely_phrasing_content(htmlNodePtr node)
{
	if (node->type == XML_TEXT_NODE)
		return true;
	return node_has_tag_array(node, ARRAY_SIZE(PHRASING_ELEMS), PHRASING_ELEMS);
}

/**
 * Is this a node that could be phrasing content, but only if its descendants
 * are as well?
 */
static bool is_conditional_phrasing_content(htmlNodePtr node)
{
	return node_has_tag(node, "a", "del", "ins");
}

/**
 * Without considering the descendants, could this node be phrasing content?
 */
static bool can_be_phrasing_content(htmlNodePtr node)
{
	if (is_definitely_phrasing_content(node))
		return true;
	if (is_conditional_phrasing_content(node))
		return true;
	return false;
}

/**
 * fx: Determine if a node qualifies as phrasing content.
 * https://developer.mozilla.org/en-US/docs/Web/Guide/HTML/Content_categories
 */
bool is_phrasing_content(htmlNodePtr node)
{
	if (is_definitely_phrasing_content(node))
		return true;
	if (!is_conditional_phrasing_content(node))
		return false;

	/* Firefox uses recursion here, we check all descendants in a loop */
	return forall_descendants(node, can_be_phrasing_content);
}

/**
 * fx: Check if this node has only whitespace and a single element with given
 * tag. Returns false if the DIV node contains non-empty text nodes or if it
 * contains no element with given tag or more than 1 element.
 *
 * Return the child element, or NULL if the check failed.
 */
htmlNodePtr has_single_tag_inside(htmlNodePtr node, const char *tag)
{
	htmlNodePtr child;
	htmlNodePtr element_child = NULL;

	for (child = node->children; child; child = child->next) {
		if (child->type == XML_ELEMENT_NODE) {
			/* fx: There should be exactly 1 element child with given tag */
			if (element_child || !node_has_tag(child, tag))
				return NULL;
			element_child = child;
		} else if (child->type == XML_TEXT_NODE) {
			xmlChar *content = xmlNodeGetContent(child);
			bool has_content = false;

			/* fx: And there should be no text nodes with real content */
			if (regex_matches(&hascontent_re, content))
				has_content = true;
			xmlFree(content);
			if (has_content)
				return NULL;
		}
	}
	return element_child;
}

/* String listing the separator characters */
static const char *SEPARATORS = "|-\\/>»";

/**
 * Count the words in a string, optionally treating separators as spaces
 * TODO: what about unicode spaces?
 */
int word_count(const char *str, bool separators_are_spaces)
{
	int count = 0;

	while (true) {
		for (; isspace(*str); ++str);
		if (separators_are_spaces)
			for (; *str && strchr(SEPARATORS, *str); ++str);
		if (!*str)
			break;

		++count;
		if (separators_are_spaces)
			for (; *str && !isspace(*str) && !strchr(SEPARATORS, *str); ++str);
		else
			for (; *str && !isspace(*str); ++str);
	}
	return count;
}

/**
 * Return a pointer to the first separator in a string, or NULL if none
 */
static char *find_first_separator(char *str)
{
	char *curr = str;

	while (true) {
		curr = strpbrk(curr, SEPARATORS);
		if (!curr)
			return NULL;

		/* The separator must have spaces on both sides */
		if (curr != str && *(curr - 1) == ' ' && *(curr + 1) == ' ')
			return curr;
		++curr;
	}
}

/**
 * Return a pointer to the last separator in a string, or NULL if none
 */
char *find_last_separator(char *str)
{
	char *curr = str;
	char *sep = NULL;

	while (true) {
		curr = find_first_separator(curr);
		if (!curr)
			return sep;
		sep = curr;
	}
}

/**
 * Replace all occurrences of a non-null character in a string with a new one
 */
void replace_char(char *str, int old, int new)
{
	for (str = strchr(str, old); str; str = strchr(str, old))
		*str = new;
}

/**
 * Is this one of the words in the given string?
 */
bool word_in_str(const char *str, const char *word)
{
	int wordlen = strlen(word);

	for (; *str; ++str) {
		if (strncasecmp(word, str, wordlen) == 0) {
			str += wordlen;
			if (!*str || isspace(*str))
				return true;
		}
		/* Look for any white-space, though most of these would never show up */
		str = strpbrk(str, " \f\n\r\t\v");
		if (!str)
			return false;
	}
	return false;
}
