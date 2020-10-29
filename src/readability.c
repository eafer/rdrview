/*
 * Implementation of parse(), which tries to extract an article from the HTML
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
#include <libxml/tree.h>
#include <libxml/uri.h>
#include "rdrview.h"

struct metadata metadata = {0};

/* List of previous attempts to get the content */
struct attempt {
	htmlNodePtr article;
	int length;
};
static struct attempt attempts[4] = {0};

/**
 * Is there a better title than the current one under the name or property of
 * this meta tag?
 */
static bool is_better_title(char *nameprop)
{
	static const char * const titlenames[] = {
		"dc:title", "dcterm:title", "og:title", "weibo:article:title",
		"weibo:webpage:title", "title", "twitter:title"
	};
	static unsigned int best_i = ARRAY_SIZE(titlenames);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(titlenames); ++i) {
		if (i <= best_i && word_in_str(nameprop, titlenames[i])) {
			best_i = i;
			return true;
		}
	}
	return false;
}

/**
 * Is there a better byline than the current one under the name or property of
 * this meta tag?
 */
static bool is_better_byline(char *nameprop)
{
	static const char * const authornames[] = {
		"dc:creator", "dcterm:creator", "author"
	};
	static unsigned int best_i = ARRAY_SIZE(authornames);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(authornames); ++i) {
		if (i <= best_i && word_in_str(nameprop, authornames[i])) {
			best_i = i;
			return true;
		}
	}
	return false;
}

/**
 * Is there a better excerpt than the current one under the name or property of
 * this meta tag?
 */
static bool is_better_excerpt(char *nameprop)
{
	static const char * const excerptnames[] = {
		"dc:description", "dcterm:description", "og:description",
		"weibo:article:description", "weibo:webpage:description",
		"description", "twitter:description"
	};
	static unsigned int best_i = ARRAY_SIZE(excerptnames);
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(excerptnames); ++i) {
		if (i <= best_i && word_in_str(nameprop, excerptnames[i])) {
			best_i = i;
			return true;
		}
	}
	return false;
}

/**
 * Extract a metadata field from the content of a name or property meta tag
 *
 * TODO: apparently a single property tag can have multiple contents?
 */
static void parse_meta_attrs(char *nameprop, char *content)
{
	char **field = NULL;

	if (!*content)
		return;

	replace_char(nameprop, '.', ':');

	field = &metadata.title;
	if (is_better_title(nameprop))
		goto copy;

	field = &metadata.byline;
	if (is_better_byline(nameprop))
		goto copy;

	field = &metadata.excerpt;
	if (is_better_excerpt(nameprop))
		goto copy;

	field = &metadata.site_name;
	if (word_in_str(nameprop, "og:site_name"))
		goto copy;
	return;

copy:
	if (*field)
		free(*field);
	*field = malloc(strlen(content) + 1);
	if (!*field)
		fatal_errno();
	strcpy_normalize((xmlChar *)*field, (xmlChar *)content);
}

/**
 * Is this node a heading containing the given string?
 */
static bool is_heading_with_str(htmlNodePtr node, const void *str)
{
	xmlChar *content;
	bool ret;

	if (!node_has_tag(node, "h1", "h2"))
		return false;

	content = node_get_normalized_content(node);
	if (!content)
		return false;
	ret = strcmp(str, (char *)content) == 0; /* TODO: trimming? */
	xmlFree(content);
	return ret;
}

/**
 * Get the title for the article from the title tag
 * TODO: this function is still missing some stuff from the firefox version
 */
static char *get_article_title(htmlDocPtr doc, htmlNodePtr titlenode)
{
	char *title, *original;
	int title_count, orig_count;
	char *sep;

	title = (char *)node_get_normalized_content(titlenode);
	original = strdup(title);

	sep = find_last_separator(title);
	if (sep) {
		*(sep - 1) = '\0';
	} else {
		char *substr, *copy;

		substr = strrchr(title, ':');

		/*
		 * fx: Check if we have an heading containing this exact string, so we
		 * could assume it's the full title.
		 */
		if (substr && such_node_exists(doc, is_heading_with_str, title))
			goto out;

		if (substr) {
			copy = strdup(substr + 1);
			xmlFree(title);
			title = copy;
		}
	}

	title_count = word_count(title, false /* separators_are_spaces */);
	orig_count = word_count(original, true /* separators are spaces */);
	if (title_count <= 4 && (!sep || title_count != orig_count - 1)) {
		free(title);
		title = original;
	}

out:
	if (title != original)
		free(original);
	return title;
}

/**
 * If this is a metadata node, extract and remember the metadata. Return NULL,
 * except for the title node: in that case return the node.
 */
static void *node_extract_metadata(htmlNodePtr node)
{
	xmlChar *name = NULL;
	xmlChar *property = NULL;
	xmlChar *content = NULL;

	if (node_has_tag(node, "title"))
		return node;
	if (!node_has_tag(node, "meta"))
		return NULL;

	content = xmlGetProp(node, (xmlChar *)"content");
	if (!content)
		goto out;

	property = xmlGetProp(node, (xmlChar *)"property");
	if (regex_matches(&property_re, property)) {
		parse_meta_attrs((char *)property, (char *)content);
		goto out;
	}

	name = xmlGetProp(node, (xmlChar *)"name");
	if (regex_matches(&name_re, name))
		parse_meta_attrs((char *)name, (char *)content);

out:
	xmlFree(name);
	xmlFree(property);
	xmlFree(content);
	return NULL;
}

/**
 * fx: Attempts to get excerpt and byline metadata for the article.
 */
static void get_article_metadata(htmlDocPtr doc)
{
	htmlNodePtr titlenode = NULL;

	titlenode = run_on_nodes(doc, node_extract_metadata);
	if (!metadata.title && titlenode)
		metadata.title = get_article_title(doc, titlenode);
}

/**
 * Check if this node has the byline and, if it does, remember the value
 */
