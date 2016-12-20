/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "config.h"
#include "util.h"
#include "rspamd.h"
#include "message.h"
#include "cfg_file.h"
#include "libutil/regexp.h"
#include "html.h"
#include "images.h"
#include "archives.h"
#include "email_addr.h"
#include "utlist.h"
#include "tokenizers/tokenizers.h"
#include "cryptobox.h"
#include "smtp_parsers.h"
#include "mime_parser.h"
#include "mime_encoding.h"

#ifdef WITH_SNOWBALL
#include "libstemmer.h"
#endif

#include <iconv.h>

#define GTUBE_SYMBOL "GTUBE"

#define SET_PART_RAW(part) ((part)->flags &= ~RSPAMD_MIME_TEXT_PART_FLAG_UTF)
#define SET_PART_UTF(part) ((part)->flags |= RSPAMD_MIME_TEXT_PART_FLAG_UTF)

static const gchar gtube_pattern[] = "XJS*C4JDBQADN1.NSBN3*2IDNEN*"
		"GTUBE-STANDARD-ANTI-UBE-TEST-EMAIL*C.34X";
static const guint64 words_hash_seed = 0xdeadbabe;


static void
free_byte_array_callback (void *pointer)
{
	GByteArray *arr = (GByteArray *) pointer;
	g_byte_array_free (arr, TRUE);
}

struct language_match {
	const char *code;
	const char *name;
	GUnicodeScript script;
};

static int
language_elts_cmp (const void *a, const void *b)
{
	GUnicodeScript sc = *(const GUnicodeScript *)a;
	const struct language_match *bb = (const struct language_match *)b;

	return (sc - bb->script);
}

