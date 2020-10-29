/*
 * Main routines for rdrview
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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <iconv.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <curl/curl.h>
#include <libxml/tree.h>
#include <libxml/HTMLparser.h>
#include <libxml/encoding.h>
#include "rdrview.h"

/* Program name */
static char *progname;

/*
 * Full path name for the temporary files; these must be static so that we
 * can do cleanup from the signal handler on abnormal process termination.
 */
static char *tmpdir;
static char *inputfile;
static char *outputfile;

#define MAX_ENC_LEN 31 /* Arbitrary length limit for character encodings */
struct options options = {0};

/* Use STR() for macro stringification */
#define STR_INNER(X) #X
#define STR(X) STR_INNER(X)

/**
 * Print the location of the issue and exit with an error code
 */
__attribute__((noreturn)) void fatal_with_loc(const char *fn, int line)
{
	char *FORMAT = "%s: fatal error in function %s(), line %d\n";

	fprintf(stderr, FORMAT, progname, fn, line);
	exit(1);
}

/**
 * Print a message and exit with an error code
 */
__attribute__((noreturn)) void fatal_msg(char *message)
{
	fprintf(stderr, "%s: %s\n", progname, message);
	exit(1);
}

/**
 * Print the message for the current errno value and exit with an error code
 */
__attribute__((noreturn)) void fatal_errno(void)
{
	perror(progname);
	exit(1);
}

/**
 * Print usage information and exit
 */
static void usage(void)
{
	char *args = "[-v] [-u base-url] [-E encoding] [-c|-H|-M|-B browser] [URL]";

	fprintf(stderr, "usage: %s %s\n", progname, args);
	exit(1);
}

/**
 * Return a newly allocated string with the requested format
 */
__attribute__((format(printf, 1, 2)))
static char *mkstring(const char *format, ...)
{
	va_list args;
	int size;
	char *str;

	va_start(args, format);
	size = vsnprintf(NULL, 0, format, args);
	va_end(args);
	if (size < 0)
		fatal();

	str = malloc(++size); /* One more for the null termination */
	if (!str)
		fatal_errno();

	va_start(args, format);
	size = vsnprintf(str, size, format, args);
	va_end(args);
	if (size < 0)
		fatal();

	return str;
}

/**
 * Reserve a filepath for a temporary file; return the path.
 *
 * We put the file inside a directory of our own, to protect against some old
 * implementations of the same-origin policy.
 */
static char *get_temp_filepath(const char *filename)
{
	static char *dir_template;
	char *envdir = getenv("TMPDIR");

	if (!tmpdir) {
		dir_template = mkstring("%s/XXXXXX", envdir ? envdir : "/tmp");
		tmpdir = mkdtemp(dir_template);
		if (!tmpdir)
			fatal_errno();
	}
	return mkstring("%s/%s", tmpdir, filename);
}

#if 0
/**
 * Get the encoding for the html document from the headers of the HTTP response
 *
 * TODO: this is commented out for now because it's dangerous and outside the
 * sandbox. It doesn't matter much because most websites provide the encoding
 * inside the html as well.
 */
static void set_encoding_from_response(CURL *curl)
{
	const char *type = NULL;
	const char *charset = NULL;

	/* Just ignore failures, we can guess the encoding or get it elsewhere */
	if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) || !type)
		return;
	charset = strcasestr(type, "charset="); /* TODO: whitespace? */
	if (!charset)
		return;

	if (!options.encoding) {
		options.encoding = malloc(MAX_ENC_LEN + 1);
		if (!options.encoding)
			fatal();
		*options.encoding = '\0';
	}

	/* Even if the user requested an encoding, obey the HTTP headers */
	sscanf(charset, "%*[^=]=%" STR(MAX_ENC_LEN) "s", options.encoding);
}
#else
static void set_encoding_from_response(CURL *curl)
{
	(void)curl;
}
#endif

/**
 * Save stdin to a temporary file
 */
static void stdin_to_file(FILE *file)
{
	char *buf;

	buf = malloc(4096);
	if (!buf)
		fatal_errno();

	while (!feof(stdin)) {
		ssize_t ret;

		ret = fread(buf, 1, 4096, stdin);
		fwrite(buf, 1, ret, file);
		if (ferror(stdin) || ferror(file))
			fatal_msg("I/O error");
	}

	if (fflush(file))
		fatal_errno();
	free(buf);
}