static bool check_byline(htmlNodePtr node)
{
	static bool found_byline;
	xmlChar *itemprop = NULL;
	xmlChar *class = NULL;
	xmlChar *id = NULL;
	bool is_byline = true;

	if (found_byline)
		return false;

	if (attrcmp(node, "rel", "author"))
		goto out;
	itemprop = xmlGetProp(node, (xmlChar *)"itemprop");
	if (itemprop && strstr((char *)itemprop, "author"))
		goto out;

	class = xmlGetProp(node, (xmlChar *)"class");
	id = xmlGetProp(node, (xmlChar *)"id");

	if (regex_matches(&byline_re, class) || regex_matches(&byline_re, id))
		goto out;

	is_byline = false;
out:
	if (is_byline) {
		int len = text_content_length(node);

		if (len > 0 && len < 100) { /* Is this a sane byline? */
			if (!metadata.byline)
				metadata.byline = (char *)node_get_normalized_content(node);
			found_byline = true;
		}
	}

	xmlFree(itemprop);
	xmlFree(class);
	xmlFree(id);
	return found_byline;
}

/**
 * Is this node unlikely to be readable?
 */
static bool is_node_unlikely(htmlNodePtr node)
{
	if (attrcmp(node, "role", "complementary"))
		return true;
	if (has_ancestor_tag(node, "table") || node_has_tag(node, "body", "a"))
		return false;
	return node_has_unlikely_class_id(node);
}

/**
 * If this node is an element, is it a break element?
 */
static bool is_break_if_element(htmlNodePtr node)
{
	if (node->type != XML_ELEMENT_NODE)
		return true;
	return node_has_tag(node, "br", "hr");
}

/**
 * Is this node an empty element?
 */
static bool is_element_without_content(htmlNodePtr node)
{
	if (node->type != XML_ELEMENT_NODE || text_content_length(node))
		return false;
	return forall_descendants(node, is_break_if_element);
}

static const char * const DIV_ELEMS[] = {
	"div", "section", "header", "h1", "h2", "h3", "h4", "h5", "h6",
};

/**
 * Is this a DIV, SECTION or HEADER node without any content?
 */
static bool is_division_without_content(htmlNodePtr node)
{
	if (!node_has_tag_array(node, ARRAY_SIZE(DIV_ELEMS), DIV_ELEMS))
		return false;
	return is_element_without_content(node);
}

/**
 * Is this node white space?
 */
static bool is_whitespace(htmlNodePtr node)
{
	if (xmlNodeIsText(node) && !text_content_length(node))
		return true;
	return node_has_tag(node, "br");
}

/**
 * Remove all trailing children that are just whitespace
 */
static void prune_trailing_whitespace(htmlNodePtr node)
{
	htmlNodePtr child = xmlGetLastChild(node);

	while (child && is_whitespace(child)) {
		htmlNodePtr prev = child->prev;

		xmlUnlinkNode(child);
		free_node(child);
		child = prev;
	}
}

/**
 * Reparent a node to a preceding "p" sibling; if the sibling is NULL, create
 * it. Return the new parent for the node, or NULL if no reparenting happened.
 */
static htmlNodePtr reparent_to_p_sibling(htmlNodePtr node, htmlNodePtr sibling)
{
	if (!sibling) {
		if (is_whitespace(node))
			return NULL; /* Don't start a paragraph for whitespace alone */
		sibling = xmlNewNode(NULL, (xmlChar *)"p");
		if (!sibling || !xmlAddPrevSibling(node, sibling))
			fatal();
	}

	xmlUnlinkNode(node);
	if (!xmlAddChild(sibling, node))
		fatal();
	return sibling;
}

static const char * const DIV_TO_P_ELEMS[] = {
	"a", "blockquote", "dl", "div", "img", "ol", "p", "pre", "table", "ul",
	"select",
};

/**
 * Is this node a block level element?
 */
static bool is_block_element(htmlNodePtr node)
{
	static const int tagc = ARRAY_SIZE(DIV_TO_P_ELEMS);

	if (node->type != XML_ELEMENT_NODE)
		return false;
	return node_has_tag_array(node, tagc, DIV_TO_P_ELEMS);
}

/**
 * Handle a div node for grab_article() and return the next node to process
 */
static htmlNodePtr handle_div_node_for_grab(htmlNodePtr node)
{
	htmlNodePtr parag = NULL;
	htmlNodePtr child;

	/* fx: Put phrasing content into paragraphs */
	for (child = node->children; child; child = child->next) {
		if (is_phrasing_content(child)) {
			parag = reparent_to_p_sibling(child, parag);
			if (parag)
				child = parag;
		} else if (parag) {
			prune_trailing_whitespace(parag);
			parag = NULL;
		}
	}

	/*
	 * fx: Sites like http://mobile.slate.com encloses each paragraph with a
	 * DIV element. DIVs with only a P element inside and no text content can
	 * be safely converted into plain P elements to avoid confusing the scoring
	 * algorithm with DIVs with are, in practice, paragraphs.
	 */
	if (has_single_tag_inside(node, "p") && get_link_density(node) < 0.25) {
		htmlNodePtr child = xmlFirstElementChild(node);

		if (!child)
			fatal();
		xmlReplaceNode(node, child);
		free_node(node);
		node = child;
		mark_to_score(node);
	} else if (!has_such_descendant(node, is_block_element)) {
		xmlNodeSetName(node, (xmlChar *)"p");
		mark_to_score(node);
	}

	return following_node(node);
}

static const char * const TAGS_TO_SCORE[] = {
	"section", "h2", "h3", "h4", "h5", "h6", "p", "td", "pre",
};

/**
 * Is this node's tag one of those that need to be scored by default?
 */
static inline bool has_default_tag_to_score(htmlNodePtr node)
{
	return node_has_tag_array(node, ARRAY_SIZE(TAGS_TO_SCORE), TAGS_TO_SCORE);
}

/**
 * Do we know for sure that we won't need to score this node?
 *
 * This function may have the side-effect of setting the byline.
 */
static bool no_need_to_score(htmlNodePtr node)
{
	if (!is_node_visible(node))
		return true;
	if (check_byline(node))
		return true;
	if ((options.flags & OPT_STRIP_UNLIKELY) && is_node_unlikely(node))
		return true;

	/*
	 * fx: Remove DIV, SECTION, and HEADER nodes without any content
	 * (e.g. text, image, video, or iframe).
	 */
	return is_division_without_content(node);
}