static void
detect_text_language (struct rspamd_mime_text_part *part)
{
	/* Keep sorted */
	static const struct language_match language_codes[] = {
			{ "", "english", G_UNICODE_SCRIPT_COMMON },
			{ "", "", G_UNICODE_SCRIPT_INHERITED },
			{ "ar", "arabic", G_UNICODE_SCRIPT_ARABIC },
			{ "hy", "armenian", G_UNICODE_SCRIPT_ARMENIAN },
			{ "bn", "chineese", G_UNICODE_SCRIPT_BENGALI },
			{ "", "", G_UNICODE_SCRIPT_BOPOMOFO },
			{ "chr", "", G_UNICODE_SCRIPT_CHEROKEE },
			{ "cop", "",  G_UNICODE_SCRIPT_COPTIC  },
			{ "ru", "russian",  G_UNICODE_SCRIPT_CYRILLIC },
			/* Deseret was used to write English */
			{ "", "",  G_UNICODE_SCRIPT_DESERET },
			{ "hi", "",  G_UNICODE_SCRIPT_DEVANAGARI },
			{ "am", "",  G_UNICODE_SCRIPT_ETHIOPIC },
			{ "ka", "",  G_UNICODE_SCRIPT_GEORGIAN },
			{ "", "",  G_UNICODE_SCRIPT_GOTHIC },
			{ "el", "greek",  G_UNICODE_SCRIPT_GREEK },
			{ "gu", "",  G_UNICODE_SCRIPT_GUJARATI },
			{ "pa", "",  G_UNICODE_SCRIPT_GURMUKHI },
			{ "han", "chineese",  G_UNICODE_SCRIPT_HAN },
			{ "ko", "",  G_UNICODE_SCRIPT_HANGUL },
			{ "he", "hebrew",  G_UNICODE_SCRIPT_HEBREW },
			{ "ja", "",  G_UNICODE_SCRIPT_HIRAGANA },
			{ "kn", "",  G_UNICODE_SCRIPT_KANNADA },
			{ "ja", "",  G_UNICODE_SCRIPT_KATAKANA },
			{ "km", "",  G_UNICODE_SCRIPT_KHMER },
			{ "lo", "",  G_UNICODE_SCRIPT_LAO },
			{ "en", "english",  G_UNICODE_SCRIPT_LATIN },
			{ "ml", "",  G_UNICODE_SCRIPT_MALAYALAM },
			{ "mn", "",  G_UNICODE_SCRIPT_MONGOLIAN },
			{ "my", "",  G_UNICODE_SCRIPT_MYANMAR },
			/* Ogham was used to write old Irish */
			{ "", "",  G_UNICODE_SCRIPT_OGHAM },
			{ "", "",  G_UNICODE_SCRIPT_OLD_ITALIC },
			{ "or", "",  G_UNICODE_SCRIPT_ORIYA },
			{ "", "",  G_UNICODE_SCRIPT_RUNIC },
			{ "si", "",  G_UNICODE_SCRIPT_SINHALA },
			{ "syr", "",  G_UNICODE_SCRIPT_SYRIAC },
			{ "ta", "",  G_UNICODE_SCRIPT_TAMIL },
			{ "te", "",  G_UNICODE_SCRIPT_TELUGU },
			{ "dv", "",  G_UNICODE_SCRIPT_THAANA },
			{ "th", "",  G_UNICODE_SCRIPT_THAI },
			{ "bo", "",  G_UNICODE_SCRIPT_TIBETAN },
			{ "iu", "",  G_UNICODE_SCRIPT_CANADIAN_ABORIGINAL },
			{ "", "",  G_UNICODE_SCRIPT_YI },
			{ "tl", "",  G_UNICODE_SCRIPT_TAGALOG },
			/* Phillipino languages/scripts */
			{ "hnn", "",  G_UNICODE_SCRIPT_HANUNOO },
			{ "bku", "",  G_UNICODE_SCRIPT_BUHID },
			{ "tbw", "",  G_UNICODE_SCRIPT_TAGBANWA },

			{ "", "",  G_UNICODE_SCRIPT_BRAILLE },
			{ "", "",  G_UNICODE_SCRIPT_CYPRIOT },
			{ "", "",  G_UNICODE_SCRIPT_LIMBU },
			/* Used for Somali (so) in the past */
			{ "", "",  G_UNICODE_SCRIPT_OSMANYA },
			/* The Shavian alphabet was designed for English */
			{ "", "",  G_UNICODE_SCRIPT_SHAVIAN },
			{ "", "",  G_UNICODE_SCRIPT_LINEAR_B },
			{ "", "",  G_UNICODE_SCRIPT_TAI_LE },
			{ "uga", "",  G_UNICODE_SCRIPT_UGARITIC },
			{ "", "",  G_UNICODE_SCRIPT_NEW_TAI_LUE },
			{ "bug", "",  G_UNICODE_SCRIPT_BUGINESE },
			{ "", "",  G_UNICODE_SCRIPT_GLAGOLITIC },
			/* Used for for Berber (ber), but Arabic script is more common */
			{ "", "",  G_UNICODE_SCRIPT_TIFINAGH },
			{ "syl", "",  G_UNICODE_SCRIPT_SYLOTI_NAGRI },
			{ "peo", "",  G_UNICODE_SCRIPT_OLD_PERSIAN },
			{ "", "",  G_UNICODE_SCRIPT_KHAROSHTHI },
			{ "", "",  G_UNICODE_SCRIPT_UNKNOWN },
			{ "", "",  G_UNICODE_SCRIPT_BALINESE },
			{ "", "",  G_UNICODE_SCRIPT_CUNEIFORM },
			{ "", "",  G_UNICODE_SCRIPT_PHOENICIAN },
			{ "", "",  G_UNICODE_SCRIPT_PHAGS_PA },
			{ "nqo", "", G_UNICODE_SCRIPT_NKO }
	};
	const struct language_match *lm;
	const int max_chars = 32;

	if (part != NULL) {
		if (IS_PART_UTF (part)) {
			/* Try to detect encoding by several symbols */
			const gchar *p, *pp;
			gunichar c;
			gint32 remain = part->content->len, max = 0, processed = 0;
			gint32 scripts[G_N_ELEMENTS (language_codes)];
			GUnicodeScript scc, sel = G_UNICODE_SCRIPT_COMMON;

			p = part->content->data;
			memset (scripts, 0, sizeof (scripts));

			while (remain > 0 && processed < max_chars) {
				c = g_utf8_get_char_validated (p, remain);
				if (c == (gunichar) -2 || c == (gunichar) -1) {
					break;
				}
				if (g_unichar_isalpha (c)) {
					scc = g_unichar_get_script (c);
					if (scc < (gint)G_N_ELEMENTS (scripts)) {
						scripts[scc]++;
					}
					processed ++;
				}
				pp = g_utf8_next_char (p);
				remain -= pp - p;
				p = pp;
			}
			for (remain = 0; remain < (gint)G_N_ELEMENTS (scripts); remain++) {
				if (scripts[remain] > max) {
					max = scripts[remain];
					sel = remain;
				}
			}
			part->script = sel;
			lm = bsearch (&sel, language_codes, G_N_ELEMENTS (language_codes),
					sizeof (language_codes[0]), &language_elts_cmp);

			if (lm != NULL) {
				part->lang_code = lm->code;
				part->language = lm->name;
			}
		}
	}
}

