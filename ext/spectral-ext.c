/* Spectral Browser web-process extension.
 *
 * Sees every subresource request (send-request), cancels third-party
 * requests to hosts on the bundled tracker list, and reports each
 * request to the UI process for the Ghost Panel. */
#include <string.h>
#include <webkit2/webkit-web-extension.h>

static GHashTable *blocked_hosts;
static gboolean monitor; /* report requests to the UI only while a Ghost Panel is open */

static gboolean listed(const char *h) {
	while (h && *h) {
		if (g_hash_table_contains(blocked_hosts, h)) return TRUE;
		h = strchr(h, '.');
		if (h) h++;
	}
	return FALSE;
}

/* Rough registrable domain: last two labels, or three for xx.co.uk style. */
static const char *base_domain(const char *h) {
	const char *last = NULL, *prev = NULL, *prev2 = NULL;
	for (const char *p = h; *p; p++)
		if (*p == '.') { prev2 = prev; prev = last; last = p; }
	if (!last) return h;
	if (!prev) return h;
	const char *b = prev + 1;
	if (prev2 && strlen(last + 1) == 2 && (last - prev - 1) <= 3) b = prev2 + 1;
	return b;
}

static char *host_of(const char *uri) {
	if (!uri) return g_strdup("");
	GUri *u = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
	if (!u) return g_strdup("");
	char *h = g_ascii_strdown(g_uri_get_host(u) ? g_uri_get_host(u) : "", -1);
	g_uri_unref(u);
	return h;
}

static gboolean on_send(WebKitWebPage *page, WebKitURIRequest *req, WebKitURIResponse *redirect, gpointer d) {
	const char *uri = webkit_uri_request_get_uri(req);
	if (!uri || !(g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://"))) return FALSE;
	char *host = host_of(uri), *top = host_of(webkit_web_page_get_uri(page));
	gboolean third = *top && strcmp(base_domain(host), base_domain(top)) != 0;
	gboolean block = third && listed(host);
	if (!monitor) { /* blocking still applies; just nothing is reported */
		g_free(host);
		g_free(top);
		return block;
	}
	const char *verdict = block ? "Blocked" : third ? "Third-party" : "Allowed";
	const char *method = webkit_uri_request_get_http_method(req);
	SoupMessageHeaders *hd = webkit_uri_request_get_http_headers(req);
	const char *accept = hd ? soup_message_headers_get_one(hd, "Accept") : NULL;
	webkit_web_page_send_message_to_view(page,
		webkit_user_message_new("out", g_variant_new("(ssss)", verdict, method ? method : "GET", uri, accept ? accept : "")),
		NULL, NULL, NULL);
	g_free(host);
	g_free(top);
	return block; /* TRUE cancels the request */
}

static void on_page(WebKitWebExtension *e, WebKitWebPage *page, gpointer d) {
	g_signal_connect(page, "send-request", G_CALLBACK(on_send), NULL);
}

static gboolean on_ui_message(WebKitWebExtension *e, WebKitUserMessage *m, gpointer d) {
	GVariant *p = webkit_user_message_get_parameters(m);
	if (strcmp(webkit_user_message_get_name(m), "monitor") != 0 || !p || !g_variant_is_of_type(p, G_VARIANT_TYPE_BOOLEAN))
		return FALSE;
	monitor = g_variant_get_boolean(p);
	return TRUE;
}

G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(WebKitWebExtension *e, GVariant *data) {
	blocked_hosts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	const char *path = NULL;
	if (data && g_variant_is_of_type(data, G_VARIANT_TYPE("(sb)"))) g_variant_get(data, "(&sb)", &path, &monitor);
	else if (data && g_variant_is_of_type(data, G_VARIANT_TYPE_STRING)) path = g_variant_get_string(data, NULL);
	char *text = NULL;
	if (path && g_file_get_contents(path, &text, NULL, NULL)) {
		char **lines = g_strsplit(text, "\n", -1);
		for (char **l = lines; *l; l++)
			if (**l && **l != '#') g_hash_table_add(blocked_hosts, g_strdup(g_strstrip(*l)));
		g_strfreev(lines);
		g_free(text);
	} else {
		g_warning("spectral-ext: could not read blocklist %s", path ? path : "(none)");
	}
	g_signal_connect(e, "page-created", G_CALLBACK(on_page), NULL);
	g_signal_connect(e, "user-message-received", G_CALLBACK(on_ui_message), NULL);
}