/**
 * Initialize a node with a preliminary readability score.
 */
void initialize_node(htmlNodePtr node)
{
	if (node_has_tag(node, "div"))
		add_to_score(node, +5);
	else if (node_has_tag(node, "pre", "td", "blockquote"))
		add_to_score(node, +3);
	else if (node_has_tag(node, "address", "form"))
		add_to_score(node, -3);
	else if (node_has_tag(node, "ol", "ul", "dl", "dd", "dt", "li"))
		add_to_score(node, -3);
	else if (node_has_tag(node, "h1", "h2", "h3", "h4", "h5", "h6", "th"))
		add_to_score(node, -5);

	add_to_score(node, get_class_weight(node));
	mark_as_initialized(node);
}

/**
 * fx: Initialize and score ancestors
 */
static void assign_content_score_ancestors(htmlNodePtr node, int score)
{
	int level = 3; /* Only score 3 ancestors */

	for (node = node->parent; node && level; node = node->parent, level--) {
		if (!node->name)
			continue;
		if (!node->parent || node->parent->type != XML_ELEMENT_NODE)
			continue;
		if (!is_initialized(node)) {
			initialize_node(node);
			mark_as_candidate(node);
		}

		switch (level) {
		case 3:
			add_to_score(node, score);
			break;
		case 2:
			add_to_score(node, score / 2.0);
			break;
		case 1:
			add_to_score(node, score / 6.0);
			break;
		}
	}
}

/**
 * fx: assign a score to them based on how content-y they look. Then add their
 * score to their parent node. A score is determined by things like number of
 * commas, class names, etc. Maybe eventually link density.
 */
static void *assign_content_score(htmlNodePtr node)
{
	xmlChar *text = NULL;
	int length;
	int score = 0;

	if (!is_to_score(node))
		goto out;

	if (!node->parent || node->parent->type != XML_ELEMENT_NODE)
		goto out;

	text = node_get_normalized_content(node);
	length = text ? xmlUTF8Strlen(text) : 0;
	if (length < 25)
		goto out;

	++score; /* fx: Add a point for the paragraph itself as a base. */
	score += char_count((char *)text, ',') + 1; /* fx: points for commas */

	/*
	 * fx: For every 100 characters in this paragraph, add another point.
	 * Up to 3 points.
	 */
	score += MIN(length / 100, 3);

	/* fx: Initialize and score ancestors */
	assign_content_score_ancestors(node, score);

out:
	xmlFree(text);
	return NULL;
}

/*
 * fx: Because of our bonus system, parents of candidates might have scores
 * themselves. They get half of the node. There won't be nodes with higher
 * scores than our topCandidate, but if we see the score going *up* in the
 * first few steps up the tree, that's a decent sign that there might be more
 * content lurking in other places that we want to unify in. The sibling stuff
 * below does some of that - but only if we've looked high enough up the DOM
 * tree.
 */
static htmlNodePtr find_ancestor_with_more_content(htmlNodePtr node)
{
	htmlNodePtr ancestor;
	double lastscore = load_score(node);
	double score_threshold = lastscore / 3.0;

	for (ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
		double ancestor_score;

		if (node_has_tag(ancestor, "body"))
			break;

		ancestor_score = load_score(ancestor);
		if (!ancestor_score)
			continue;
		if (ancestor_score < score_threshold)
			break; /* fx: The scores shouldn't get too low */
		if (ancestor_score > lastscore)
			return ancestor; /* fx: Alright! We found a better parent to use */
		lastscore = load_score(ancestor);
	}
	return node;
}

/**
 * Is 'n1' an ancestor of 'n2'?
 */
static bool is_ancestor_of(htmlNodePtr n1, htmlNodePtr n2)
{
	while (n2) {
		if (n2 == n1)
			return true;
		n2 = n2->parent;
	}
	return false;
}

/*
 * fx: The number of top candidates to consider when analysing how tight the
 * competition is among candidates. TODO: override through cli option?
 */
#define DEFAULT_N_TOP_CANDIDATES 5

/**
 * Search for a better top candidate among the ancestors of the current one
 */
static htmlNodePtr find_better_top_candidate(htmlNodePtr *tops)
{
	htmlNodePtr topnode = tops[0];
	double topscore = load_score(topnode);
	htmlNodePtr ancestor;
	int i;

	if (!topscore)
		return topnode; /* Avoid division by zero */

	/*
	 * fx: Find a better top candidate node if it contains (at least three) node
	 * which belong to `topCandidates` array and whose scores are quite closed
	 * with current `topCandidate` node.
	 */
	for (ancestor = topnode->parent; ancestor; ancestor = ancestor->parent) {
		static const int MINIMUM_TOPCANDIDATES = 3;
		int contained_tops = 0;

		if (node_has_tag(ancestor, "body"))
			break;

		for (i = 1; i < DEFAULT_N_TOP_CANDIDATES && tops[i]; i++) {
			if (load_score(tops[i]) / topscore < 0.75)
				continue;
			if (!is_ancestor_of(ancestor, tops[i]))
				continue;
			++contained_tops;
		}

		if (contained_tops >= MINIMUM_TOPCANDIDATES) {
			topnode = ancestor;
			break;
		}
	}
	if (!is_initialized(topnode))
		initialize_node(topnode);

	topnode = find_ancestor_with_more_content(topnode);

	/*
	 * fx: If the top candidate is the only child, use parent instead. This
	 * will help sibling joining logic when adjacent content is actually
	 * located in parent's sibling node.
	 */
	while (xmlChildElementCount(topnode->parent) == 1) {
		if (!topnode->parent || node_has_tag(topnode->parent, "body"))
			break;
		topnode = topnode->parent;
	}
	if (!is_initialized(topnode))
		initialize_node(topnode);
	return topnode;
}

/**
 * Return the top candidate list, updated if appropriate to include the node
 */