/**
 * Fetch the webpage and save it to a temporary file; exit on failure
 */
static void url_to_file(FILE *file)
{
	CURL *curl;
	CURLcode res;
	long protocols;

	curl = curl_easy_init();
	if (!curl)
		fatal_msg("libcurl could not be initialized");
	if (curl_easy_setopt(curl, CURLOPT_URL, options.url))
		fatal();
	if (curl_easy_setopt(curl, CURLOPT_WRITEDATA, file))
		fatal();

	/* I don't expect any other protocols, so be safe */
	protocols = CURLPROTO_HTTP | CURLPROTO_HTTPS;
	if (curl_easy_setopt(curl, CURLOPT_PROTOCOLS, protocols))
		fatal();

	/* Follow up to 50 redirections, like the curl cli does by default */
	if (curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1))
		fatal();
	if (curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50))
		fatal();

	res = curl_easy_perform(curl);
	if (res == CURLE_UNSUPPORTED_PROTOCOL)
		fatal_msg("unsupported url protocol");
	else if (res)
		fatal_msg("couldn't fetch the webpage");

	set_encoding_from_response(curl);
	curl_easy_cleanup(curl);
}

/**
 * Map a whole file to memory; return the length of the mapping
 */
static size_t map_file(FILE *file, char **map)
{
	int fd = fileno(file);
	struct stat statbuf;
	size_t size;

	if (fstat(fd, &statbuf))
		fatal_errno();
	size = statbuf.st_size;
	if (!size)
		fatal_msg("the document is empty");

	*map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (*map == MAP_FAILED)
		fatal_errno();
	return size;
}

/**
 * Overwrite text between script tags with X's; this gets rid of any closing
 * tags, which would terminate the CDATA section and cause libxml2 to choke.
 *
 * We search through the text as ascii, which will fail for some encodings,
 * but is not likely to corrupt the page because the "<script" ascii string
 * would have to randomly show up somewhere.
 */
static void invalidate_script_cdata(char *map, size_t size)
{
	char lastchar;
	const char *opentag;

	if (!size)
		fatal();

	/* Work with the file as a string, and restore the last character later */
	lastchar = map[size - 1];
	map[size - 1] = '\0';

	opentag = (char *)xmlStrcasestr(BAD_CAST map, BAD_CAST "<script");
	while (opentag) {
		char *gt;
		const char *closetag;

		gt = strchr(opentag, '>');
		if (!gt)
			break; /* Malformed html, just ignore it for now and move on */
		if (*(gt - 1) == '/') { /* No closing tag for this node */
			opentag = (char *)xmlStrcasestr(BAD_CAST gt, BAD_CAST "<script");
			continue;
		}
		closetag = (char *)xmlStrcasestr(BAD_CAST gt, BAD_CAST "</script>");
		if (!closetag)
			break; /* Malformed html, just ignore it for now and move on */
		memset(gt + 1, 'X', closetag - (gt + 1));
		opentag = (char *)xmlStrcasestr(BAD_CAST closetag, BAD_CAST "<script");
	}

	map[size - 1] = lastchar;
}

/**
 * Parse the html in the file and return its htmlDocPtr; exit on failure
 */
static htmlDocPtr parse_file(FILE *file)
{
	htmlDocPtr doc;
	int flags = HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING;
	char *map;
	size_t mapsize;

	mapsize = map_file(file, &map);
	invalidate_script_cdata(map, mapsize);
	doc = htmlReadMemory(map, mapsize, options.base_url, options.enc, flags);
	if (!doc)
		fatal_msg("libxml2 couldn't parse the document as HTML");
	munmap(map, mapsize);
	return doc;
}

/**
 * Abort if the character encoding is unrecognized
 */
static void check_known_encoding(const char *enc)
{
	if (xmlParseCharEncoding(enc) > 0) /* Known to libxml2 */
		return;
	if (strcasecmp(enc, "gb2312") == 0) /* Prepared for iconv */
		return;
	fatal_msg("unrecognized encoding");
}

