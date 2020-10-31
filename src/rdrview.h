/*
 * The single header file for rdrview
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

#ifndef RDRVIEW_H
#define RDRVIEW_H

#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>
#include <regex.h>
#include <libxml/HTMLtree.h>
#include <libxml/debugXML.h>

/* Cli options, plus some internal configuration */
struct options {
	unsigned int flags;
	bool disable_sandbox; /* Keep this separate from the other flags */
	char *enc; /* Character encoding */
	const char *template; /* Fields to include in the extracted article */
	const char *base_url;
	const char *browser;
	const char *url;
	FILE *localfile; /* Used only if the "url" is actually a local path */
};
extern struct options options;

/* Flags for the options struct */
#define OPT_HTML (1 << 0) /* Output the html for the article */
#define OPT_METADATA (1 << 1) /* Output the metadata for the article */
#define OPT_CHECK (1 << 2) /* Check if the doc looks readerable and exit */
#define OPT_BROWSER (1 << 3) /* Open the article in a browser */
#define OPT_URL_OVERRIDE (1 << 4) /* Override the base url */
#define OPT_STRIP_UNLIKELY (1 << 5) /* Remove unlikely nodes early */
#define OPT_WEIGHT_CLASSES (1 << 6) /* Consider classes for node score */
#define OPT_CLEAN_CONDITIONALLY (1 << 7) /* Remove fishy nodes on prep */

/* fx: number of chars an article must have in order to return a result */
#define DEFAULT_CHAR_THRESHOLD 500

/* Metadata extracted from the article */
struct metadata {
	char *title;
	char *byline;
	char *excerpt;
	char *site_name;
	char *direction;
};
extern struct metadata metadata;

/* Extra information we might attach to a node via its _private field */
struct node_info {
	unsigned char flags;
	double score;
};

/* Flags for the node_info struct */
#define NODE_TO_SCORE (1 << 0) /* The node needs to be scored */
#define NODE_INITIALIZED (1 << 1) /* The node's score is initialized */
#define NODE_CANDIDATE (1 << 2) /* The node is marked as a candidate */
#define NODE_TOP_CANDIDATE (1 << 3) /* The node is marked as a top candidate */
#define NODE_DATATABLE (1 << 4) /* The node is marked as a data table */

/* content.c */
extern void trim_and_unescape(char **str);
extern void strcpy_normalize(xmlChar *dest, const xmlChar *src);
extern xmlChar *node_get_normalized_content(htmlNodePtr node);
extern xmlChar *node_get_normalized_or_preformatted(htmlNodePtr node);
extern int text_normalized_content_length(htmlNodePtr node);
extern int text_content_length(htmlNodePtr node);
extern int char_count(const char *str, char c);
extern double get_link_density(htmlNodePtr node);
extern bool is_phrasing_content(htmlNodePtr node);
extern htmlNodePtr has_single_tag_inside(htmlNodePtr node, const char *tag);
extern int word_count(const char *str, bool separators_are_spaces);
extern char *find_last_separator(char *str);
extern void replace_char(char *str, int old, int new);
extern bool word_in_str(const char *str, const char *word);

/* iterator.c */
typedef bool (*condition_fn)(htmlNodePtr);
typedef bool (*condition_fn2)(htmlNodePtr, const void *);
typedef void *(*action_fn)(htmlNodePtr);
typedef htmlNodePtr (replace_fn)(htmlNodePtr);
typedef double (*calc_fn)(htmlNodePtr);
extern htmlNodePtr first_node(htmlDocPtr doc);
extern htmlNodePtr skip_node_descendants(htmlNodePtr node);
extern htmlNodePtr following_node(htmlNodePtr node);
extern xmlNodePtr remove_and_get_following(xmlNodePtr node);
extern void remove_descendants_if(htmlNodePtr node, condition_fn check);
extern void remove_nodes_if(htmlDocPtr doc, condition_fn check);
extern bool forall_descendants(htmlNodePtr node, condition_fn check);
extern bool such_desc_exists(htmlNodePtr node, condition_fn2 check, const void *data);
extern bool such_node_exists(htmlDocPtr doc, condition_fn2 check, const void *data);
extern bool has_such_descendant(htmlNodePtr node, condition_fn check);
extern void *run_on_nodes(htmlDocPtr doc, action_fn act);
extern void change_descendants(htmlNodePtr node, replace_fn replace);
extern double total_for_descendants(htmlNodePtr node, calc_fn calc);
extern int count_such_descs(htmlNodePtr node, condition_fn2 check, const void *data);
extern htmlNodePtr first_descendant_with_tag(htmlNodePtr node, const char *tag);
extern htmlNodePtr first_node_with_tag(htmlDocPtr doc, const char *tag);
extern void bw_remove_descendants_if(htmlNodePtr node, condition_fn2 check, const void *data);
extern htmlNodePtr next_element(htmlNodePtr node);
extern htmlNodePtr prev_element(htmlNodePtr node);