static void *consider_for_top_list(htmlNodePtr node)
{
	static htmlNodePtr tops[DEFAULT_N_TOP_CANDIDATES] = {0};
	double score;
	int i;

	if (!is_candidate(node))
		return tops;

	score = load_score(node) * (1 - get_link_density(node));
	save_score(node, score);

	for (i = 0; i < DEFAULT_N_TOP_CANDIDATES; ++i) {
		int remaining;

		if (tops[i] && score <= load_score(tops[i]))
			continue;

		/* Make room for the new node among the top ones */
		remaining = DEFAULT_N_TOP_CANDIDATES - i - 1;
		memmove(&tops[i + 1], &tops[i], remaining * sizeof(*tops));
		tops[i] = node;
		break;
	}
	return tops;
}

/**
 * fx: After we've calculated scores, loop through all of the possible
 * candidate nodes we found and find the one with the highest score.
 */
static htmlNodePtr find_top_candidate(htmlDocPtr doc)
{
	htmlNodePtr *tops = run_on_nodes(doc, consider_for_top_list);
	htmlNodePtr result;

	if (!tops[0] || node_has_tag(tops[0], "body"))
		result = NULL;
	else
		result = find_better_top_candidate(tops);

	tops[0] = NULL; /* Static array: mark as empty so that it can be reused */
	return result;
}

/**
 * Get the body node for the document
 */
static htmlNodePtr get_body(htmlDocPtr doc)
{
	htmlNodePtr root = xmlDocGetRootElement(doc);
	htmlNodePtr child;

	for (child = root->children; child; child = child->next) {
		if (node_has_tag(child, "body"))
			return child;
	}
	fatal_msg("document has no body tag");
}

/**
 * fx: If we still have no top candidate, just use the body as a last resort.
 * We also have to copy the body node so it is something we can modify.
 */
static htmlNodePtr top_candidate_from_all(htmlDocPtr doc)
{
	htmlNodePtr body = get_body(doc);
	htmlNodePtr new, child, next;

	new = xmlNewNode(NULL, (xmlChar *)"div");
	if (!new)
		fatal();

	for (child = body->children; child; child = next) {
		next = child->next;
		xmlUnlinkNode(child);
		if (!xmlAddChild(new, child))
			fatal();
	}
	if (!xmlAddChild(body, new))
		fatal();

	initialize_node(new);
	return new;
}

/**
 * Append a node to the content node for the document
 */
static void append_content(htmlNodePtr content, htmlNodePtr node)
{
	static const char * const to_div_exc[] = {"div", "article", "section", "p"};

	if (!node_has_tag_array(node, ARRAY_SIZE(to_div_exc), to_div_exc)) {
		/*
		 * fx: We have a node that isn't a common block level element, like a
		 * form or td tag. Turn it into a div so it doesn't get filtered out
		 * later by accident.
		 */
		xmlNodeSetName(node, (xmlChar *)"div");
	}
	xmlUnlinkNode(node);
	if (!xmlAddChild(content, node))
		fatal();
}

/**
 * Is this node a paragraph with content?
 */
static bool is_paragraph_with_content(htmlNodePtr node)
{
	double link_density;
	xmlChar *content;
	int length;
	bool ret;

	if (!node_has_tag(node, "p"))
		return false;

	content = node_get_normalized_content(node);
	if (!content)
		return false;
	length = strlen((char *)content);
	link_density = get_link_density(node);

	ret = true;
	if (length > 80 && link_density < 0.25)
		goto out;
	if (link_density == 0 && regex_matches(&sentence_dot_re, content))
		goto out;

	ret = false;
out:
	xmlFree(content);
	return ret;
}

/**
 * fx: Now that we have the top candidate, look through its siblings for
 * content that might also be related. Things like preambles, content split
 * by ads that we removed, etc.
 */
static htmlNodePtr gather_related_content(htmlNodePtr top)
{
	htmlNodePtr parent = top->parent;
	double topscore = load_score(top);
	double score_threshold = MAX(topscore * 0.2, 10.0);
	htmlNodePtr child, next;
	htmlNodePtr content;
	xmlChar *topclass;

	content = xmlNewNode(NULL, (xmlChar *)"div");
	if (!content)
		fatal();

	topclass = xmlGetProp(top, (xmlChar *)"class");

	for (child = parent->children; child; child = next) {
		double score;
		double content_bonus = 0;
		xmlChar *class;

		next = child->next; /* The child may get unlinked at some point */

		if (child == top) {
			append_content(content, child);
			continue;
		}

		/*
		 * fx: Give a bonus if sibling nodes and top candidates have the
		 * example same classname
		 */
		class = xmlGetProp(child, (xmlChar *)"class");
		if (class && topclass)
			if (*class && strcasecmp((char *)class, (char *)topclass) == 0)
				content_bonus = topscore * 0.2;
		xmlFree(class);

		score = load_score(child);
		if (is_initialized(child) && score + content_bonus >= score_threshold) {
			append_content(content, child);
			continue;
		}

		if (is_paragraph_with_content(child))
			append_content(content, child);
	}

	xmlFree(topclass);
	return content;
}

/**
 * Set on this node the attributes expected for the main div of the article
 */
static void set_main_div_attrs(htmlNodePtr div)
{
	xmlSetProp(div, (xmlChar *)"id", (xmlChar *)"readability-page-1");
	xmlSetProp(div, (xmlChar *)"class", (xmlChar *)"page");
}

/**
 * Create a single main div for the given article
 */
static void create_main_div(htmlNodePtr article)
{
	htmlNodePtr div, child, next;

	div = xmlNewNode(NULL, (xmlChar *)"div");
	if (!div)
		fatal();
	set_main_div_attrs(div);

	for (child = article->children; child; child = next) {
		next = child->next;
		xmlUnlinkNode(child);
		xmlAddChild(div, child);
	}
	xmlAddChild(article, div);
}

/**
 * Save an extracted article in the list of previous attempts
 */
static void save_attempt(htmlNodePtr article, int article_len)
{
	struct attempt *slot;

	for (slot = &attempts[0]; slot != &attempts[4]; ++slot) {
		if (slot->article)
			continue;
		slot->article = article;
		slot->length = article_len;
		return;
	}
	assert(false);
}

/**
 * Do we need to keep working on extracting the contents? If true, save the
 * current attempt and tweak the flags for the next one.
 */