static const char *OPTSTRING = "cu:vB:E:HM";
#define DISABLE_SANDBOX 256 /* No short version of this option */
static const struct option LONGOPTS[] = {
	{"check", no_argument, NULL, 'c'},
	{"base", required_argument, NULL, 'u'},
	{"browser", required_argument, NULL, 'B'},
	{"encoding", required_argument, NULL, 'E'},
	{"html", no_argument, NULL, 'H'},
	{"meta", no_argument, NULL, 'M'},
	{"disable-sandbox", no_argument, NULL, DISABLE_SANDBOX},
	{0}
};

/**
 * Parse the command line arguments and set the global options structure
 */
static void parse_arguments(int argc, char *argv[])
{
	int output_opts = 0; /* Only one of these options can be set */

	/* TODO: add flags to override this? */
	options.flags |= OPT_STRIP_UNLIKELY;
	options.flags |= OPT_WEIGHT_CLASSES;
	options.flags |= OPT_CLEAN_CONDITIONALLY;

	progname = argv[0];
	while (1) {
		int opt = getopt_long(argc, argv, OPTSTRING, LONGOPTS, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'c':
			++output_opts;
			options.flags |= OPT_CHECK;
			break;
		case 'u':
			options.base_url = optarg;
			break;
		case 'v':
			puts("rdrview version 0.1");
			exit(0);
		case 'B':
			++output_opts;
			options.browser = optarg;
			options.flags |= OPT_BROWSER;
			break;
		case 'H':
			++output_opts;
			options.flags |= OPT_HTML;
			break;
		case 'M':
			++output_opts;
			options.flags |= OPT_METADATA;
			break;
		case 'E':
			check_known_encoding(optarg);
			options.enc = optarg;
			break;
		case DISABLE_SANDBOX:
			options.disable_sandbox = true;
			break;
		default:
			usage();
		}
	}

	if (output_opts > 1)
		usage();
	if (!output_opts) {
		/* Using a browser is the default */
		options.browser = getenv("RDRVIEW_BROWSER");
		options.flags |= OPT_BROWSER;
	}

	if (optind < argc - 1)
		usage();
	if (optind == argc - 1)
		options.url = argv[optind];

	if (!options.base_url) {
		options.base_url = options.url;
		if (!options.base_url)
			options.base_url = "none://standard.input";
	}
}

/**
 * Save the HTML for a node to the given file
 */
static void save_node_to_file(htmlNodePtr node, FILE *file)
{
	int temp_stdout;

	/* Save a duplicate of the stdout file descriptor, to restore it later */
	temp_stdout = dup(STDOUT_FILENO);
	if (temp_stdout < 0)
		fatal_errno();

	/* Turn the file into stdout, so that xmlShellPrintNode() writes to it */
	if (dup2(fileno(file), STDOUT_FILENO) < 0)
		fatal_errno();
	xmlShellPrintNode(node);
	fflush(stdout);

	/* Now restore stdout */
	if (dup2(temp_stdout, STDOUT_FILENO) < 0)
		fatal_errno();
	close(temp_stdout);
}

/**
 * Search a mailcap file for a way to open text/html under copiousoutput
 */
static char *extract_browser_command_template(FILE *mailcap)
{
	static char line[256];
	static char base[5]; /* Enough for 'text' */
	static char type[5]; /* Enough for 'html' */
	static char comm[128];
	static char flag[14]; /* Enough for 'copiousoutput' */

	/* TODO: don't ignore the "test" command */
	while (fgets(line, 256, mailcap)) {
		static const char *format = "%4[^/]/%4[^;]; %127[^;]; %13[^;]";
		int ret;

		ret = sscanf(line, format, base, type, comm, flag);
		if (ret != 4)
			continue;
		if (strcasecmp(base, "text") != 0)
			continue;
		if (strcasecmp(type, "html") != 0 && strcmp(type, "*") != 0)
			continue;
		if (strcasecmp(flag, "copiousoutput") != 0)
			continue;
		return comm;
	}
	return NULL;
}

/**
 * Abort if the given command is a call to rdrview
 *
 * A user might want to configure mailcap to use rdrview for copiousoutput of
 * HTML. This could lead to an infinite recursion if they forget to specify a
 * web browser. The point of this check is to mitigate that risk.
 */
static void check_no_recursion(const char *comm)
{
	int namelen = strlen("rdrview ");

	/*
	 * Technically the name of the tool could also be terminated by something
	 * like '<', but let's ignore that. No matter what I do, a user that really
	 * wants to make a mess of their mailcap will find a way.
	 */
	if (strncmp(comm, "rdrview ", namelen) != 0)
		return;
	fatal_msg("recursion in the mailcap file, please specify a web browser");
}