static void
rspamd_extract_words (struct rspamd_task *task,
		struct rspamd_mime_text_part *part)
{
#ifdef WITH_SNOWBALL
	struct sb_stemmer *stem = NULL;
#endif
	rspamd_ftok_t *w;
	gchar *temp_word;
	const guchar *r;
	guint i, nlen;

#ifdef WITH_SNOWBALL
	if (part->language && part->language[0] != '\0' && IS_PART_UTF (part)) {
		stem = sb_stemmer_new (part->language, "UTF_8");
		if (stem == NULL) {
			msg_info_task ("<%s> cannot create lemmatizer for %s language",
					task->message_id, part->language);
		}
	}
#endif
	/* Ugly workaround */
	part->normalized_words = rspamd_tokenize_text (part->content->data,
			part->content->len, IS_PART_UTF (part), task->cfg,
			part->exceptions, FALSE,
			NULL);

	if (part->normalized_words) {
		part->normalized_hashes = g_array_sized_new (FALSE, FALSE,
				sizeof (guint64), part->normalized_words->len);

		for (i = 0; i < part->normalized_words->len; i ++) {
			guint64 h;

			w = &g_array_index (part->normalized_words, rspamd_ftok_t, i);
			r = NULL;
#ifdef WITH_SNOWBALL
			if (stem) {
				r = sb_stemmer_stem (stem, w->begin, w->len);
			}
#endif

			if (w->len > 0 && !(w->len == 6 && memcmp (w->begin, "!!EX!!", 6) == 0)) {
				if (r != NULL) {
					nlen = strlen (r);
					nlen = MIN (nlen, w->len);
					temp_word = rspamd_mempool_alloc (task->task_pool, nlen);
					memcpy (temp_word, r, nlen);
					w->begin = temp_word;
					w->len = nlen;
				}
				else {
					temp_word = rspamd_mempool_alloc (task->task_pool, w->len);
					memcpy (temp_word, w->begin, w->len);

					if (IS_PART_UTF (part)) {
						rspamd_str_lc_utf8 (temp_word, w->len);
					}
					else {
						rspamd_str_lc (temp_word, w->len);
					}

					w->begin = temp_word;
				}
			}

			if (w->len > 0) {
				/*
				 * We use static hash seed if we would want to use that in shingles
				 * computation in future
				 */
				h = rspamd_cryptobox_fast_hash_specific (RSPAMD_CRYPTOBOX_HASHFAST_INDEPENDENT,
						w->begin, w->len, words_hash_seed);
				g_array_append_val (part->normalized_hashes, h);
			}
		}
	}
#ifdef WITH_SNOWBALL
	if (stem != NULL) {
		sb_stemmer_delete (stem);
	}
#endif
}

static void
rspamd_normalize_text_part (struct rspamd_task *task,
		struct rspamd_mime_text_part *part)
{

	const guchar *p, *c, *end;
	guint i;
	goffset off;
	struct rspamd_process_exception *ex;

	/* Strip newlines */
	part->stripped_content = g_byte_array_sized_new (part->content->len);
	part->newlines = g_ptr_array_sized_new (128);
	p = part->content->data;
	c = p;
	end = p + part->content->len;

	rspamd_strip_newlines_parse (p, end, part->stripped_content,
			IS_PART_HTML (part), &part->nlines, part->newlines);

	for (i = 0; i < part->newlines->len; i ++) {
		ex = rspamd_mempool_alloc (task->task_pool, sizeof (*ex));
		off = (goffset)g_ptr_array_index (part->newlines, i);
		g_ptr_array_index (part->newlines, i) = (gpointer)(goffset)
				(part->stripped_content->data + off);
		ex->pos = off;
		ex->len = 0;
		ex->type = RSPAMD_EXCEPTION_NEWLINE;
		part->exceptions = g_list_prepend (part->exceptions, ex);
	}

	rspamd_mempool_add_destructor (task->task_pool,
			(rspamd_mempool_destruct_t) free_byte_array_callback,
			part->stripped_content);
	rspamd_mempool_add_destructor (task->task_pool,
			(rspamd_mempool_destruct_t) rspamd_ptr_array_free_hard,
			part->newlines);
}

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