static bool needs_one_more_try(htmlNodePtr article)
{
	int article_len = text_normalized_content_length(article);

	/*
	 * fx: Now that we've gone through the full algorithm, check to see if we
	 * got any meaningful content. If we didn't, we may need to re-run
	 * grabArticle with different flags set. This gives us a higher likelihood
	 * of finding the content, and the sieve approach gives us a higher
	 * likelihood of finding the -right- content.
	 */
	save_attempt(article, article_len);
	if (article_len >= DEFAULT_CHAR_THRESHOLD)
		return false;

	if (options.flags & OPT_STRIP_UNLIKELY)
		options.flags ^= OPT_STRIP_UNLIKELY;
	else if (options.flags & OPT_WEIGHT_CLASSES)
		options.flags ^= OPT_WEIGHT_CLASSES;
	else if (options.flags & OPT_CLEAN_CONDITIONALLY)
		options.flags ^= OPT_CLEAN_CONDITIONALLY;
	else
		return false;

	return true;
}

/**
 * Free all articles in the attempt list except for the best one that is given
 */
static void free_attempts_except(htmlNodePtr best)
{
	struct attempt *slot;

	for (slot = &attempts[0]; slot != &attempts[4]; ++slot) {
		if (slot->article != best)
			free_node(slot->article);
	}
}

/**
 * Return the best extraction attempt, or NULL if all were a failure
 */
static htmlNodePtr get_best_attempt(void)
{
	htmlNodePtr article = NULL;
	struct attempt *slot, *best;

	/* fx: just return the longest text we found during the different loops */
	best = &attempts[0];
	for (slot = &attempts[1]; slot != &attempts[4]; ++slot) {
		if (slot->length <= best->length)
			continue;
		best = slot;
	}
	if (best->length) /* fx: But first check if we actually have something */
		article = best->article;
	return article;
}

/**
 * fx: Find out text direction from ancestors of final top candidate
 *
 * The given parent is the actual parent of the node in the original html, not
 * its current parent inside the extracted article.
 */
static void extract_text_direction(htmlNodePtr node, htmlNodePtr parent)
{
	htmlNodePtr ancestor = node;
	xmlChar *direction = NULL;

	while (ancestor && !direction) {
		if (ancestor->name)
			direction = xmlGetProp(ancestor, (xmlChar *)"dir");
		ancestor = (ancestor == node) ? parent : ancestor->parent;
	}
	if (!direction)
		return;

	metadata.direction = malloc(strlen((char *)direction) + 1);
	if (!metadata.direction)
		fatal_errno();
	strcpy(metadata.direction, (char *)direction);
	xmlFree(direction);
}

/**
 * fx: Using a variety of metrics (content score, classname, element types),
 * find the content that is most likely to be the stuff a user wants to read.
 * Then return it wrapped up in a div.
 */
static htmlNodePtr grab_article(htmlDocPtr doc)
{
	htmlNodePtr node, top, top_parent, article;
	htmlDocPtr tempdoc = NULL;

	do {
		bool top_is_new = false;

		/* We may got through several attempts, so preserve the original doc */
		free_doc(tempdoc);
		tempdoc = xmlCopyDoc(doc, 1 /* recursive */);
		if (!tempdoc)
			fatal();

		node = first_node(tempdoc);
		while (node) {
			if (no_need_to_score(node)) {
				node = remove_and_get_following(node);
				continue;
			}

			if (has_default_tag_to_score(node))
				mark_to_score(node);

			/*
			 * fx: Turn all divs that don't have children block level elements
			 * into p's
			 */
			if (node_has_tag(node, "div")) {
				node = handle_div_node_for_grab(node);
				continue;
			}
			node = following_node(node);
		}

		run_on_nodes(tempdoc, assign_content_score);
		top = find_top_candidate(tempdoc);
		if (!top) {
			top = top_candidate_from_all(tempdoc);
			top_is_new = true;
		}
		top_parent = top->parent; /* Save this before unlinking top */
		article = gather_related_content(top);

		/*
		 * fx: So we have all of the content that we need. Now we clean it up
		 * for presentation.
		 */
		prep_article(article);
		if (!article->children) /* Even the top candidate is gone */
			continue;

		if (top_is_new) /* fx: we already created a fake div thing... */
			set_main_div_attrs(top);
		else
			create_main_div(article);
	} while (needs_one_more_try(article));

	article = get_best_attempt();
	if (article)
		extract_text_direction(top, top_parent);
	free_attempts_except(article);

	free_doc(tempdoc);
	return article;
}

/**
 * Is this node an image placeholder?
 */
static bool is_image_placeholder(htmlNodePtr node)
{
	xmlAttrPtr attr;

	if (!node_has_tag(node, "img"))
		return false;

	for (attr = node->properties; attr; attr = attr->next) {
		char *name = (char *)attr->name;
		xmlChar *value;
		bool is_match;

		if (strcmp(name, "src") == 0 || strcmp(name, "srcset") == 0)
			return false;
		if (strcmp(name, "data-src") == 0 || strcmp(name, "data-srcset") == 0)
			return false;

		value = xmlGetProp(node, attr->name);
		is_match = regex_matches(&imgext_re, value);
		xmlFree(value);
		if (is_match)
			return false;
	}
	return true;
}

/**
 * fx: Check if node is image, or if node contains exactly only one image
 * whether as a direct child or as its descendants.
 *
 * Our version returns the image node directly, or NULL if the check failed.
 */
static htmlNodePtr get_single_image(htmlNodePtr node)
{
	if (node_has_tag(node, "img"))
		return node;

	while (node) {
		htmlNodePtr child, elem_child = NULL;

		child = node->children;
		for (child = node->children; child; child = child->next) {
			if (child->type == XML_ELEMENT_NODE) {
				if (elem_child)
					return NULL;
				elem_child = child;
			} else if (text_normalized_content_length(child)) {
				return NULL;
			}
		}
		if (node_has_tag(elem_child, "img"))
			return elem_child;
		node = elem_child;
	}

	return NULL;
}

/**
 * Could this attribute contain an image?
 */
static bool is_image_attr(const xmlChar *name, const xmlChar *value)
{
	if (!value || !*value)
		return false;

	if (strcasecmp((char *)name, "src") == 0)
		return true;
	if (strcasecmp((char *)name, "srcset") == 0)
		return true;
	if (regex_matches(&imgext_re, value))
		return true;

	return false;
}

