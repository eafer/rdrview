/*
 * All regular expressions used by rdrview
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

#include "rdrview.h"

/*
 * These are all the regex literals provided with Readability.js
 * TODO: test that no regex was broken by the transpilation
 */
static const char * const UNLIKELY_RE =
	"-ad-|ai2html|banner|breadcrumbs|combx|comment|community|cover-wrap|disqus|"
	"extra|footer|gdpr|header|legends|menu|related|remark|replies|rss|shoutbox|"
	"sidebar|skyscraper|social|sponsor|supplemental|ad-break|agegate|"
	"pagination|pager|popup|yom-remote";

static const char * const CANDIDATE_RE =
	"and|article|body|column|content|main|shadow";

static const char * const BYLINE_RE =
	"byline|author|dateline|writtenby|p-author";

static const char * const PROPERTY_RE =
	"[[:space:]]*(dc|dcterm|og|twitter)[[:space:]]*:"
	"[[:space:]]*(author|creator|description|title|site_name)[[:space:]]*";

static const char * const NAME_RE =
	"^[[:space:]]*((dc|dcterm|og|twitter|weibo:(article|webpage))[[:space:]]*"
	"[\\.:][[:space:]]*)?(author|creator|description|title|site_name)"
	"[[:space:]]*$";

static const char * const IMGEXT_RE = "\\.(jpg|jpeg|png|webp)";

static const char * const HASCONTENT_RE = "[^[:space:]]$";

static const char * const NEGATIVE_RE =
	"hidden|^hid$| hid$| hid |^hid |banner|combx|comment|com-|contact|foot|"
	"footer|footnote|gdpr|masthead|media|meta|outbrain|promo|related|scroll|"
	"share|shoutbox|sidebar|skyscraper|sponsor|shopping|tags|tool|widget";

static const char * const POSITIVE_RE =
	"article|body|content|entry|hentry|h-entry|main|page|pagination|post|"
	"text|blog|story";

static const char * const SENTENCE_DOT_RE = "\\.( |$)";

static const char * const B64_DATAURL_RE =
	"^data:[[:space:]]*[^[:space:];,]+[[:space:]]*;"
	"[[:space:]]*base64[[:space:]]*,";

static const char * const SRCSET_RE =
	"\\.(jpg|jpeg|png|webp)[[:space:]]+[[:digit:]]";

static const char * const SRC_RE =
	"^[[:space:]]*[^[:space:]]+\\.(jpg|jpeg|png|webp)"
	"[^[:space:]]*[[:space:]]*$";

static const char * const VIDEOS_RE =
	"//(www\\.)?((dailymotion|youtube|youtube-nocookie|player\\.vimeo|v\\.qq)"
	"\\.com|(archive|upload\\.wikimedia)\\.org|player\\.twitch\\.tv)";

static const char * const SHARE_RE =
	"(^|[[:space:]]|_)(share|sharedaddy)($|[[:space:]]|_)";

static const char * const ABSOLUTE_RE = "^([[:alpha:]]+:)?//";


/* These are the compiled regexes that will actually get exported */
regex_t unlikely_re, candidate_re, byline_re, property_re, name_re, imgext_re;
regex_t hascontent_re, negative_re, positive_re, sentence_dot_re;
regex_t b64_dataurl_re, srcset_re, src_re, videos_re, share_re, absolute_re;

/* List of all literal regexes */
static const char * const REGEXES[] = {
	UNLIKELY_RE, CANDIDATE_RE, BYLINE_RE, PROPERTY_RE, NAME_RE, IMGEXT_RE,
	HASCONTENT_RE, NEGATIVE_RE, POSITIVE_RE, SENTENCE_DOT_RE, B64_DATAURL_RE,
	SRCSET_RE, SRC_RE, VIDEOS_RE, SHARE_RE, ABSOLUTE_RE,
};

/* List of pointers to all compiled regexes, in the same order as above */
static regex_t * const PREGS[] = {
	&unlikely_re, &candidate_re, &byline_re, &property_re, &name_re,
	&imgext_re, &hascontent_re, &negative_re, &positive_re, &sentence_dot_re,
	&b64_dataurl_re, &srcset_re, &src_re, &videos_re, &share_re, &absolute_re,
};

/**
 * Compile all regexes to be used
 */
void init_regexes(void)
{
	int cflags = REG_EXTENDED | REG_ICASE | REG_NOSUB;
	unsigned int i;

	assert(ARRAY_SIZE(REGEXES) == ARRAY_SIZE(PREGS));

	for (i = 0; i < ARRAY_SIZE(REGEXES); ++i) {
		if (regcomp(PREGS[i], REGEXES[i], cflags))
			fatal();
	}
}

/**
 * Check if a string matches a precompiled regular expression
 */
bool regex_matches(const regex_t *preg, const xmlChar *string)
{
	if (!string)
		return false;
	return !regexec(preg, (char *)string, 0, NULL, 0);
}