static guint
rspamd_words_levenshtein_distance (struct rspamd_task *task,
		GArray *w1, GArray *w2)
{
	guint s1len, s2len, x, y, lastdiag, olddiag;
	guint *column, ret;
	guint64 h1, h2;
	gint eq;
	static const guint max_words = 8192;

	s1len = w1->len;
	s2len = w2->len;

	if (s1len + s2len > max_words) {
		msg_err_task ("cannot compare parts with more than %ud words: %ud",
				max_words, s1len);
		return 0;
	}

	column = g_malloc0 ((s1len + 1) * sizeof (guint));

	for (y = 1; y <= s1len; y++) {
		column[y] = y;
	}

	for (x = 1; x <= s2len; x++) {
		column[0] = x;

		for (y = 1, lastdiag = x - 1; y <= s1len; y++) {
			olddiag = column[y];
			h1 = g_array_index (w1, guint64, y - 1);
			h2 = g_array_index (w2, guint64, x - 1);
			eq = (h1 == h2) ? 1 : 0;
			/*
			 * Cost of replacement is twice higher than cost of add/delete
			 * to calculate percentage properly
			 */
			column[y] = MIN3 (column[y] + 1, column[y - 1] + 1,
					lastdiag + (eq * 2));
			lastdiag = olddiag;
		}
	}

	ret = column[s1len];
	g_free (column);

	return ret;
}

static gboolean
rspamd_check_gtube (struct rspamd_task *task, struct rspamd_mime_text_part *part)
{
	static const gsize max_check_size = 4 * 1024;
	g_assert (part != NULL);

	if (part->content && part->content->len > sizeof (gtube_pattern) &&
			part->content->len <= max_check_size) {
		if (rspamd_substring_search_twoway (part->content->data,
				part->content->len,
				gtube_pattern, sizeof (gtube_pattern) - 1) != -1) {
			task->flags |= RSPAMD_TASK_FLAG_SKIP;
			task->flags |= RSPAMD_TASK_FLAG_GTUBE;
			msg_info_task ("<%s>: gtube pattern has been found in part of length %ud",
					task->message_id, part->content->len);

			return TRUE;
		}
	}

	return FALSE;
}

static gint
exceptions_compare_func (gconstpointer a, gconstpointer b)
{
	const struct rspamd_process_exception *ea = a, *eb = b;

	return ea->pos - eb->pos;
}

static void
rspamd_message_process_text_part (struct rspamd_task *task,
	struct rspamd_mime_part *mime_part)
{
	struct rspamd_mime_text_part *text_part;
	rspamd_ftok_t html_tok, xhtml_tok;
	GByteArray *part_content;

	/* Skip attachments */
	if (mime_part->cd && mime_part->cd->type == RSPAMD_CT_ATTACHMENT &&
		(task->cfg && !task->cfg->check_text_attachements)) {
		debug_task ("skip attachments for checking as text parts");
		return;
	}

	html_tok.begin = "html";
	html_tok.len = 4;
	xhtml_tok.begin = "xhtml";
	xhtml_tok.len = 5;

	if (rspamd_ftok_cmp (&mime_part->ct->subtype, &html_tok) == 0 ||
			rspamd_ftok_cmp (&mime_part->ct->subtype, &xhtml_tok) == 0) {

		text_part = rspamd_mempool_alloc0 (task->task_pool,
				sizeof (struct rspamd_mime_text_part));
		text_part->raw.begin = mime_part->raw_data.begin;
		text_part->raw.len = mime_part->raw_data.len;
		text_part->parsed.begin = mime_part->parsed_data.begin;
		text_part->parsed.len = mime_part->parsed_data.len;
		text_part->flags |= RSPAMD_MIME_TEXT_PART_FLAG_HTML;
		text_part->mime_part = mime_part;

		if (mime_part->parsed_data.len == 0) {
			text_part->flags |= RSPAMD_MIME_TEXT_PART_FLAG_EMPTY;
			g_ptr_array_add (task->text_parts, text_part);
			return;
		}

		part_content = rspamd_mime_text_part_maybe_convert (task, text_part);

		text_part->html = rspamd_mempool_alloc0 (task->task_pool,
				sizeof (*text_part->html));
		text_part->mime_part = mime_part;

		text_part->flags |= RSPAMD_MIME_TEXT_PART_FLAG_BALANCED;
		text_part->content = rspamd_html_process_part_full (
				task->task_pool,
				text_part->html,
				part_content,
				&text_part->exceptions,
				task->urls,
				task->emails);

		if (text_part->content->len == 0) {
			text_part->flags |= RSPAMD_MIME_TEXT_PART_FLAG_EMPTY;
		}

		rspamd_mempool_add_destructor (task->task_pool,
			(rspamd_mempool_destruct_t) free_byte_array_callback,
			text_part->content);
		g_ptr_array_add (task->text_parts, text_part);
	}
	else {

		text_part =
			rspamd_mempool_alloc0 (task->task_pool,
				sizeof (struct rspamd_mime_text_part));
		text_part->mime_part = mime_part;
		text_part->raw.begin = mime_part->raw_data.begin;
		text_part->raw.len = mime_part->raw_data.len;
		text_part->parsed.begin = mime_part->parsed_data.begin;
		text_part->parsed.len = mime_part->parsed_data.len;
		text_part->mime_part = mime_part;

		if (mime_part->parsed_data.len == 0) {
			text_part->flags |= RSPAMD_MIME_TEXT_PART_FLAG_EMPTY;
			g_ptr_array_add (task->text_parts, text_part);
			return;
		}

		text_part->content = rspamd_mime_text_part_maybe_convert (task,
				text_part);
		g_ptr_array_add (task->text_parts, text_part);
	}