/* node.c */
extern struct node_info *allocate_node_info(htmlNodePtr node);
extern void free_node(htmlNodePtr node);
extern void free_doc(htmlDocPtr doc);
extern bool node_has_tag_array(htmlNodePtr node, int tagc, const char * const tagv[]);
extern htmlNodePtr has_ancestor_tag(htmlNodePtr node, const char *tag);
extern bool node_has_unlikely_class_id(htmlNodePtr node);
extern bool is_node_visible(htmlNodePtr node);
extern int get_class_weight(htmlNodePtr node);
extern bool attrcmp(htmlNodePtr node, const char *attrname, const char *str);

/* prep_article.c */
extern void prep_article(htmlNodePtr article);

/* readability.c */
extern htmlNodePtr parse(htmlDocPtr doc);

/* readerable.c */
extern bool is_probably_readerable(htmlDocPtr doc);

/* regex.c */
extern regex_t unlikely_re, candidate_re, byline_re, property_re, name_re;
extern regex_t imgext_re, hascontent_re, negative_re, positive_re;
extern regex_t sentence_dot_re, b64_dataurl_re, srcset_re, src_re, videos_re;
extern regex_t share_re, absolute_re;
extern void init_regexes(void);
extern bool regex_matches(const regex_t *preg, const xmlChar *string);

/* sandbox.c */
extern void start_sandbox(void);

/* rdrview.c */
extern __attribute__((noreturn)) void fatal_errno(void);
extern __attribute__((noreturn)) void fatal_msg(char *message);
extern __attribute__((noreturn)) void fatal_with_loc(const char *fn, int line);
#define fatal() fatal_with_loc(__func__, __LINE__)

/**
 * Get the size of an array
 */
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * Return the minimum/maximum of two numbers
 */
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

/**
 * Check if a node has one of the given (lowercase) tag names
 */
#define node_has_tag(node, tags...) \
({ \
	const char *tagv[] = {tags}; \
	int tagc = sizeof(tagv) / sizeof(tagv[0]); \
	node_has_tag_array(node, tagc, tagv); \
})

/**
 * Mark a node as initialized
 */
static inline void mark_as_initialized(htmlNodePtr node)
{
	struct node_info *ni = allocate_node_info(node);

	ni->flags |= NODE_INITIALIZED;
}

/**
 * Have we initialized the readability score for this node?
 */
static inline bool is_initialized(htmlNodePtr node)
{
	struct node_info *ni = node->_private;

	return ni ? ni->flags & NODE_INITIALIZED : false;
}

/**
 * Mark a node as a data table
 */
static inline void mark_as_data_table(htmlNodePtr node)
{
	struct node_info *ni = allocate_node_info(node);

	ni->flags |= NODE_DATATABLE;
}

/**
 * Is this node a data table?
 */
static inline bool is_data_table(htmlNodePtr node)
{
	struct node_info *ni = node->_private;

	return ni ? ni->flags & NODE_DATATABLE : false;
}

/**
 * Mark a node as a candidate
 */
static inline void mark_as_candidate(htmlNodePtr node)
{
	struct node_info *ni = allocate_node_info(node);

	ni->flags |= NODE_CANDIDATE;
}

/**
 * Is this node marked as candidate?
 */
static inline bool is_candidate(htmlNodePtr node)
{
	struct node_info *ni = node->_private;

	return ni ? ni->flags & NODE_CANDIDATE : false;
}

/**
 * Mark a node to remember that it needs to be scored
 */
static inline void mark_to_score(htmlNodePtr node)
{
	struct node_info *ni = allocate_node_info(node);

	ni->flags |= NODE_TO_SCORE;
}

/**
 * Does this node need to be scored?
 */
static inline bool is_to_score(htmlNodePtr node)
{
	struct node_info *ni = node->_private;

	return ni ? ni->flags & NODE_TO_SCORE : false;
}

/**
 * Save a new value to the node's info score field
 */
static inline void save_score(htmlNodePtr node, double score)
{
	struct node_info *ni = allocate_node_info(node);

	ni->score = score;
}

/**
 * Load the readability score stored at the node's info structure
 */
static inline double load_score(htmlNodePtr node)
{
	struct node_info *ni = node->_private;

	return ni ? ni->score : 0;
}

/**
 * Add a number to the readability score for the node
 */
static inline void add_to_score(htmlNodePtr node, double change)
{
	struct node_info *ni = allocate_node_info(node);

	ni->score += change;
}

#endif /* RDRVIEW_H */