/**
 * Copy attributes of an image element that might contain an image; preserve
 * those that already exist in the destination.
 */
static void copy_image_attrs(htmlNodePtr dest, htmlNodePtr src)
{
	xmlAttrPtr attr;

	for (attr = src->properties; attr; attr = attr->next) {
		xmlChar *srcval = NULL, *destval = NULL;
		xmlChar *backup_name = NULL;

		srcval = xmlGetProp(src, attr->name);
		if (!is_image_attr(attr->name, srcval))
			goto next;
		destval = xmlGetProp(dest, attr->name);

		if (!destval) {
			xmlSetProp(dest, attr->name, srcval);
			goto next;
		}
		if (xmlStrcmp(destval, srcval) == 0)
			goto next;

		/*
		 * This attribute already exists in the destination, but copy the
		 * source value anyway, under a different name.
		 */
		backup_name = xmlMalloc(1);
		if (!backup_name)
			fatal_errno();
		*backup_name = '\0';
		backup_name = xmlStrcat(backup_name, (xmlChar *)"data-old-");
		backup_name = xmlStrcat(backup_name, attr->name);
		xmlSetProp(dest, backup_name, srcval);
		xmlFree(backup_name);

next:
		xmlFree(destval);
		xmlFree(srcval);
	}
}

/**
 * If the node is a noscript element with a single image inside, try to use it
 * to replace a previous image.
 */
static void *unwrap_if_noscript_image(htmlNodePtr node)
{
	htmlNodePtr prev, newimg, oldimg;

	if (!node_has_tag(node, "noscript"))
		return NULL;
	newimg = get_single_image(node);
	if (!newimg)
		return NULL;

	prev = prev_element(node);
	if (!prev)
		return NULL;
	oldimg = get_single_image(prev);
	if (!oldimg)
		return NULL;

	/*
	 * fx: If noscript has previous sibling and it only contains image, replace
	 * it with noscript content. However we also keep old attributes that might
	 * contains image.
	 */
	copy_image_attrs(newimg, oldimg);
	xmlReplaceNode(prev, newimg);
	free_node(prev);
	return NULL;
}

/**
 * fx: Find all <noscript> that are located after <img> nodes, and which
 * contain only one <img> element. Replace the first image with the image
 * from inside the <noscript> tag, and remove the <noscript> tag. This
 * improves the quality of the images we use on some sites (e.g. Medium).
 */
static void unwrap_noscript_images(htmlDocPtr doc)
{
	/*
	 * fx: Find img without source or attributes that might contains image,
	 * and remove it. This is done to prevent a placeholder img is replaced
	 * by img from noscript in next step.
	 */
	remove_nodes_if(doc, is_image_placeholder);

	run_on_nodes(doc, unwrap_if_noscript_image);
}

/**
 * Is this a script or noscript node?
 */
static bool is_script_or_noscript(htmlNodePtr node)
{
	if (node_has_tag(node, "noscript"))
		return true;
	if (node_has_tag(node, "script")) {
		xmlAttrPtr src = xmlHasProp(node, (xmlChar *)"src");

		/* I don't see how this is necessary at all, but just follow firefox */
		if (src && xmlRemoveProp(src) < 0)
			fatal();
		xmlNodeSetContent(node, (xmlChar *)"");
		return true;
	}
	return false;
}

/**
 * Is this node the first <br> in a <br><br> sequence?
 */
static bool is_double_br(htmlNodePtr node)
{
	return node_has_tag(node, "br") && node_has_tag(next_element(node), "br");
}

/**
 * fx: Replaces 2 or more successive <br> elements with a single <p>.
 * Whitespace between <br> elements are ignored. For example:
 *   <div>foo<br>bar<br> <br><br>abc</div>
 * will become:
 *   <div>foo<br>bar<p>abc</p></div>
 */
static void *replace_brs(htmlNodePtr node)
{
	htmlNodePtr next;
	bool replaced = false;

	if (!node_has_tag(node, "br"))
		return NULL;

	for (next = next_element(node); next; next = next_element(node)) {
		if (!node_has_tag(next, "br"))
			break;
		replaced = true;
		xmlUnlinkNode(next);
		free_node(next);
	}
	if (!replaced)
		return NULL;

	xmlNodeSetName(node, (xmlChar *)"p");
	for (next = node->next; next; next = node->next) {
		/*
		 * fx: If we've hit another <br><br>, we're done adding children
		 * to this <p>.
		 */
		if (is_double_br(next) || !is_phrasing_content(next))
			break;

		/* fx: make this node a child of the new <p> */
		xmlUnlinkNode(next);
		if (!xmlAddChild(node, next))
			fatal();
	}
	prune_trailing_whitespace(node);

	if (node_has_tag(node->parent, "p"))
		xmlNodeSetName(node->parent, (xmlChar *)"div");
	return NULL;
}

/**
 * fx: Prepare the HTML document for readability to scrape it. This includes
 * things like stripping javascript [sic], CSS, and handling terrible markup.
 */
static void prep_document(htmlDocPtr doc)
{
	htmlNodePtr node = first_node(doc);

	while (node) {
		if (node_has_tag(node, "style")) {
			node = remove_and_get_following(node);
		} else if (node_has_tag(node, "font")) {
			xmlNodeSetName(node, (xmlChar *)"span");
			node = following_node(node);
		} else {
			node = following_node(node);
		}
	}
	run_on_nodes(doc, replace_brs);
}

/**
 * Replace a relative URL with its absolute version
 */
static void to_absolute_url(xmlChar **url)
{
	xmlChar *original = *url;
	xmlChar *result;
	size_t i;

	/* fx: Leave hash links alone if the base URI matches the document URI */
	if (!(options.flags & OPT_URL_OVERRIDE) && original[0] == '#')
		return;

	/* Trailing whitespace in the URL seems to confuse libxml2 */
	i = strlen((char *)original);
	while (i--) {
		if (!isspace(original[i]))
			break;
		original[i] = '\0';
	}

	result = xmlBuildURI(original, (xmlChar *)options.base_url);
	if (!result)
		return;
	xmlFree(original);
	*url = result;
}