	mime_part->flags |= RSPAMD_MIME_PART_TEXT;
	mime_part->specific.txt = text_part;

	if (rspamd_check_gtube (task, text_part)) {
		struct rspamd_metric_result *mres;

		mres = rspamd_create_metric_result (task, DEFAULT_METRIC);

		if (mres != NULL) {
			mres->score = rspamd_task_get_required_score (task, mres);
			mres->action = METRIC_ACTION_REJECT;
		}

		task->pre_result.action = METRIC_ACTION_REJECT;
		task->pre_result.str = "Gtube pattern";
		ucl_object_insert_key (task->messages,
				ucl_object_fromstring ("Gtube pattern"), "smtp_message", 0,
				false);
		rspamd_task_insert_result (task, GTUBE_SYMBOL, 0, NULL);

		return;
	}

	/* Post process part */
	detect_text_language (text_part);
	rspamd_normalize_text_part (task, text_part);

	if (!IS_PART_HTML (text_part)) {
		rspamd_url_text_extract (task->task_pool, task, text_part, FALSE);
	}

	if (text_part->exceptions) {
		text_part->exceptions = g_list_sort (text_part->exceptions,
				exceptions_compare_func);
		rspamd_mempool_add_destructor (task->task_pool,
				(rspamd_mempool_destruct_t)g_list_free,
				text_part->exceptions);
	}

	rspamd_extract_words (task, text_part);
}

/* Creates message from various data using libmagic to detect type */
static void
rspamd_message_from_data (struct rspamd_task *task, const guchar *start,
		gsize len)
{
	struct rspamd_content_type *ct = NULL;
	struct rspamd_mime_part *part;
	const char *mb = NULL;
	gchar *mid;
	rspamd_ftok_t srch, *tok;

	g_assert (start != NULL);

	tok = rspamd_task_get_request_header (task, "Content-Type");

	if (tok) {
		/* We have Content-Type defined */
		ct = rspamd_content_type_parse (tok->begin, tok->len,
				task->task_pool);
	}
	else if (task->cfg && task->cfg->libs_ctx) {
		/* Try to predict it by content (slow) */
		mb = magic_buffer (task->cfg->libs_ctx->libmagic,
				start,
				len);

		if (mb) {
			srch.begin = mb;
			srch.len = strlen (mb);
			ct = rspamd_content_type_parse (srch.begin, srch.len,
					task->task_pool);
		}
	}

	msg_warn_task ("construct fake mime of type: %s", mb);
	part = rspamd_mempool_alloc0 (task->task_pool, sizeof (*part));
	part->ct = ct;
	part->raw_data.begin = start;
	part->raw_data.len = len;
	part->parsed_data.begin = start;
	part->parsed_data.len = len;

	/* Generate message ID */
	mid = rspamd_mime_message_id_generate ("localhost.localdomain");
	rspamd_mempool_add_destructor (task->task_pool,
			(rspamd_mempool_destruct_t) g_free, mid);
	task->message_id = mid;
	task->queue_id = mid;
}