/**
 * Return the shell command to open the given html file, according to mailcap
 */
static char *get_browser_command_via_mailcap(char *filepath)
{
	static char *mailcap_paths[] = {
		NULL, /* Placeholder for the user's mailcap */
		"/etc/mailcap",
		"/usr/share/etc/mailcap",
		"/usr/local/etc/mailcap",
	};
	char *homepath;
	char *template = NULL;
	unsigned int i = 0;

	/* Search the user's local mailcap before the others */
	homepath = getenv("HOME");
	if (homepath)
		mailcap_paths[0] = mkstring("%s/.mailcap", getenv("HOME"));
	else
		++i;

	for (; i < ARRAY_SIZE(mailcap_paths); ++i) {
		FILE *mailcap;

		mailcap = fopen(mailcap_paths[i], "r");
		if (mailcap)
			template = extract_browser_command_template(mailcap);
		if (template)
			break;
	}
	if (!template)
		fatal_msg("mailcap query failed, please specify a web browser");
	check_no_recursion(template);

	free(mailcap_paths[0]);
	return mkstring(template, filepath);
}

/**
 * Return the shell command to open the given html file
 */
static char *get_browser_command(char *filepath)
{
	if (!options.browser)
		return get_browser_command_via_mailcap(filepath);
	return mkstring("%s %s", options.browser, filepath);
}

/**
 * Run the given shell command to open the temporary html file; return its
 * exit status code.
 */
static int run_browser_command(char *command)
{
	int ret;

	if (!options.browser && isatty(STDOUT_FILENO)) {
		/* No specific request from the user: just pipe the text to a pager */
		char *newcom;

		if (system("command -v pager >/dev/null 2>&1") == 0)
			newcom = mkstring("%s | pager", command);
		else if (system("command -v less >/dev/null 2>&1") == 0)
			newcom = mkstring("%s | less", command);
		else
			newcom = strdup(command); /* No pager found... */

		free(command);
		command = newcom;
	}

	/* We may open a TUI now, so make sure stdin comes from the terminal */
	if (!isatty(STDIN_FILENO)) {
		char *termdev = ctermid(NULL);

		if (!freopen(termdev, "r", stdin))
			fatal_errno();
	}

	ret = system(command);
	if (ret < 0)
		fatal_errno();
	free(command);
	return ret;
}

/**
 * Print the already obtained document metadata to standard output
 */
static void print_metadata(htmlDocPtr doc)
{
	if (metadata.title)
		printf("Title: %s\n", metadata.title);
	if (metadata.byline)
		printf("Byline: %s\n", metadata.byline);
	if (metadata.excerpt)
		printf("Excerpt: %s\n", metadata.excerpt);
	printf("Readerable: %s\n", is_probably_readerable(doc) ? "Yes" : "No");
	if (metadata.site_name)
		printf("Site name: %s\n", metadata.site_name);

	if (metadata.direction) {
		char *dir = NULL;

		/* Spell out the text direction */
		if (strcmp(metadata.direction, "ltr") == 0)
			dir = "Left to right";
		else if (strcmp(metadata.direction, "rtl") == 0)
			dir = "Right to left";

		if (dir)
			printf("Text direction: %s\n", dir);
	}
}

/**
 * Remove a temporary file and its parent directory
 */
static void clean_temp_filepath(void)
{
	if (!tmpdir) /* This is the child, let the parent handle cleanup */
		return;

	unlink(inputfile);
	free(inputfile);
	inputfile = NULL;

	unlink(outputfile);
	free(outputfile);
	outputfile = NULL;

	rmdir(tmpdir);
	free(tmpdir);
	tmpdir = NULL;
}

/**
 * Run any cleanups needed on normal process termination
 */
static void normal_term_cleanup(void)
{
	clean_temp_filepath();
}

/**
 * Run any cleanups needed when the process is terminated by a signal
 */
static void signal_term_cleanup(int signum)
{
	clean_temp_filepath();
	signal(signum, SIG_DFL);
	raise(signum);
}

/**
 * Make sure that the temp file always gets removed on process termination
 */