/**
 * Remove a node but preserve its children in the same location; return a
 * pointer to this new node.
 */
static htmlNodePtr remove_but_preserve_content(htmlNodePtr node)
{
	htmlNodePtr child = node->children;
	htmlNodePtr next;
	htmlNodePtr new;

	/*
	 * fx: if the link only contains simple text content, it can be converted
	 * to a text node
	 */
	if (child && !child->next && xmlNodeIsText(child)) {
		xmlChar *content;

		content = xmlNodeGetContent(child);
		new = xmlNewText(content);
		xmlFree(content);
		goto replace;
	}

	/* fx: if the link has multiple children, they should all be preserved */
	new = xmlNewNode(NULL, (xmlChar *)"span");
	for (child = node->children; child; child = next) {
		next = child->next;
		xmlUnlinkNode(child);
		if (!xmlAddChild(new, child))
			fatal();
	}

replace:
	xmlReplaceNode(node, new);
	free_node(node);
	return new;
}

/**
 * If the node is a link, get rid of any relative or javascript URLs. If this
 * involves replacing the node altogether, return the new node in its location.
 */
static htmlNodePtr fix_non_absolute_link(htmlNodePtr node)
{
	xmlChar *href = NULL;

	if (!node_has_tag(node, "a"))
		goto out;
	href = xmlGetProp(node, (xmlChar *)"href");
	if (!href)
		goto out;

	if (xmlStrcasestr(href, (xmlChar *)"javascript:")) {
		/* fx: Remove links with javascript: URIs */
		node = remove_but_preserve_content(node);
		goto out;
	}

	to_absolute_url(&href);
	xmlSetProp(node, (xmlChar *)"href", href);

out:
	xmlFree(href);
	return node;
}

struct srcset_entry {
	char *url;
	char *size;
};

/**
 * Parse a single item from a srcset into the url and size buffers; return the
 * length of the item, or 0 on failure.
 */
static int parse_srcset_item(const char *srcset, char *url, char *size)
{
	const char *start = srcset;
	int i;

	url[0] = size[0] = '\0';

	for (; isspace(*srcset); ++srcset);

	for (i = 0; *srcset && !isspace(*srcset); ++srcset, ++i)
		url[i] = *srcset;
	if (i == 0)
		return 0;

	if (url[i - 1] == ',') {
		url[i - 1] = '\0';
		return srcset - start;
	}
	url[i] = '\0';

	for (; isspace(*srcset); ++srcset);

	/* If a srcset item has a size, it doesn't need a space after the comma */
	for (i = 0; *srcset && *srcset != ','; ++srcset, ++i)
		size[i] = *srcset;
	size[i] = '\0';
	if (*srcset == ',')
		++srcset;

	return srcset - start;
}

/**
 * Parse a srcset property into an array of srcset_entry structs
 */
static struct srcset_entry *parse_srcset(const char *srcset)
{
	size_t len = strlen(srcset) + 1;
	struct srcset_entry *ents;
	int url_bound, i;

	/* Only an upper bound, because there might be commas inside the URLs */
	url_bound = char_count(srcset, ',') + 1;
	/* The extra NULL entry at the end acts as a terminator */
	ents = calloc(url_bound + 1, sizeof(*ents));

	for (i = 0; i < url_bound; ++i) {
		char *url, *size;
		int ret;

		/* Again, extremely coarse upper bounds */
		url = malloc(len);
		size = malloc(len);
		if (!url || !size)
			fatal();

		ret = parse_srcset_item(srcset, url, size);
		if (!ret) {
			free(url);
			free(size);
			break;
		}
		ents[i].url = url;
		ents[i].size = size;
		srcset += ret;
	}
	return ents;
}

/**
 * Replace relative with absolute URLs in an array of srcset_entry structs
 */
static void to_absolute_srcset_entries(struct srcset_entry *ents)
{
	while (ents->url) {
		to_absolute_url((xmlChar **)&ents->url);
		++ents;
	}
}

/**
 * Assemble a srcset property from an array of srcset_entry structs
 */
static char *build_srcset(struct srcset_entry *ents)
{
	struct srcset_entry *curr;
	char *srcset;
	size_t len = 0;

	/* Find the length for the new srcset */
	curr = ents;
	while (curr->url) {
		size_t url_len, sizelen;

		url_len = strlen(curr->url);
		sizelen = curr->size ? strlen(curr->size) : 0;
		if (url_len > 4096 || sizelen > 4096)
			break;
		len += url_len + sizelen + 3; /* Two spaces and a comma */
		++curr;
	}
	len += 1; /* The null termination */

	srcset = malloc(len);
	if (!srcset)
		fatal_errno();
	srcset[0] = '\0';

	/* Now get it assembled */
	curr = ents;
	while (curr->url) {
		if (curr != ents)
			strcat(srcset, ", ");
		strcat(srcset, curr->url);
		if (*curr->size) {
			strcat(srcset, " ");
			strcat(srcset, curr->size);
		}
		++curr;
	}
	return srcset;
}

/**
 * Clean up an array of srcset_entry structs
 */
static void free_srcset(struct srcset_entry *ents)
{
	struct srcset_entry *curr = ents;

	while (curr->url) {
		free(curr->url);
		free(curr->size);
		++curr;
	}
	free(ents);
}

/**
 * Convert all relative URLs in a srcset to absolute URLs
 */
static void to_absolute_srcset(xmlChar **srcset_p)
{
	struct srcset_entry *ents;

	ents = parse_srcset((char *)*srcset_p);
	to_absolute_srcset_entries(ents);
	xmlFree(*srcset_p);
	*srcset_p = (xmlChar *)build_srcset(ents);
	free_srcset(ents);
}

static const char * const MEDIA_ELEMS[] = {
	"img", "picture", "figure", "video", "audio", "source",
};

/**
 * If the node is a media element, convert it to an absolute URL
 */