gboolean
rspamd_message_parse (struct rspamd_task *task)
{
	GPtrArray *hdrs;
	struct rspamd_mime_header *rh;
	struct rspamd_mime_text_part *p1, *p2;
	struct received_header *recv, *trecv;
	const gchar *p;
	gsize len;
	gint i;
	gdouble diff, *pdiff;
	guint tw, *ptw, dw;
	GError *err = NULL;
	rspamd_cryptobox_hash_state_t st;
	guchar digest_out[rspamd_cryptobox_HASHBYTES];

	if (RSPAMD_TASK_IS_EMPTY (task)) {
		/* Don't do anything with empty task */

		return TRUE;
	}

	p = task->msg.begin;
	len = task->msg.len;

	/* Skip any space characters to avoid some bad messages to be unparsed */
	while (len > 0 && g_ascii_isspace (*p)) {
		p ++;
		len --;
	}

	/*
	 * Exim somehow uses mailbox format for messages being scanned:
	 * From xxx@xxx.com Fri May 13 19:08:48 2016
	 *
	 * So we check if a task has non-http format then we check for such a line
	 * at the beginning to avoid errors
	 */
	if (!(task->flags & RSPAMD_TASK_FLAG_JSON) || (task->flags &
			RSPAMD_TASK_FLAG_LOCAL_CLIENT)) {
		if (len > sizeof ("From ") - 1) {
			if (memcmp (p, "From ", sizeof ("From ") - 1) == 0) {
				/* Skip to CRLF */
				msg_info_task ("mailbox input detected, enable workaround");
				p += sizeof ("From ") - 1;
				len -= sizeof ("From ") - 1;

				while (len > 0 && *p != '\n') {
					p ++;
					len --;
				}
				while (len > 0 && g_ascii_isspace (*p)) {
					p ++;
					len --;
				}
			}
		}
	}

	task->msg.begin = p;
	task->msg.len = len;
	rspamd_cryptobox_hash_init (&st, NULL, 0);

	if (task->flags & RSPAMD_TASK_FLAG_MIME) {

		debug_task ("construct mime parser from string length %d",
				(gint) task->msg.len);
		if (!rspamd_mime_parse_task (task, &err)) {
			msg_err_task ("cannot construct mime from stream: %e", err);

			if (task->cfg && (!task->cfg->allow_raw_input)) {
				msg_err_task ("cannot construct mime from stream");
				if (err) {
					task->err = err;
				}

				return FALSE;
			}
			else {
				task->flags &= ~RSPAMD_TASK_FLAG_MIME;
				rspamd_message_from_data (task, p, len);
			}
		}
	}
	else {
		task->flags &= ~RSPAMD_TASK_FLAG_MIME;
		rspamd_message_from_data (task, p, len);
	}


	/* Save message id for future use */
	hdrs = rspamd_message_get_header_array (task, "Message-ID", FALSE);

	if (hdrs) {
		gchar *p, *end;

		rh = g_ptr_array_index (hdrs, 0);

		p = rh->decoded;
		end = p + strlen (p);

		if (*p == '<') {
			p ++;

			if (end > p && *(end - 1) == '>') {
				*(end - 1) = '\0';
				p = rspamd_mempool_strdup (task->task_pool, p);
				*(end - 1) = '>';
			}
		}

		task->message_id = p;
	}

	if (task->message_id == NULL) {
		task->message_id = "undef";
	}

	if (!task->subject) {
		hdrs = rspamd_message_get_header_array (task, "Subject", FALSE);

		if (hdrs) {
			rh = g_ptr_array_index (hdrs, 0);
			task->subject = rh->decoded;
		}
	}

	debug_task ("found %ud parts in message", task->parts->len);
	if (task->queue_id == NULL) {
		task->queue_id = "undef";
	}

	for (i = 0; i < task->parts->len; i ++) {
		struct rspamd_mime_part *part;

		part = g_ptr_array_index (task->parts, i);

		if (IS_CT_TEXT (part->ct)) {
			rspamd_message_process_text_part (task, part);
		}
	}

	rspamd_images_process (task);
	rspamd_archives_process (task);

	/* Parse received headers */
	hdrs = rspamd_message_get_header_array (task, "Received", FALSE);

	PTR_ARRAY_FOREACH (hdrs, i, rh) {
		recv = rspamd_mempool_alloc0 (task->task_pool,
				sizeof (struct received_header));
		rspamd_smtp_recieved_parse (task, rh->decoded, strlen (rh->decoded), recv);
		/*
		 * For the first header we must ensure that
		 * received is consistent with the IP that we obtain through
		 * client.
		 */
		if (i == 0) {
			gboolean need_recv_correction = FALSE;
			rspamd_inet_addr_t *raddr = recv->addr;

			if (recv->real_ip == NULL || (task->cfg && task->cfg->ignore_received)) {
				need_recv_correction = TRUE;
			}
			else if (!(task->flags & RSPAMD_TASK_FLAG_NO_IP) && task->from_addr) {
				if (!raddr) {
					need_recv_correction = TRUE;
				}
				else {
					if (rspamd_inet_address_compare (raddr, task->from_addr) != 0) {
						need_recv_correction = TRUE;
					}
				}

			}

			if (need_recv_correction && !(task->flags & RSPAMD_TASK_FLAG_NO_IP)
					&& task->from_addr) {
				msg_debug_task ("the first received seems to be"
						" not ours, replace it with fake one");

				trecv = rspamd_mempool_alloc0 (task->task_pool,
								sizeof (struct received_header));
				trecv->real_ip = rspamd_mempool_strdup (task->task_pool,
						rspamd_inet_address_to_string (task->from_addr));
				trecv->from_ip = trecv->real_ip;
				trecv->addr = rspamd_inet_address_copy (task->from_addr);
				rspamd_mempool_add_destructor (task->task_pool,
						(rspamd_mempool_destruct_t)rspamd_inet_address_destroy,
						trecv->addr);

				if (task->hostname) {
					trecv->real_hostname = task->hostname;
					trecv->from_hostname = trecv->real_hostname;
				}

				g_ptr_array_add (task->received, trecv);
			}
		}

		g_ptr_array_add (task->received, recv);
	}

	/* Extract data from received header if we were not given IP */
	if (task->received->len > 0 && (task->flags & RSPAMD_TASK_FLAG_NO_IP) &&
			(task->cfg && !task->cfg->ignore_received)) {
		recv = g_ptr_array_index (task->received, 0);
		if (recv->real_ip) {
			if (!rspamd_parse_inet_address (&task->from_addr,
					recv->real_ip,
					0)) {
				msg_warn_task ("cannot get IP from received header: '%s'",
						recv->real_ip);
				task->from_addr = NULL;
			}
		}
		if (recv->real_hostname) {
			task->hostname = recv->real_hostname;
		}
	}

	if (task->from_envelope == NULL) {
		hdrs = rspamd_message_get_header_array (task, "Return-Path", FALSE);

		if (hdrs && hdrs->len > 0) {
			rh = g_ptr_array_index (hdrs, 0);
			task->from_envelope = rspamd_email_address_from_smtp (rh->decoded,
					strlen (rh->decoded));
		}
	}

	if (task->deliver_to == NULL) {
		hdrs = rspamd_message_get_header_array (task, "Delivered-To", FALSE);

		if (hdrs && hdrs->len > 0) {
			rh = g_ptr_array_index (hdrs, 0);
			task->deliver_to = rspamd_mempool_strdup (task->task_pool, rh->decoded);
		}
	}

	/* Set mime recipients and sender for the task */
	/* TODO: kill it with fire */
	task->rcpt_mime = internet_address_list_new ();

	static const gchar *to_hdrs[] = {"To", "Cc", "Bcc"};

	for (int k = 0; k < G_N_ELEMENTS (to_hdrs); k ++) {
		hdrs = rspamd_message_get_header_array (task, to_hdrs[k], FALSE);
		if (hdrs && hdrs->len > 0) {

			InternetAddressList *tia;

			for (i = 0; i < hdrs->len; i ++) {
				rh = g_ptr_array_index (hdrs, i);

				tia = internet_address_list_parse_string (rh->decoded);

				if (tia) {
					internet_address_list_append (task->rcpt_mime, tia);
					g_object_unref (tia);
				}
			}
		}
	}

	rspamd_mempool_add_destructor (task->task_pool,
			(rspamd_mempool_destruct_t) g_object_unref,
			task->rcpt_mime);

	hdrs = rspamd_message_get_header_array (task, "From", FALSE);

	if (hdrs && hdrs->len > 0) {
		rh = g_ptr_array_index (hdrs, 0);
		task->from_mime = internet_address_list_parse_string (rh->value);
		if (task->from_mime) {
#ifdef GMIME24
			rspamd_mempool_add_destructor (task->task_pool,
					(rspamd_mempool_destruct_t) g_object_unref,
					task->from_mime);
#else
			rspamd_mempool_add_destructor (task->task_pool,
					(rspamd_mempool_destruct_t) internet_address_list_destroy,
					task->from_mime);
#endif
		}
	}

	/* Parse urls inside Subject header */
	hdrs = rspamd_message_get_header_array (task, "Subject", FALSE);

	PTR_ARRAY_FOREACH (hdrs, i, rh) {
		p = rh->decoded;
		len = strlen (p);
		rspamd_url_find_multiple (task->task_pool, p, len, FALSE, NULL,
				rspamd_url_task_callback, task);
	}

	/* Calculate distance for 2-parts messages */
	if (task->text_parts->len == 2) {
		p1 = g_ptr_array_index (task->text_parts, 0);
		p2 = g_ptr_array_index (task->text_parts, 1);

		/* First of all check parent object */
		if (p1->mime_part->parent_part &&
				p1->mime_part->parent_part == p2->mime_part->parent_part) {
			rspamd_ftok_t srch;

			srch.begin = "alternative";
			srch.len = 11;

			if (rspamd_ftok_cmp (&p1->mime_part->parent_part->ct->subtype, &srch) == 0) {
				if (!IS_PART_EMPTY (p1) && !IS_PART_EMPTY (p2) &&
						p1->normalized_hashes && p2->normalized_hashes) {

					tw = p1->normalized_hashes->len + p2->normalized_hashes->len;

					if (tw > 0) {
						dw = rspamd_words_levenshtein_distance (task,
								p1->normalized_hashes,
								p2->normalized_hashes);
						diff = dw / (gdouble)tw;

						msg_debug_task (
								"different words: %d, total words: %d, "
								"got diff between parts of %.2f",
								dw, tw,
								diff);

						pdiff = rspamd_mempool_alloc (task->task_pool,
								sizeof (gdouble));
						*pdiff = diff;
						rspamd_mempool_set_variable (task->task_pool,
								"parts_distance",
								pdiff,
								NULL);
						ptw = rspamd_mempool_alloc (task->task_pool,
								sizeof (gint));
						*ptw = tw;
						rspamd_mempool_set_variable (task->task_pool,
								"total_words",
								ptw,
								NULL);
					}
				}
			}
		}
		else {
			debug_task (
					"message contains two parts but they are in different multi-parts");
		}
	}

	for (i = 0; i < task->parts->len; i ++) {
		struct rspamd_mime_part *part;

		part = g_ptr_array_index (task->parts, i);
		rspamd_cryptobox_hash_update (&st, part->digest, sizeof (part->digest));
	}

	rspamd_cryptobox_hash_final (&st, digest_out);
	memcpy (task->digest, digest_out, sizeof (task->digest));

	if (task->queue_id) {
		msg_info_task ("loaded message; id: <%s>; queue-id: <%s>; size: %z; "
				"checksum: <%*xs>",
				task->message_id, task->queue_id, task->msg.len,
				(gint)sizeof (task->digest), task->digest);
	}
	else {
		msg_info_task ("loaded message; id: <%s>; size: %z; "
				"checksum: <%*xs>",
				task->message_id, task->msg.len,
				(gint)sizeof (task->digest), task->digest);
	}

	return TRUE;
}