static void set_cleanup_handlers(void)
{
	struct sigaction act = { .sa_handler = signal_term_cleanup, };

	/* Deal with normal termination */
	if (atexit(normal_term_cleanup))
		fatal();

	/*
	 * Now deal with signals that may cause termination. Only focus on those
	 * that I believe are expected, I can always add the others later on.
	 * Ignore errors: sigaction() doesn't seem to fail, and it's not that bad
	 * if it happens here.
	 */
	sigaction(SIGHUP, &act, NULL);
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGPIPE, &act, NULL);
	sigaction(SIGQUIT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
}

/**
 * Try a single banned syscall to check if the sandbox works
 */
#ifdef NDEBUG
static inline void assert_sandbox_works(void) {}
#else /* NDEBUG */
static inline void assert_sandbox_works(void)
{
	errno = 0;
	if (!options.disable_sandbox) {
		int ret = nice(0);
		assert(ret == -1);
		assert(errno == EPERM);
	}
}
#endif /* NDEBUG */

/* Descriptors for the encodings supported via iconv; for now only GB2312 */
static iconv_t gb2312_cd;

/**
 * Set up the iconv conversion descriptors to be used by libxml2. This is
 * needed because libxml2 can't do it from inside the sandbox.
 */
static void init_iconv(void)
{
	gb2312_cd = iconv_open("UTF-8", "GB2312");
	if (gb2312_cd == (iconv_t)(-1))
		fatal_errno();
}

/**
 * Clean up after init_iconv()
 */
static void clean_iconv(void)
{
	iconv_close(gb2312_cd);
}

/**
 * Set up a sandbox and run all dangerous processing of the input HTML file
 */
static int run_dangerous(FILE *input_fp, FILE *output_fp)
{
	htmlDocPtr doc;
	htmlNodePtr article = NULL;
	int ret = 0;

	init_iconv();

	if (options.url)
		url_to_file(input_fp);
	else
		stdin_to_file(input_fp);

	/* TODO: consider ways to sandbox the libcurl stuff as well */
	start_sandbox();
	assert_sandbox_works();

	doc = parse_file(input_fp);
	init_regexes();

	if (options.flags & OPT_CHECK) {
		ret = !is_probably_readerable(doc);
		goto out;
	}

	article = parse(doc);
	if (!article)
		fatal_msg("no content could be extracted");

	if (options.flags & OPT_HTML)
		xmlShellPrintNode(article);
	else if (options.flags & OPT_METADATA)
		print_metadata(doc);
	else
		save_node_to_file(article, output_fp);

out:
	/* Valgrind complains if I don't run these cleanups */
	free_node(article);
	free_doc(doc);
	xmlCleanupParser();
	clean_iconv();
	return ret;
}

int main(int argc, char *argv[])
{
	FILE *input_fp, *output_fp;
	char *command = NULL;
	pid_t cpid;
	int ret = 1;
	int wstatus;

	LIBXML_TEST_VERSION
	/* I made a mess mixing xmlMalloc() and malloc(), so play it safe here */
	if (xmlMemSetup(free, malloc, realloc, strdup))
		fatal();

	set_cleanup_handlers();
	parse_arguments(argc, argv);

	inputfile = get_temp_filepath("input.html");
	outputfile = get_temp_filepath("output.html");
	input_fp = fopen(inputfile, "w+");
	output_fp = fopen(outputfile, "w");
	if (!input_fp || !output_fp)
		fatal_msg("failed to create the temporary files");

	/* Do this before the fork to avoid wasting time if there is no browser */
	if (options.flags & OPT_BROWSER)
		command = get_browser_command(outputfile);

	cpid = fork();
	if (cpid < 0) {
		fatal_errno();
	} else if (!cpid) { /* Sandboxed subprocess */
		free(tmpdir);
		tmpdir = NULL; /* Otherwise the child would remove it on exit */
		free(command); /* For the sake of valgrind */
		ret = run_dangerous(input_fp, output_fp);
		exit(ret);
	} else if (wait(&wstatus) < 0) {
		fatal_errno();
	}

	if (WIFEXITED(wstatus))
		ret = WEXITSTATUS(wstatus);
	else if (WIFSIGNALED(wstatus))
		ret = 128 + WTERMSIG(wstatus);

	if (!ret && (options.flags & OPT_BROWSER))
		ret = run_browser_command(command);
	return ret;
}