static htmlNodePtr fix_relative_media(htmlNodePtr node)
{
	xmlChar *urls;

	if (!node_has_tag_array(node, ARRAY_SIZE(MEDIA_ELEMS), MEDIA_ELEMS))
		return node;

	urls = xmlGetProp(node, (xmlChar *)"src");
	if (urls) {
		to_absolute_url(&urls);
		xmlSetProp(node, (xmlChar *)"src", urls);
		xmlFree(urls);
	}
	urls = xmlGetProp(node, (xmlChar *)"poster");
	if (urls) {
		to_absolute_url(&urls);
		xmlSetProp(node, (xmlChar *)"poster", urls);
		xmlFree(urls);
	}
	urls = xmlGetProp(node, (xmlChar *)"srcset");
	if (urls) {
		to_absolute_srcset(&urls);
		xmlSetProp(node, (xmlChar *)"srcset", urls);
		xmlFree(urls);
	}
	return node;
}

/**
 * fx: Converts each <a> and <img> uri in the given element to an absolute URI,
 * ignoring #ref URIs.
 *
 * TODO: add a cli option to preserve the relative URLs
 */
static void fix_all_relative_urls(htmlNodePtr article)
{
	change_descendants(article, fix_non_absolute_link);
	change_descendants(article, fix_relative_media);
}

/**
 * fx: Removes the class="" attribute [...], except those that match
 * CLASSES_TO_PRESERVE and the classesToPreserve array from the options object.
 *
 * TODO: add cli option to preserve other classes, or all of them
 */
static htmlNodePtr clean_classes(htmlNodePtr node)
{
	xmlChar *class_list;
	char *class;

	class_list = xmlGetProp(node, (xmlChar *)"class");
	if (!class_list)
		return node;

	class = strtok((char *)class_list, " ");
	while (class) {
		if (strcmp((char *)class, "page") == 0) /* Set by ourselves */
			break;
		class = strtok(NULL, " ");
	}
	if (class) /* The node has the page class */
		xmlSetProp(node, (xmlChar *)"class", (xmlChar *)"page");
	else
		xmlUnsetProp(node, (xmlChar *)"class");

	xmlFree(class_list);
	return node;
}

/**
 * If this is a text node, normalize it
 */
static htmlNodePtr clean_if_text_node(htmlNodePtr node)
{
	/*
	 * libxml2 will indent tags even inside preformatted blocks, so get
	 * rid of the inner tag entirely.
	 */
	if (node_has_tag(node, "code") && node_has_tag(node->parent, "pre")) {
		htmlNodePtr parent = node->parent;

		xmlReplaceNode(parent, node);
		free_node(parent);
		xmlNodeSetName(node, (xmlChar *)"pre");
	} else if (xmlNodeIsText(node)) {
		xmlChar *content = node_get_normalized_or_preformatted(node);

		xmlNodeSetContent(node, content);
		xmlFree(content);
	}
	return node;
}

/**
 * If the document provides a base URL, set it in the global options
 */
void set_base_url_from_doc(htmlDocPtr doc)
{
	xmlChar *meta_url;

	meta_url = xmlGetProp(first_node_with_tag(doc, "base"), (xmlChar *)"href");
	if (!meta_url)
		return;

	to_absolute_url(&meta_url);
	options.base_url = (char *)meta_url;
	options.flags |= OPT_URL_OVERRIDE;
}

/**
 * Is the node a comment?
 */
static bool is_comment(htmlNodePtr node)
{
	return node->type == XML_COMMENT_NODE;
}

/**
 * Put an empty text node inside this element if it's not allowed to be
 * self-closing, otherwise libxml2 will make it so.
 *
 * We only focus on the elements that caused actual problems in our tests,
 * others can be added later if needed.
 */
static htmlNodePtr fill_if_not_self_closing(htmlNodePtr node)
{
	if (node_has_tag(node, "iframe", "em", "a") && !node->children)
		xmlNodeSetContent(node, (xmlChar *)" ");
	return node;
}

/**
 * Get rid of the siblings for the document's root node
 */
static void remove_root_siblings(htmlDocPtr doc)
{
	htmlNodePtr root = xmlDocGetRootElement(doc);
	htmlNodePtr sib;

	assert(root);

	for (sib = root->next; sib; sib = root->next) {
		xmlUnlinkNode(sib);
		free_node(sib);
	}
	for (sib = root->prev; sib; sib = root->prev) {
		xmlUnlinkNode(sib);
		free_node(sib);
	}
}

/**
 * Extract the content from an article's first paragraph
 */
static char *first_paragraph_content(htmlNodePtr article)
{
	htmlNodePtr node = first_descendant_with_tag(article, "p");

	return (char *)node_get_normalized_content(node);
}

/**
 * Clean up the extracted metadata for presentation
 */
static void clean_metadata(void)
{
	trim_and_unescape(&metadata.title);
	trim_and_unescape(&metadata.byline);
	trim_and_unescape(&metadata.excerpt);
	trim_and_unescape(&metadata.site_name);
}

/**
 * fx: Runs readability.
 *
 * Workflow:
 *  1. Prep the document by removing script tags, css, etc.
 *  2. Build readability's DOM tree.
 *  3. Grab the article content from the current dom tree.
 *  4. Replace the current DOM tree with the new one.
 *  5. Read peacefully.
 */
htmlNodePtr parse(htmlDocPtr doc)
{
	htmlNodePtr article, content;

	if (!xmlDocGetRootElement(doc))
		return NULL;

	/* Do this early to prevent problems when traversing the tree */
	remove_root_siblings(doc);

	set_base_url_from_doc(doc);

	remove_nodes_if(doc, is_comment);
	unwrap_noscript_images(doc);
	remove_nodes_if(doc, is_script_or_noscript);
	prep_document(doc);
	get_article_metadata(doc);

	article = grab_article(doc);
	if (!article)
		return NULL;

	fix_all_relative_urls(article);
	change_descendants(article, clean_classes);
	change_descendants(article, clean_if_text_node);
	/* We wouldn't need this if we could print/save a single node as html */
	change_descendants(article, fill_if_not_self_closing);

	if (!metadata.excerpt)
		metadata.excerpt = first_paragraph_content(article);
	clean_metadata();

	/* Discard the wrapping div */
	content = article->children;
	xmlUnlinkNode(content);
	free_node(article);
	return content;
}