GPtrArray *
rspamd_message_get_header_from_hash (GHashTable *htb,
		rspamd_mempool_t *pool,
		const gchar *field,
		gboolean strong)
{
	GPtrArray *ret, *ar;
	struct rspamd_mime_header *cur;
	guint i;

	ar = g_hash_table_lookup (htb, field);

	if (ar == NULL) {
		return NULL;
	}

	if (strong && pool != NULL) {
		/* Need to filter what we have */
		ret = g_ptr_array_sized_new (ar->len);

		PTR_ARRAY_FOREACH (ar, i, cur) {
			if (strcmp (cur->name, field) != 0) {
				continue;
			}

			g_ptr_array_add (ret, cur);
		}

		rspamd_mempool_add_destructor (pool,
				(rspamd_mempool_destruct_t)rspamd_ptr_array_free_hard, ret);
	}
	else {
		ret = ar;
	}

	return ret;
}

GPtrArray *
rspamd_message_get_header_array (struct rspamd_task *task,
		const gchar *field,
		gboolean strong)
{
	return rspamd_message_get_header_from_hash (task->raw_headers,
			task->task_pool, field, strong);
}

GPtrArray *
rspamd_message_get_mime_header_array (struct rspamd_task *task,
		const gchar *field,
		gboolean strong)
{
	GPtrArray *ret, *ar;
	struct rspamd_mime_header *cur;
	guint nelems = 0, i, j;
	struct rspamd_mime_part *mp;

	for (i = 0; i < task->parts->len; i ++) {
		mp = g_ptr_array_index (task->parts, i);
		ar = g_hash_table_lookup (mp->raw_headers, field);

		if (ar == NULL) {
			continue;
		}

		nelems += ar->len;
	}

	if (nelems == 0) {
		return NULL;
	}

	ret = g_ptr_array_sized_new (nelems);

	for (i = 0; i < task->parts->len; i ++) {
		mp = g_ptr_array_index (task->parts, i);
		ar = g_hash_table_lookup (mp->raw_headers, field);

		PTR_ARRAY_FOREACH (ar, j, cur) {
			if (strong) {
				if (strcmp (cur->name, field) != 0) {
					continue;
				}
			}

			g_ptr_array_add (ret, cur);
		}
	}

	rspamd_mempool_add_destructor (task->task_pool,
		(rspamd_mempool_destruct_t)rspamd_ptr_array_free_hard, ret);

	return ret;
}
