/* GTK3 + WebKit2GTK 4.1 window code for Spectral Browser.
 * Policy and state live in Go (see ui.go); this file builds widgets and
 * forwards WebKit signals. Strings returned by go* calls are freed here.
 *
 * Each Win is a top-level window: toolbar, favorites bar, a GtkNotebook of
 * Tabs (one WebKitWebView each, sharing the window's settings, cookie-consent
 * filter and network session) beside the Ghost Panel, and a downloads bar.
 * Downloads (DL) are app-wide and listed in one Downloads window. */
#include <stdlib.h>
#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include "browser.h"
#include "_cgo_export.h"

enum { ROW_CONN, ROW_OUT, ROW_IN };
enum { COL_TAB, COL_A, COL_B, COL_C, COL_D, COL_E, COL_COLOR, N_COLS };
#define MAX_ROWS 300

typedef struct Win Win;
typedef struct DL DL;

typedef struct {
	Win *w;
	int id;
	WebKitWebView *view;
	GtkWidget *page, *info, *info_label; /* page = HTTPS info bar + view */
	GtkWidget *http_btn, *cookie_btn;     /* info bar buttons */
	GtkWidget *label, *title, *spinner;  /* notebook tab label */
	char *http_uri;      /* http:// URL offered after an HTTPS failure */
	char *pending;       /* first URL, loaded once the cookie filter is ready */
	gboolean committed;  /* has shown a page */
	gboolean from_link;  /* opened by a link/popup: closed again if it only downloads */
	gboolean nav_dl;     /* a page response here is turning into a download */
} Tab;

struct Win {
	GtkWidget *win, *nb, *entry, *star, *shield, *favbar, *paned, *panel, *only_tab;
	GtkWidget *hconn, *hout, *hin;
	GtkTreeModel *filt[3];
	GtkWidget *dlbar, *dl_label, *dl_progress, *dl_open, *dl_folder;
	DL *dl; /* download shown in the bar */
	WebKitUserContentManager *ucm;
	WebKitSettings *settings;
	gboolean priv, dead;
	char tag[16]; /* active tab's Ghost Panel tag, e.g. "#3" */
};

enum { DL_RUNNING, DL_DONE, DL_FAILED, DL_CANCELLED };
static const char *dl_state_name[] = {"running", "finished", "failed", "cancelled"};

struct DL {
	WebKitDownload *d; /* NULL for a page saved as a web archive */
	char *uri, *name, *path, *error;
	int state, tab, pct;
	gboolean from_nav; /* converted page load: the Ghost Panel already has its request */
	GtkWidget *row, *title, *status, *bar, *open, *folder, *cancel;
};

static GList *wins, *dls;
static WebKitWebContext *main_ctx, *priv_ctx;
static WebKitUserContentFilterStore *fstore;
static WebKitUserContentFilter *consent_filter;
static gboolean filter_ready, quitting;
static GtkListStore *stores[3]; /* exist only while a Ghost Panel is open */
static int panels_open;
static guint tick_id;
static gboolean monitoring;
static int msgs_while_off; /* extension reports that arrived while closed (should stay ~0) */
static GtkWidget *dl_win, *dl_list;
static int tab_seq;
static char *reload_site;   /* host whose tabs reload after a cookie toggle */
static char *last_save_dir; /* folder last picked in a save dialog (this session) */
static char *selftest;      /* SPECTRAL_SELFTEST base URL: hidden self-test mode */
static char *selftest_name; /* file name the self-test types into the next save dialog */

static Win *make_win(gboolean priv, const char *uri, gboolean ghost);
static Tab *new_tab(Win *w, WebKitWebView *related, const char *uri, gboolean focus, int pos);
static void close_tab(Tab *t);
static void rebuild_favs(void);
static void refresh_buttons(Win *w);
static void update_filter(gboolean reload);
static void purge_cookies(gboolean quit);
static void show_downloads(void);
static void on_download_started(WebKitWebContext *c, WebKitDownload *d, gpointer data);

static void dbg(const char *fmt, ...) G_GNUC_PRINTF(1, 2);
static void dbg(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char *s = g_strdup_vprintf(fmt, ap);
	va_end(ap);
	goDebug(s);
	g_free(s);
}

const char *sb_xdg_download_dir(void) {
	const char *d = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
	return d ? d : "";
}

/* ---------- contexts and settings ---------- */

static WebKitWebContext *make_ctx(gboolean ephemeral) {
	WebKitWebContext *c;
	if (ephemeral) {
		c = webkit_web_context_new_ephemeral();
	} else {
		char *data = goDataDir(), *cache = goCacheDir();
		WebKitWebsiteDataManager *dm = webkit_website_data_manager_new(
			"base-data-directory", data, "base-cache-directory", cache, NULL);
		c = webkit_web_context_new_with_website_data_manager(dm);
		char *jar = g_build_filename(data, "cookies.sqlite", NULL);
		webkit_cookie_manager_set_persistent_storage(webkit_website_data_manager_get_cookie_manager(dm),
			jar, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
		g_free(jar);
		g_object_unref(dm);
		free(data);
		free(cache);
	}
	WebKitWebsiteDataManager *dm = webkit_web_context_get_website_data_manager(c);
	webkit_cookie_manager_set_accept_policy(webkit_website_data_manager_get_cookie_manager(dm),
		WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY);
	webkit_website_data_manager_set_itp_enabled(dm, TRUE);
	webkit_web_context_set_sandbox_enabled(c, TRUE); /* before any web view exists */
	/* DOCUMENT_BROWSER: smaller memory/disk cache than WEB_BROWSER but keeps a
	 * small back/forward page cache, so Back/Forward stay quick. */
	webkit_web_context_set_cache_model(c, WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER);

	char *ext = goExtDir(), *bl = goBlocklistPath();
	if (*ext) {
		char *bldir = g_path_get_dirname(bl);
		webkit_web_context_add_path_to_sandbox(c, bldir, TRUE);
		g_free(bldir);
		webkit_web_context_set_web_extensions_directory(c, ext);
		webkit_web_context_set_web_extensions_initialization_user_data(c, g_variant_new("(sb)", bl, monitoring));
	}
	free(ext);
	free(bl);
	g_signal_connect(c, "download-started", G_CALLBACK(on_download_started), NULL);
	return c;
}

static void set_bool(GObject *o, const char *name, gboolean v) {
	if (g_object_class_find_property(G_OBJECT_GET_CLASS(o), name)) g_object_set(o, name, v, NULL);
}

static WebKitSettings *make_settings(void) {
	WebKitSettings *s = webkit_settings_new();
	GObject *o = G_OBJECT(s);
	set_bool(o, "enable-dns-prefetching", FALSE);
	/* <a ping> auditing: WebKit 2.52 ignores the old setting; pings to listed trackers are still cancelled by the extension. */
	set_bool(o, "enable-webrtc", FALSE);             /* leaks local IPs */
	set_bool(o, "enable-developer-extras", FALSE);
	set_bool(o, "javascript-can-access-clipboard", FALSE);
	set_bool(o, "javascript-can-open-windows-automatically", FALSE); /* pop-ups need a click */
	set_bool(o, "enable-media-stream", TRUE); /* still gated by permission prompt */
	return s;
}

static void apply_dark(void) {
	g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", goDark(), NULL);
}

/* ---------- tab lookup ---------- */

static Tab *tab_of_page(GtkWidget *p) { return p ? g_object_get_data(G_OBJECT(p), "tab") : NULL; }
static int n_tabs(Win *w) { return gtk_notebook_get_n_pages(GTK_NOTEBOOK(w->nb)); }
static Tab *nth_tab(Win *w, int i) { return tab_of_page(gtk_notebook_get_nth_page(GTK_NOTEBOOK(w->nb), i)); }
static int tab_index(Tab *t) { return gtk_notebook_page_num(GTK_NOTEBOOK(t->w->nb), t->page); }

static Tab *cur_tab(Win *w) {
	int i = gtk_notebook_get_current_page(GTK_NOTEBOOK(w->nb));
	return i < 0 ? NULL : nth_tab(w, i);
}

static gboolean is_cur(Tab *t) { return !t->w->dead && cur_tab(t->w) == t; }

static Tab *tab_where(WebKitWebView *v, int id) {
	for (GList *l = wins; l; l = l->next) {
		Win *w = l->data;
		for (int i = 0; i < n_tabs(w); i++) {
			Tab *t = nth_tab(w, i);
			if (t && ((v && t->view == v) || (id && t->id == id))) return t;
		}
	}
	return NULL;
}

static Win *any_win(void) {
	for (GList *l = wins; l; l = l->next)
		if (gtk_window_is_active(GTK_WINDOW(((Win *)l->data)->win))) return l->data;
	return wins ? g_list_last(wins)->data : NULL;
}

/* ---------- navigation ---------- */

static void load(Tab *t, const char *in) {
	char *u = goNormalizeURL((char *)in);
	if (*u) webkit_web_view_load_uri(t->view, u);
	free(u);
}

static const char *tab_uri(Tab *t) {
	const char *u = t ? webkit_web_view_get_uri(t->view) : NULL;
	return u ? u : "";
}

static const char *cur_uri(Win *w) { return tab_uri(cur_tab(w)); }

static void on_activate(GtkEntry *e, Win *w) {
	Tab *t = cur_tab(w);
	if (t) load(t, gtk_entry_get_text(e));
}
static void on_back(GtkWidget *b, Win *w) { if (cur_tab(w)) webkit_web_view_go_back(cur_tab(w)->view); }
static void on_fwd(GtkWidget *b, Win *w) { if (cur_tab(w)) webkit_web_view_go_forward(cur_tab(w)->view); }
static void on_reload(GtkWidget *b, Win *w) { if (cur_tab(w)) webkit_web_view_reload(cur_tab(w)->view); }
static void on_home(GtkWidget *b, Win *w) {
	char *h = goHomeURL();
	if (cur_tab(w)) load(cur_tab(w), h);
	free(h);
}

static void set_win_title(Win *w) {
	Tab *t = cur_tab(w);
	const char *title = t ? webkit_web_view_get_title(t->view) : NULL;
	char *s = g_strdup_printf("%s%s — Spectral Browser", w->priv ? "Private — " : "", title && *title ? title : "New Tab");
	gtk_window_set_title(GTK_WINDOW(w->win), s);
	g_free(s);
}

static void update_label(Tab *t) {
	const char *title = webkit_web_view_get_title(t->view), *u = tab_uri(t);
	const char *text = title && *title ? title : *u ? u : "New Tab";
	gtk_label_set_text(GTK_LABEL(t->title), text);
	char *tip = g_strdup_printf("%s\nTab #%d%s", text, t->id, t->w->priv ? " (private)" : "");
	gtk_widget_set_tooltip_text(t->label, tip);
	g_free(tip);
}

static void on_uri(WebKitWebView *v, GParamSpec *p, Tab *t) {
	update_label(t);
	if (!is_cur(t)) return;
	gtk_entry_set_text(GTK_ENTRY(t->w->entry), tab_uri(t));
	refresh_buttons(t->w);
}

static void on_title(WebKitWebView *v, GParamSpec *p, Tab *t) {
	update_label(t);
	if (is_cur(t)) set_win_title(t->w);
}

/* Ghost Panel "current tab only" filter. */
static gboolean row_visible(GtkTreeModel *m, GtkTreeIter *it, gpointer data) {
	Win *w = data;
	if (w->dead || !w->only_tab || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w->only_tab))) return TRUE;
	char *tag = NULL;
	gtk_tree_model_get(m, it, COL_TAB, &tag, -1);
	gboolean ok = !tag || !*tag || strcmp(tag, w->tag) == 0;
	g_free(tag);
	return ok;
}

static void refilter(Win *w) {
	for (int i = ROW_OUT; i <= ROW_IN; i++)
		if (w->filt[i]) gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(w->filt[i]));
}

/* Address bar, buttons, title and Ghost Panel filter follow the active tab. */
static void on_switch(GtkNotebook *nb, GtkWidget *page, guint num, Win *w) {
	if (w->dead) return;
	Tab *t = tab_of_page(page);
	if (!t) return;
	gtk_entry_set_text(GTK_ENTRY(w->entry), tab_uri(t));
	set_win_title(w);
	refresh_buttons(w);
	g_snprintf(w->tag, sizeof w->tag, "#%d", t->id);
	refilter(w);
	dbg("[tabs] active tab #%d (%d of %d)%s", t->id, (int)num + 1, n_tabs(w), w->priv ? " private" : "");
}

/* HTTPS-only: upgrade http:// top-level navigations unless allowed this session.
 * Middle-click / Ctrl+click on a link opens it in a background tab. */
static gboolean on_nav_policy(WebKitWebView *v, WebKitPolicyDecision *d, WebKitPolicyDecisionType type, Tab *t) {
	WebKitNavigationAction *a = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(d));
	const char *uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(a));
	if (!uri) uri = "";
	if (webkit_navigation_action_get_navigation_type(a) == WEBKIT_NAVIGATION_TYPE_LINK_CLICKED &&
	    (webkit_navigation_action_get_mouse_button(a) == 2 || (webkit_navigation_action_get_modifiers(a) & GDK_CONTROL_MASK)) &&
	    *uri && !g_str_has_prefix(uri, "javascript:")) {
		webkit_policy_decision_ignore(d);
		Tab *n = new_tab(t->w, NULL, uri, FALSE, tab_index(t) + 1);
		n->from_link = TRUE;
		return TRUE;
	}
	if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;
	char *up = goUpgradeURL((char *)uri);
	gboolean handled = FALSE;
	if (*up) {
		webkit_policy_decision_ignore(d);
		webkit_web_view_load_uri(v, up);
		handled = TRUE;
	}
	free(up);
	return handled;
}

static gboolean is_attachment(WebKitURIResponse *r) {
	SoupMessageHeaders *h = webkit_uri_response_get_http_headers(r);
	const char *cd = h ? soup_message_headers_get_one(h, "Content-Disposition") : NULL;
	if (!cd) return FALSE;
	while (*cd == ' ' || *cd == '\t') cd++;
	return g_ascii_strncasecmp(cd, "attachment", 10) == 0;
}

/* Responses the page can't show (zip, octet-stream, ...) and attachments are
 * downloaded. WebKit's default silently ignores the former. */
static gboolean on_response_policy(WebKitPolicyDecision *d, Tab *t) {
	WebKitResponsePolicyDecision *rd = WEBKIT_RESPONSE_POLICY_DECISION(d);
	WebKitURIResponse *r = webkit_response_policy_decision_get_response(rd);
	gboolean main = webkit_response_policy_decision_is_main_frame_main_resource(rd);
	gboolean shown = webkit_response_policy_decision_is_mime_type_supported(rd);
	guint status = webkit_uri_response_get_status_code(r);
	gboolean ok = status == 0 || (status >= 200 && status < 300 && status != 204 && status != 205);
	if (is_attachment(r) && ok) {
		t->nav_dl = TRUE;
		webkit_policy_decision_download(d);
	} else if (shown) {
		webkit_policy_decision_use(d);
	} else if (main && ok) {
		t->nav_dl = TRUE;
		webkit_policy_decision_download(d);
	} else {
		webkit_policy_decision_ignore(d);
		return TRUE;
	}
	if (!shown || is_attachment(r)) {
		const char *mime = webkit_uri_response_get_mime_type(r);
		dbg("[download] tab #%d response %s (%s%s) -> download", t->id, webkit_uri_response_get_uri(r),
			mime ? mime : "?", is_attachment(r) ? ", attachment" : "");
	}
	return TRUE;
}

static gboolean on_policy(WebKitWebView *v, WebKitPolicyDecision *d, WebKitPolicyDecisionType type, Tab *t) {
	if (t->w->dead) return FALSE;
	if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE) return on_response_policy(d, t);
	return on_nav_policy(v, d, type, t);
}

static void on_shield(GtkButton *b, Win *w);

static void show_info(Tab *t, const char *text, const char *http_uri) {
	g_free(t->http_uri);
	t->http_uri = g_strdup(http_uri);
	gtk_label_set_text(GTK_LABEL(t->info_label), text);
	gtk_widget_set_no_show_all(t->info, FALSE);
	gtk_widget_show_all(t->info);
	gtk_widget_set_visible(t->http_btn, http_uri != NULL);
	gtk_widget_set_visible(t->cookie_btn, http_uri == NULL);
}

static void on_info(GtkInfoBar *bar, int resp, Tab *t) {
	gtk_widget_hide(t->info);
	if (resp == 2 && !t->w->dead) {
		if (!is_cur(t)) gtk_notebook_set_current_page(GTK_NOTEBOOK(t->w->nb), tab_index(t));
		on_shield(NULL, t->w);
	}
	if (resp == 1 && t->http_uri) {
		goAllowHTTP(t->http_uri);
		webkit_web_view_load_uri(t->view, t->http_uri);
	}
}

static gboolean on_failed(WebKitWebView *v, WebKitLoadEvent e, char *uri, GError *err, Tab *t) {
	if (g_error_matches(err, WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_CANCELLED)) return FALSE;
	if (g_error_matches(err, WEBKIT_POLICY_ERROR, WEBKIT_POLICY_ERROR_FRAME_LOAD_INTERRUPTED_BY_POLICY_CHANGE)) return FALSE;
	char *http = goHTTPFallback(uri);
	if (*http) {
		char *msg = g_strdup_printf("This site didn't load over a secure connection (%s). Spectral is HTTPS-only.", err->message);
		show_info(t, msg, http);
		g_free(msg);
	}
	free(http);
	return FALSE;
}

static gboolean on_tls(WebKitWebView *v, char *uri, GTlsCertificate *c, GTlsCertificateFlags f, Tab *t) {
	char *http = goHTTPFallback(uri);
	if (*http) show_info(t, "This site's HTTPS certificate is invalid. Spectral is HTTPS-only.", http);
	free(http);
	return FALSE;
}

static void on_load_changed(WebKitWebView *v, WebKitLoadEvent e, Tab *t) {
	switch (e) {
	case WEBKIT_LOAD_STARTED:
		gtk_widget_hide(t->info);
		gtk_widget_show(t->spinner);
		gtk_spinner_start(GTK_SPINNER(t->spinner));
		break;
	case WEBKIT_LOAD_COMMITTED:
		t->committed = TRUE;
		break;
	case WEBKIT_LOAD_FINISHED:
		gtk_spinner_stop(GTK_SPINNER(t->spinner));
		gtk_widget_hide(t->spinner);
		if (goIsGoogleLogin((char *)tab_uri(t)) && !goCookiesAllowed("google.com"))
			show_info(t, "Google sign-in needs cookies, and they're off for Google. Allow them (this covers google.com, "
				"gmail.com and youtube.com) and the page reloads.", NULL);
		if (goIsGoogleLogin((char *)tab_uri(t)) && !goCookiesAllowed("google.com"))
			dbg("[consent] tab #%d: Google sign-in with cookies off; showing the Allow cookies bar", t->id);
		dbg("[tabs] tab #%d loaded %s", t->id, tab_uri(t));
		break;
	default:
		break;
	}
}

static void on_ready(WebKitWebView *v, Tab *t) {
	gtk_notebook_set_current_page(GTK_NOTEBOOK(t->w->nb), tab_index(t));
}

/* target=_blank links and window.open() (only allowed after a click) open a tab. */
static GtkWidget *on_create(WebKitWebView *v, WebKitNavigationAction *a, Tab *t) {
	if (t->w->dead) return NULL;
	Tab *n = new_tab(t->w, v, NULL, FALSE, tab_index(t) + 1);
	n->from_link = TRUE;
	g_signal_connect(n->view, "ready-to-show", G_CALLBACK(on_ready), n);
	dbg("[tabs] tab #%d opened tab #%d (link target / window.open)", t->id, n->id);
	return GTK_WIDGET(n->view);
}

static void on_view_close(WebKitWebView *v, Tab *t) { close_tab(t); }

/* Default-deny prompt for camera, mic, location, notifications, etc. */
static gboolean on_permission(WebKitWebView *v, WebKitPermissionRequest *r, Tab *t) {
	const char *what = "use a protected browser feature";
	if (WEBKIT_IS_GEOLOCATION_PERMISSION_REQUEST(r)) what = "know your location";
	else if (WEBKIT_IS_NOTIFICATION_PERMISSION_REQUEST(r)) what = "show notifications";
	else if (WEBKIT_IS_USER_MEDIA_PERMISSION_REQUEST(r)) {
		WebKitUserMediaPermissionRequest *m = WEBKIT_USER_MEDIA_PERMISSION_REQUEST(r);
		gboolean cam = webkit_user_media_permission_is_for_video_device(m);
		gboolean mic = webkit_user_media_permission_is_for_audio_device(m);
		what = cam && mic ? "use your camera and microphone" : cam ? "use your camera" : "use your microphone";
	} else if (WEBKIT_IS_DEVICE_INFO_PERMISSION_REQUEST(r)) what = "list your cameras and microphones";
	else if (WEBKIT_IS_WEBSITE_DATA_ACCESS_PERMISSION_REQUEST(r)) what = "use its cookies while embedded in another site";
	else if (WEBKIT_IS_POINTER_LOCK_PERMISSION_REQUEST(r)) what = "lock your mouse pointer";
	char *host = goHostOf((char *)tab_uri(t));
	if (!is_cur(t)) gtk_notebook_set_current_page(GTK_NOTEBOOK(t->w->nb), tab_index(t));
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(t->w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION,
		GTK_BUTTONS_NONE, "%s wants to %s.", *host ? host : "This page", what);
	gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Deny", GTK_RESPONSE_REJECT, "_Allow once", GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_REJECT);
	int resp = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	free(host);
	if (resp == GTK_RESPONSE_ACCEPT) webkit_permission_request_allow(r);
	else webkit_permission_request_deny(r);
	return TRUE;
}

/* ---------- Ghost Panel traffic ---------- */

static gboolean on_message(WebKitWebView *v, WebKitUserMessage *m, Tab *t) {
	if (strcmp(webkit_user_message_get_name(m), "out") != 0) return FALSE;
	if (!monitoring) { /* late message from before the panel closed */
		msgs_while_off++;
		return TRUE;
	}
	GVariant *p = webkit_user_message_get_parameters(m);
	if (!p || !g_variant_is_of_type(p, G_VARIANT_TYPE("(ssss)"))) return TRUE;
	const char *verdict, *method, *uri, *accept;
	g_variant_get(p, "(&s&s&s&s)", &verdict, &method, &uri, &accept);
	goRecordOut(t->id, (char *)verdict, (char *)method, (char *)uri, (char *)accept);
	return TRUE;
}

static void on_res_finished(WebKitWebResource *r, gpointer id) {
	WebKitURIResponse *resp = webkit_web_resource_get_response(r);
	if (!resp) return;
	const char *mime = webkit_uri_response_get_mime_type(resp);
	goRecordIn(GPOINTER_TO_INT(id), (char *)webkit_web_resource_get_uri(r), (int)webkit_uri_response_get_status_code(resp),
		(char *)(mime ? mime : ""), (long long)webkit_uri_response_get_content_length(resp));
}

static void on_res_started(WebKitWebView *v, WebKitWebResource *r, WebKitURIRequest *req, Tab *t) {
	if (!monitoring) return;
	g_signal_connect(r, "finished", G_CALLBACK(on_res_finished), GINT_TO_POINTER(t->id));
}

void sb_add_row(int which, char *tab, char *a, char *b, char *c, char *d, char *e, char *color) {
	GtkListStore *s = stores[which];
	if (!s) return;
	gtk_list_store_insert_with_values(s, NULL, 0, COL_TAB, tab, COL_A, a, COL_B, b, COL_C, c, COL_D, d, COL_E, e,
		COL_COLOR, *color ? color : NULL, -1);
	int n = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(s), NULL);
	GtkTreeIter it;
	if (n > MAX_ROWS && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(s), &it, NULL, n - 1))
		gtk_list_store_remove(s, &it);
}

/* Update row idx in place (append if past the end); only touched cells change. */
void sb_set_row(int which, int idx, char *a, char *b, char *c, char *d) {
	GtkListStore *s = stores[which];
	if (!s) return;
	GtkTreeIter it;
	if (!gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(s), &it, NULL, idx)) {
		gtk_list_store_insert_with_values(s, NULL, -1, COL_A, a, COL_B, b, COL_C, c, COL_D, d, -1);
		return;
	}
	char *o[4];
	gtk_tree_model_get(GTK_TREE_MODEL(s), &it, COL_A, &o[0], COL_B, &o[1], COL_C, &o[2], COL_D, &o[3], -1);
	if (g_strcmp0(o[0], a) || g_strcmp0(o[1], b) || g_strcmp0(o[2], c) || g_strcmp0(o[3], d))
		gtk_list_store_set(s, &it, COL_A, a, COL_B, b, COL_C, c, COL_D, d, -1);
	for (int i = 0; i < 4; i++) g_free(o[i]);
}

/* Drop rows from index n on. */
void sb_trim_rows(int which, int n) {
	GtkListStore *s = stores[which];
	if (!s) return;
	GtkTreeIter it;
	while (gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(s), &it, NULL, n)) gtk_list_store_remove(s, &it);
}

void sb_clear_rows(int which) { if (stores[which]) gtk_list_store_clear(stores[which]); }

void sb_set_headers(char *conn, char *out, char *in) {
	for (GList *l = wins; l; l = l->next) {
		Win *w = l->data;
		if (!w->panel) continue;
		gtk_label_set_markup(GTK_LABEL(w->hconn), conn);
		gtk_label_set_markup(GTK_LABEL(w->hout), out);
		gtk_label_set_markup(GTK_LABEL(w->hin), in);
	}
}

static void on_clear(GtkButton *b, gpointer which) { goClearList(GPOINTER_TO_INT(which)); }

typedef struct { const char *name; int col; } Col;

static GtkWidget *section(Win *w, int which, const Col *cols, int n, int expand, GtkWidget **header) {
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	*header = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(*header), 0);
	GtkWidget *clear = gtk_button_new_with_label("Clear");
	gtk_button_set_relief(GTK_BUTTON(clear), GTK_RELIEF_NONE);
	g_signal_connect(clear, "clicked", G_CALLBACK(on_clear), GINT_TO_POINTER(which));
	gtk_box_pack_start(GTK_BOX(top), *header, TRUE, TRUE, 4);
	gtk_box_pack_end(GTK_BOX(top), clear, FALSE, FALSE, 0);

	GtkTreeModel *m;
	if (which == ROW_CONN) {
		m = GTK_TREE_MODEL(g_object_ref(stores[which]));
	} else {
		m = gtk_tree_model_filter_new(GTK_TREE_MODEL(stores[which]), NULL);
		gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(m), row_visible, w, NULL);
		w->filt[which] = m;
	}
	GtkWidget *tv = gtk_tree_view_new_with_model(m);
	g_object_unref(m);
	for (int i = 0; i < n; i++) {
		GtkCellRenderer *r = gtk_cell_renderer_text_new();
		g_object_set(r, "ellipsize", PANGO_ELLIPSIZE_END, "font", "Monospace 9", NULL);
		GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(cols[i].name, r, "text", cols[i].col,
			"foreground", COL_COLOR, NULL);
		gtk_tree_view_column_set_resizable(c, TRUE);
		if (cols[i].col == expand) gtk_tree_view_column_set_expand(c, TRUE);
		gtk_tree_view_append_column(GTK_TREE_VIEW(tv), c);
	}
	GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(sw), tv);
	gtk_widget_set_vexpand(sw, TRUE);
	gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);
	return box;
}

static void on_only_tab(GtkToggleButton *b, Win *w) { refilter(w); }

static void on_panel_close(GtkButton *b, Win *w);

static GtkWidget *ghost_panel(Win *w) {
	static const Col conn[] = {{"Host", COL_A}, {"Remote", COL_B}, {"Protocol", COL_C}, {"Process", COL_D}};
	static const Col out[] = {{"Tab", COL_TAB}, {"Time", COL_A}, {"Flag", COL_B}, {"Method", COL_C}, {"Host", COL_D}, {"Type", COL_E}};
	static const Col in[] = {{"Tab", COL_TAB}, {"Time", COL_A}, {"Status", COL_B}, {"Type", COL_C}, {"Host", COL_D}, {"Size", COL_E}};
	GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	w->only_tab = gtk_check_button_new_with_label("Current tab only (Outgoing / Incoming)");
	gtk_widget_set_tooltip_text(w->only_tab, "Show only rows from the tab you're looking at. Hover a tab to see its number.");
	g_signal_connect(w->only_tab, "toggled", G_CALLBACK(on_only_tab), w);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_set_homogeneous(GTK_BOX(box), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(outer), 4);
	gtk_box_pack_start(GTK_BOX(box), section(w, ROW_CONN, conn, 4, COL_A, &w->hconn), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), section(w, ROW_OUT, out, 6, COL_D, &w->hout), TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), section(w, ROW_IN, in, 6, COL_D, &w->hin), TRUE, TRUE, 0);
	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	GtkWidget *x = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
	gtk_widget_set_tooltip_text(x, "Close the Ghost Panel and stop monitoring (F8)");
	g_signal_connect(x, "clicked", G_CALLBACK(on_panel_close), w);
	gtk_box_pack_start(GTK_BOX(top), w->only_tab, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(top), x, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(outer), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(outer), box, TRUE, TRUE, 0);
	return outer;
}

static gboolean tick(gpointer d);

/* Ghost Panel monitoring runs only while at least one panel is open: the
 * row stores, the 0.5 s batch timer, the Go connection scanner and the
 * extension's per-request reports all start and stop together. */
static void tell_extensions(WebKitWebContext *c, gboolean on) {
	if (!c) return;
	char *bl = goBlocklistPath();
	webkit_web_context_set_web_extensions_initialization_user_data(c, g_variant_new("(sb)", bl, on));
	free(bl);
	webkit_web_context_send_message_to_all_extensions(c, webkit_user_message_new("monitor", g_variant_new_boolean(on)));
}

static void set_monitoring(gboolean on) {
	if (on == monitoring) return;
	monitoring = on;
	if (on) {
		for (int i = 0; i < 3; i++)
			stores[i] = gtk_list_store_new(N_COLS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
		tick_id = g_timeout_add(1000, tick, NULL);
	} else {
		if (tick_id) g_source_remove(tick_id);
		tick_id = 0;
		for (int i = 0; i < 3; i++) g_clear_object(&stores[i]); /* rows freed once the views are gone */
	}
	tell_extensions(main_ctx, on);
	tell_extensions(priv_ctx, on);
	goMonitor(on);
}

static GtkWidget *ghost_panel(Win *w);

static void close_panel(Win *w) {
	if (!w->panel) return;
	gtk_widget_destroy(w->panel);
	w->panel = w->only_tab = w->hconn = w->hout = w->hin = NULL;
	for (int i = 0; i < 3; i++) w->filt[i] = NULL;
	if (--panels_open == 0) set_monitoring(FALSE);
}

static void toggle_panel(Win *w) {
	if (w->panel) {
		close_panel(w);
		return;
	}
	if (panels_open++ == 0) set_monitoring(TRUE);
	w->panel = ghost_panel(w);
	gtk_paned_pack2(GTK_PANED(w->paned), w->panel, FALSE, FALSE);
	gtk_widget_show_all(w->panel);
	int total = gtk_widget_get_allocated_width(w->paned);
	if (total < 300) gtk_window_get_size(GTK_WINDOW(w->win), &total, NULL);
	gtk_paned_set_position(GTK_PANED(w->paned), total - goPanelWidth());
	goPanelShown(1);
	goFlush();
}

static void on_position(GObject *p, GParamSpec *s, Win *w) {
	if (!w->panel) return;
	int total = gtk_widget_get_allocated_width(w->paned);
	int width = total - gtk_paned_get_position(GTK_PANED(p));
	if (total > 400 && width >= 150 && width <= total - 200) goSetPanelWidth(width);
}

static gboolean tick(gpointer d) {
	goFlush();
	return G_SOURCE_CONTINUE;
}

/* ---------- downloads ---------- */

static gboolean dl_running(void) {
	for (GList *l = dls; l; l = l->next)
		if (((DL *)l->data)->state == DL_RUNNING) return TRUE;
	return FALSE;
}

static void maybe_quit(void) {
	if (!wins && !dl_running()) purge_cookies(TRUE);
}

static char *dl_status(DL *dl) {
	switch (dl->state) {
	case DL_DONE: {
		char *dir = dl->path ? g_path_get_dirname(dl->path) : g_strdup("");
		long long n = dl->d ? (long long)webkit_download_get_received_data_length(dl->d) : 0;
		if (!n && dl->path) {
			GStatBuf st;
			if (g_stat(dl->path, &st) == 0) n = st.st_size;
		}
		char *sz = goHumanSize(n);
		char *s = g_strdup_printf("Saved · %s · in %s", sz, dir);
		free(sz);
		g_free(dir);
		return s;
	}
	case DL_FAILED:
		return g_strdup_printf("Failed: %s", dl->error ? dl->error : "unknown error");
	case DL_CANCELLED:
		return g_strdup("Cancelled");
	}
	if (!dl->d || !dl->path) return g_strdup("Starting…");
	long long got = (long long)webkit_download_get_received_data_length(dl->d), total = 0;
	WebKitURIResponse *r = webkit_download_get_response(dl->d);
	if (r) total = (long long)webkit_uri_response_get_content_length(r);
	char *a = goHumanSize(got), *s;
	if (total > 0) {
		char *b = goHumanSize(total);
		s = g_strdup_printf("Downloading… %d%% · %s of %s", (int)(100 * webkit_download_get_estimated_progress(dl->d)), a, b);
		free(b);
	} else {
		s = g_strdup_printf("Downloading… %s", a);
	}
	free(a);
	return s;
}

static void dl_sync_bar(Win *w) {
	DL *dl = w->dl;
	if (!dl) {
		gtk_widget_hide(w->dlbar);
		return;
	}
	char *st = dl_status(dl);
	char *text = g_strdup_printf("%s — %s", dl->name ? dl->name : "download", st);
	gtk_label_set_text(GTK_LABEL(w->dl_label), text);
	gtk_widget_set_tooltip_text(w->dl_label, dl->path ? dl->path : dl->uri);
	gtk_widget_set_visible(w->dl_progress, dl->state == DL_RUNNING);
	if (dl->d) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->dl_progress), webkit_download_get_estimated_progress(dl->d));
	gtk_widget_set_visible(w->dl_open, dl->state == DL_DONE);
	gtk_widget_set_visible(w->dl_folder, dl->state == DL_DONE);
	g_free(st);
	g_free(text);
}

static void dl_refresh(DL *dl) {
	if (dl->row) {
		char *st = dl_status(dl);
		gtk_label_set_text(GTK_LABEL(dl->title), dl->name ? dl->name : "download");
		gtk_widget_set_tooltip_text(dl->title, dl->uri);
		gtk_label_set_text(GTK_LABEL(dl->status), st);
		gtk_widget_set_tooltip_text(dl->status, dl->path);
		g_free(st);
		gtk_widget_set_visible(dl->bar, dl->state == DL_RUNNING);
		if (dl->d) gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(dl->bar), webkit_download_get_estimated_progress(dl->d));
		gtk_widget_set_visible(dl->open, dl->state == DL_DONE);
		gtk_widget_set_visible(dl->folder, dl->state == DL_DONE);
		gtk_widget_set_visible(dl->cancel, dl->state == DL_RUNNING && dl->d);
	}
	for (GList *l = wins; l; l = l->next)
		if (((Win *)l->data)->dl == dl) dl_sync_bar(l->data);
}

static void show_dlbar(Win *w, DL *dl) {
	w->dl = dl;
	gtk_widget_set_no_show_all(w->dlbar, FALSE);
	gtk_widget_show_all(w->dlbar);
	dl_sync_bar(w);
}

static void error_dialog(const char *msg, const char *detail) {
	Win *w = any_win();
	GtkWidget *d = gtk_message_dialog_new(w ? GTK_WINDOW(w->win) : NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s", msg);
	if (detail) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s", detail);
	g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show(d);
}

static void open_path(const char *path) {
	GError *err = NULL;
	char *uri = g_filename_to_uri(path, NULL, NULL);
	if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &err)) {
		error_dialog("Couldn't open the file", err ? err->message : path);
		g_clear_error(&err);
	}
	g_free(uri);
}

static void shown_in_folder(GObject *bus, GAsyncResult *r, gpointer path) {
	GError *err = NULL;
	GVariant *v = g_dbus_connection_call_finish(G_DBUS_CONNECTION(bus), r, &err);
	if (v) {
		g_variant_unref(v);
	} else { /* no FileManager1 service: just open the folder */
		g_error_free(err);
		char *dir = g_path_get_dirname(path);
		open_path(dir);
		g_free(dir);
	}
	g_free(path);
}

/* Ask the file manager to show and select the file (org.freedesktop.FileManager1). */
static void show_in_folder(const char *path) {
	GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
	char *uri = g_filename_to_uri(path, NULL, NULL);
	if (!bus || !uri) {
		char *dir = g_path_get_dirname(path);
		open_path(dir);
		g_free(dir);
	} else {
		const char *uris[] = {uri, NULL};
		g_dbus_connection_call(bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
			"org.freedesktop.FileManager1", "ShowItems", g_variant_new("(^ass)", uris, ""), NULL,
			G_DBUS_CALL_FLAGS_NONE, 5000, NULL, shown_in_folder, g_strdup(path));
	}
	g_free(uri);
	if (bus) g_object_unref(bus);
}

static void on_dl_open(GtkButton *b, DL *dl) { if (dl->path) open_path(dl->path); }
static void on_dl_folder(GtkButton *b, DL *dl) { if (dl->path) show_in_folder(dl->path); }
static void on_dl_cancel(GtkButton *b, DL *dl) { if (dl->d && dl->state == DL_RUNNING) webkit_download_cancel(dl->d); }
static void on_bar_open(GtkButton *b, Win *w) { if (w->dl) on_dl_open(b, w->dl); }
static void on_bar_folder(GtkButton *b, Win *w) { if (w->dl) on_dl_folder(b, w->dl); }
static void on_bar_all(GtkButton *b, Win *w) { show_downloads(); }
static void on_bar_close(GtkButton *b, Win *w) {
	w->dl = NULL;
	gtk_widget_hide(w->dlbar);
}

static void dl_free(DL *dl) {
	for (GList *l = wins; l; l = l->next) {
		Win *w = l->data;
		if (w->dl == dl) on_bar_close(NULL, w);
	}
	if (dl->d) {
		g_signal_handlers_disconnect_by_data(dl->d, dl);
		g_object_unref(dl->d);
	}
	if (dl->row) gtk_widget_destroy(dl->row);
	g_free(dl->uri);
	g_free(dl->name);
	g_free(dl->path);
	g_free(dl->error);
	g_free(dl);
}

static void on_dl_clear(GtkButton *b, gpointer x) {
	GList *keep = NULL;
	for (GList *l = dls; l; l = l->next) {
		DL *dl = l->data;
		if (dl->state == DL_RUNNING) keep = g_list_append(keep, dl);
		else dl_free(dl);
	}
	g_list_free(dls);
	dls = keep;
}

static void on_dl_dir(GtkButton *b, gpointer x) {
	char *d = goDownloadFolder();
	open_path(d);
	free(d);
}

static gboolean on_dl_win_delete(GtkWidget *win, GdkEvent *e, gpointer x) {
	gtk_widget_hide(win);
	if (!wins) { /* last browser window is gone: closing this stops what's left */
		for (GList *l = dls; l; l = l->next) on_dl_cancel(NULL, l->data);
		maybe_quit();
	}
	return TRUE;
}

static void ensure_dl_win(void) {
	if (dl_win) return;
	dl_win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(dl_win), "Downloads — Spectral Browser");
	gtk_window_set_default_size(GTK_WINDOW(dl_win), 640, 420);
	gtk_window_set_icon_name(GTK_WINDOW(dl_win), "spectral-browser");
	g_signal_connect(dl_win, "delete-event", G_CALLBACK(on_dl_win_delete), NULL);
	GtkWidget *top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *title = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(title), "<b>Downloads</b>");
	GtkWidget *dir = gtk_button_new_with_label("Open downloads folder");
	GtkWidget *clear = gtk_button_new_with_label("Clear finished");
	g_signal_connect(dir, "clicked", G_CALLBACK(on_dl_dir), NULL);
	g_signal_connect(clear, "clicked", G_CALLBACK(on_dl_clear), NULL);
	gtk_box_pack_start(GTK_BOX(top), title, FALSE, FALSE, 4);
	gtk_box_pack_end(GTK_BOX(top), clear, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(top), dir, FALSE, FALSE, 0);
	dl_list = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(dl_list), GTK_SELECTION_NONE);
	GtkWidget *ph = gtk_label_new("No downloads yet");
	gtk_widget_show(ph);
	gtk_list_box_set_placeholder(GTK_LIST_BOX(dl_list), ph);
	GtkWidget *sw = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(sw), dl_list);
	gtk_widget_set_vexpand(sw, TRUE);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(box), 8);
	gtk_box_pack_start(GTK_BOX(box), top, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), sw, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(dl_win), box);
	gtk_widget_show_all(box);
}

static void show_downloads(void) {
	ensure_dl_win();
	gtk_window_present(GTK_WINDOW(dl_win));
}

static GtkWidget *small_button(const char *label, GCallback cb, gpointer data) {
	GtkWidget *b = gtk_button_new_with_label(label);
	g_signal_connect(b, "clicked", cb, data);
	return b;
}

static DL *dl_new(WebKitDownload *d, const char *uri, int tab) {
	DL *dl = g_new0(DL, 1);
	dl->d = d ? g_object_ref(d) : NULL;
	dl->uri = g_strdup(uri ? uri : "");
	char *base = g_path_get_basename(dl->uri);
	dl->name = g_strdup(*base && strcmp(base, "/") != 0 && strcmp(base, ".") != 0 ? base : "download");
	g_free(base);
	dl->tab = tab;
	dl->pct = -1;
	dls = g_list_append(dls, dl);

	ensure_dl_win();
	dl->title = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(dl->title), 0);
	gtk_label_set_ellipsize(GTK_LABEL(dl->title), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_hexpand(dl->title, TRUE);
	dl->status = gtk_label_new(NULL);
	gtk_label_set_xalign(GTK_LABEL(dl->status), 0);
	gtk_label_set_ellipsize(GTK_LABEL(dl->status), PANGO_ELLIPSIZE_MIDDLE);
	gtk_style_context_add_class(gtk_widget_get_style_context(dl->status), "dim-label");
	dl->bar = gtk_progress_bar_new();
	dl->open = small_button("Open", G_CALLBACK(on_dl_open), dl);
	dl->folder = small_button("Show in folder", G_CALLBACK(on_dl_folder), dl);
	dl->cancel = small_button("Cancel", G_CALLBACK(on_dl_cancel), dl);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
	gtk_container_set_border_width(GTK_CONTAINER(grid), 6);
	gtk_grid_attach(GTK_GRID(grid), dl->title, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), dl->status, 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), dl->bar, 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), dl->open, 1, 0, 1, 2);
	gtk_grid_attach(GTK_GRID(grid), dl->folder, 2, 0, 1, 2);
	gtk_grid_attach(GTK_GRID(grid), dl->cancel, 3, 0, 1, 2);
	dl->row = gtk_list_box_row_new();
	gtk_container_add(GTK_CONTAINER(dl->row), grid);
	gtk_widget_show_all(dl->row);
	gtk_list_box_insert(GTK_LIST_BOX(dl_list), dl->row, 0);
	dl_refresh(dl);
	return dl;
}

/* Self-test only: fill in and accept a save dialog without a person. */
static gboolean selftest_accept(gpointer dlg) {
	char *dir = goSelftestSaveDir();
	if (*dir) gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dlg), dir);
	if (selftest_name) gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), selftest_name);
	g_clear_pointer(&selftest_name, g_free);
	free(dir);
	dbg("[selftest] save dialog \"%s\" accepted", gtk_window_get_title(GTK_WINDOW(dlg)));
	gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
	return G_SOURCE_REMOVE;
}

/* GTK save dialog. Returns a new path, or NULL if cancelled. With page=TRUE it
 * offers "HTML only" and "Web archive (MHTML)" and sets *mhtml. */
static char *ask_save_path(Win *w, const char *title, const char *name, gboolean page, gboolean *mhtml) {
	GtkWidget *dlg = gtk_file_chooser_dialog_new(title, w ? GTK_WINDOW(w->win) : NULL, GTK_FILE_CHOOSER_ACTION_SAVE,
		"_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
	GtkFileChooser *fc = GTK_FILE_CHOOSER(dlg);
	gtk_file_chooser_set_do_overwrite_confirmation(fc, TRUE);
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
	char *def = goDownloadFolder();
	gtk_file_chooser_set_current_folder(fc, last_save_dir ? last_save_dir : def);
	free(def);
	gtk_file_chooser_set_current_name(fc, name);
	GtkFileFilter *mht = NULL;
	if (page) {
		GtkFileFilter *html = gtk_file_filter_new();
		gtk_file_filter_set_name(html, "Web page, HTML only");
		gtk_file_filter_add_pattern(html, "*");
		mht = gtk_file_filter_new();
		gtk_file_filter_set_name(mht, "Web archive, complete (.mhtml)");
		gtk_file_filter_add_pattern(mht, "*.mhtml");
		gtk_file_filter_add_pattern(mht, "*.mht");
		gtk_file_chooser_add_filter(fc, html);
		gtk_file_chooser_add_filter(fc, mht);
	}
	if (selftest) g_timeout_add(700, selftest_accept, dlg);
	int r = gtk_dialog_run(GTK_DIALOG(dlg));
	char *path = r == GTK_RESPONSE_ACCEPT ? gtk_file_chooser_get_filename(fc) : NULL;
	if (path) {
		if (mhtml) *mhtml = gtk_file_chooser_get_filter(fc) == mht || g_str_has_suffix(path, ".mhtml") || g_str_has_suffix(path, ".mht");
		g_free(last_save_dir);
		last_save_dir = g_path_get_dirname(path);
	}
	gtk_widget_destroy(dlg);
	return path;
}

static gboolean close_blank(gpointer id) {
	Tab *t = tab_where(NULL, GPOINTER_TO_INT(id));
	if (t && t->from_link && !t->committed && n_tabs(t->w) > 1) {
		dbg("[tabs] closing tab #%d: it only started a download", t->id);
		close_tab(t);
	}
	return G_SOURCE_REMOVE;
}

static gboolean on_decide(WebKitDownload *d, char *suggested, DL *dl) {
	char *safe = goSafeName(suggested ? suggested : dl->name);
	const char *preset = g_object_get_data(G_OBJECT(d), "dest");
	gboolean ask = g_object_get_data(G_OBJECT(d), "ask") != NULL || goAskDownload();
	char *path = NULL;
	if (preset) {
		path = g_strdup(preset); /* chosen in Save Page As; overwrite already confirmed */
	} else if (ask) {
		Tab *t = tab_where(NULL, dl->tab);
		path = ask_save_path(t ? t->w : any_win(), "Save File", safe, FALSE, NULL);
	} else {
		char *p = goUniqueDownloadPath(safe);
		path = g_strdup(p);
		free(p);
	}
	free(safe);
	g_idle_add(close_blank, GINT_TO_POINTER(dl->tab));
	if (!path) {
		dl->state = DL_CANCELLED;
		goDownloadEvent("cancelled", dl->tab, dl->uri, "save dialog cancelled");
		webkit_download_cancel(d);
		dl_refresh(dl);
		return TRUE;
	}
	webkit_download_set_allow_overwrite(d, g_file_test(path, G_FILE_TEST_EXISTS));
	char *uri = g_filename_to_uri(path, NULL, NULL);
	webkit_download_set_destination(d, uri);
	g_free(uri);
	g_free(dl->path);
	dl->path = path;
	g_free(dl->name);
	dl->name = g_path_get_basename(path);
	goDownloadEvent("destination", dl->tab, dl->uri, path);
	dl_refresh(dl);
	return TRUE;
}

static void on_dl_created(WebKitDownload *d, char *dest, DL *dl) {
	char *p = g_filename_from_uri(dest, NULL, NULL);
	g_free(dl->path);
	dl->path = p ? p : g_strdup(dest);
	dl_refresh(dl);
}

static void on_dl_progress(WebKitDownload *d, GParamSpec *ps, DL *dl) {
	int pct = (int)(100 * webkit_download_get_estimated_progress(d));
	if (pct == dl->pct) return;
	dl->pct = pct;
	dl_refresh(dl);
}

static void on_dl_failed(WebKitDownload *d, GError *e, DL *dl) {
	if (dl->state != DL_RUNNING) return;
	dl->state = g_error_matches(e, WEBKIT_DOWNLOAD_ERROR, WEBKIT_DOWNLOAD_ERROR_CANCELLED_BY_USER) ? DL_CANCELLED : DL_FAILED;
	g_free(dl->error);
	dl->error = g_strdup(e->message);
}

static void on_dl_finished(WebKitDownload *d, DL *dl) {
	if (dl->state == DL_RUNNING) dl->state = DL_DONE;
	WebKitURIResponse *r = webkit_download_get_response(d);
	if (r && !dl->from_nav) {
		const char *mime = webkit_uri_response_get_mime_type(r);
		goRecordIn(dl->tab, dl->uri, (int)webkit_uri_response_get_status_code(r), (char *)(mime ? mime : ""),
			(long long)webkit_download_get_received_data_length(d));
	}
	goDownloadEvent((char *)dl_state_name[dl->state], dl->tab, dl->uri,
		dl->state == DL_DONE ? dl->path : dl->error ? dl->error : "");
	dl_refresh(dl);
	maybe_quit();
}

/* Every download (page responses, context-menu items, Save As, Ctrl+S) comes
 * through here, in normal and private windows alike. */
static void on_download_started(WebKitWebContext *c, WebKitDownload *d, gpointer data) {
	WebKitURIRequest *req = webkit_download_get_request(d);
	const char *uri = req ? webkit_uri_request_get_uri(req) : "";
	Tab *t = tab_where(webkit_download_get_web_view(d), 0);
	DL *dl = dl_new(d, uri, t ? t->id : 0);
	webkit_download_set_allow_overwrite(d, FALSE);
	g_signal_connect(d, "decide-destination", G_CALLBACK(on_decide), dl);
	g_signal_connect(d, "created-destination", G_CALLBACK(on_dl_created), dl);
	g_signal_connect(d, "notify::estimated-progress", G_CALLBACK(on_dl_progress), dl);
	g_signal_connect(d, "failed", G_CALLBACK(on_dl_failed), dl);
	g_signal_connect(d, "finished", G_CALLBACK(on_dl_finished), dl);
	if (t && t->nav_dl) {
		dl->from_nav = TRUE;
		t->nav_dl = FALSE;
	} else {
		goRecordOut(dl->tab, "Download", "GET", (char *)uri, "");
	}
	goDownloadEvent("started", dl->tab, (char *)uri, c == priv_ctx ? "private" : "");
	Win *w = t ? t->w : any_win();
	if (w) show_dlbar(w, dl);
}

/* Download uri from tab t. ask forces the save dialog ("Save … As"). */
static void save_uri(Tab *t, const char *uri, gboolean ask) {
	WebKitDownload *d = webkit_web_view_download_uri(t->view, uri);
	if (!d) return;
	if (ask) g_object_set_data(G_OBJECT(d), "ask", GINT_TO_POINTER(1));
	g_object_unref(d);
}

static void page_saved(GObject *o, GAsyncResult *r, gpointer data) {
	DL *dl = data;
	GError *err = NULL;
	if (webkit_web_view_save_to_file_finish(WEBKIT_WEB_VIEW(o), r, &err)) {
		dl->state = DL_DONE;
	} else {
		dl->state = DL_FAILED;
		dl->error = g_strdup(err->message);
		g_error_free(err);
	}
	goDownloadEvent((char *)dl_state_name[dl->state], dl->tab, dl->uri, dl->state == DL_DONE ? dl->path : dl->error);
	dl_refresh(dl);
	maybe_quit();
}

/* Ctrl+S: save the page as HTML (re-downloaded) or a complete MHTML archive. */
static void save_page(Tab *t) {
	if (!t) return;
	const char *u = tab_uri(t);
	if (!g_str_has_prefix(u, "http://") && !g_str_has_prefix(u, "https://") && !g_str_has_prefix(u, "file://")) return;
	const char *title = webkit_web_view_get_title(t->view);
	char *base = goSafeName((char *)(title && *title ? title : "page"));
	char *name = g_strdup_printf("%s.html", base);
	free(base);
	gboolean mhtml = FALSE;
	char *path = ask_save_path(t->w, "Save Page As", name, TRUE, &mhtml);
	g_free(name);
	if (!path) return;
	if (mhtml) {
		DL *dl = dl_new(NULL, u, t->id);
		dl->path = path;
		g_free(dl->name);
		dl->name = g_path_get_basename(path);
		GFile *f = g_file_new_for_path(path);
		webkit_web_view_save_to_file(t->view, f, WEBKIT_SAVE_MODE_MHTML, NULL, page_saved, dl);
		g_object_unref(f);
		show_dlbar(t->w, dl);
		goDownloadEvent("started", t->id, (char *)u, "web archive");
	} else {
		WebKitDownload *d = webkit_web_view_download_uri(t->view, u);
		if (d) {
			g_object_set_data_full(G_OBJECT(d), "dest", path, g_free);
			g_object_unref(d);
		} else {
			g_free(path);
		}
	}
}

/* ---------- context menu ---------- */

static void on_ctx_new_tab(GSimpleAction *a, GVariant *p, Tab *t) {
	Tab *n = new_tab(t->w, NULL, g_variant_get_string(p, NULL), FALSE, tab_index(t) + 1);
	n->from_link = TRUE;
}
static void on_ctx_new_window(GSimpleAction *a, GVariant *p, Tab *t) { make_win(t->w->priv, g_variant_get_string(p, NULL), FALSE); }
static void on_ctx_save_as(GSimpleAction *a, GVariant *p, Tab *t) { save_uri(t, g_variant_get_string(p, NULL), TRUE); }
static void on_ctx_download(GSimpleAction *a, GVariant *p, Tab *t) { save_uri(t, g_variant_get_string(p, NULL), FALSE); }
static void on_ctx_save_page(GSimpleAction *a, GVariant *p, Tab *t) { save_page(t); }

static void ctx_item(WebKitContextMenu *menu, int pos, const char *label, const char *uri, GCallback cb, Tab *t) {
	/* Each item needs its own action name: WebKit keys the menu's actions by
	 * name, so items sharing one name all ran the last one (Save Link As…). */
	static guint ctx_seq;
	char *name = g_strdup_printf("spectral-%u", ++ctx_seq);
	GSimpleAction *a = g_simple_action_new(name, G_VARIANT_TYPE_STRING);
	g_free(name);
	g_signal_connect(a, "activate", cb, t);
	webkit_context_menu_insert(menu, webkit_context_menu_item_new_from_gaction(G_ACTION(a), label, g_variant_new_string(uri)), pos);
	g_object_unref(a);
}

/* Links and images get "Open … in New Tab", "Save … As…" (always asks) and
 * "Download …" (follows the Ask-where-to-save setting); pages get "Save Page As…". */
static gboolean on_context_menu(WebKitWebView *v, WebKitContextMenu *menu, GdkEvent *e, WebKitHitTestResult *h, Tab *t) {
	const char *link = webkit_hit_test_result_context_is_link(h) ? webkit_hit_test_result_get_link_uri(h) : NULL;
	const char *img = webkit_hit_test_result_context_is_image(h) ? webkit_hit_test_result_get_image_uri(h) : NULL;
	gboolean got_tab = FALSE, got_save_link = FALSE, got_save_img = FALSE;
	for (int i = (int)webkit_context_menu_get_n_items(menu) - 1; i >= 0; i--) {
		WebKitContextMenuItem *it = webkit_context_menu_get_item_at_position(menu, i);
		switch (webkit_context_menu_item_get_stock_action(it)) {
		case WEBKIT_CONTEXT_MENU_ACTION_OPEN_LINK_IN_NEW_WINDOW:
			if (!link) break;
			webkit_context_menu_remove(menu, it);
			ctx_item(menu, i, "Open Link in New _Window", link, G_CALLBACK(on_ctx_new_window), t);
			ctx_item(menu, i, "Open Link in New _Tab", link, G_CALLBACK(on_ctx_new_tab), t);
			got_tab = TRUE;
			break;
		case WEBKIT_CONTEXT_MENU_ACTION_DOWNLOAD_LINK_TO_DISK: /* stock "Download Linked File" stays */
			if (!link) break;
			ctx_item(menu, i, "Save Lin_k As…", link, G_CALLBACK(on_ctx_save_as), t);
			got_save_link = TRUE;
			break;
		case WEBKIT_CONTEXT_MENU_ACTION_OPEN_IMAGE_IN_NEW_WINDOW:
			if (!img) break;
			webkit_context_menu_remove(menu, it);
			ctx_item(menu, i, "Open _Image in New Tab", img, G_CALLBACK(on_ctx_new_tab), t);
			break;
		case WEBKIT_CONTEXT_MENU_ACTION_DOWNLOAD_IMAGE_TO_DISK:
			if (!img) break;
			webkit_context_menu_remove(menu, it);
			ctx_item(menu, i, "Download Ima_ge", img, G_CALLBACK(on_ctx_download), t);
			ctx_item(menu, i, "Sa_ve Image As…", img, G_CALLBACK(on_ctx_save_as), t);
			got_save_img = TRUE;
			break;
		default:
			break;
		}
	}
	/* In case this WebKit build left the stock items out. */
	if (link && !got_tab) ctx_item(menu, 0, "Open Link in New _Tab", link, G_CALLBACK(on_ctx_new_tab), t);
	if (link && !got_save_link) {
		webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());
		ctx_item(menu, -1, "Save Lin_k As…", link, G_CALLBACK(on_ctx_save_as), t);
		ctx_item(menu, -1, "_Download Linked File", link, G_CALLBACK(on_ctx_download), t);
	}
	if (img && !got_save_img) {
		webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());
		ctx_item(menu, -1, "Sa_ve Image As…", img, G_CALLBACK(on_ctx_save_as), t);
		ctx_item(menu, -1, "Download Ima_ge", img, G_CALLBACK(on_ctx_download), t);
	}
	if (!link && !img && !webkit_hit_test_result_context_is_media(h) && !webkit_hit_test_result_context_is_selection(h) &&
	    !webkit_hit_test_result_context_is_editable(h)) {
		webkit_context_menu_append(menu, webkit_context_menu_item_new_separator());
		ctx_item(menu, -1, "_Save Page As…", tab_uri(t), G_CALLBACK(on_ctx_save_page), t);
	}
	if (selftest) { /* log the menu instead of showing it */
		GString *s = g_string_new(NULL);
		for (GList *l = webkit_context_menu_get_items(menu); l; l = l->next) {
			GtkAction *a = webkit_context_menu_item_get_action(l->data);
			const char *label = a ? gtk_action_get_label(a) : NULL;
			g_string_append_printf(s, "%s | ", label ? label : "—");
		}
		dbg("[selftest] context menu (link=%s image=%s): %s", link ? link : "-", img ? img : "-", s->str);
		g_string_free(s, TRUE);
		return TRUE;
	}
	return FALSE;
}

/* ---------- cookie consent filter ---------- */

static void filter_saved(GObject *o, GAsyncResult *res, gpointer reload) {
	GError *err = NULL;
	WebKitUserContentFilter *f = webkit_user_content_filter_store_save_finish(fstore, res, &err);
	if (!f) {
		g_warning("cookie consent filter failed: %s", err->message);
		g_error_free(err);
	} else {
		if (consent_filter) webkit_user_content_filter_unref(consent_filter);
		consent_filter = f;
	}
	filter_ready = TRUE;
	for (GList *l = wins; l; l = l->next) {
		Win *w = l->data;
		webkit_user_content_manager_remove_all_filters(w->ucm);
		if (consent_filter) webkit_user_content_manager_add_filter(w->ucm, consent_filter);
		for (int i = 0; i < n_tabs(w); i++) {
			Tab *t = nth_tab(w, i);
			if (t->pending) {
				load(t, t->pending);
				g_clear_pointer(&t->pending, g_free);
			} else if (GPOINTER_TO_INT(reload) && reload_site && *tab_uri(t)) {
				char *host = goHostOf((char *)tab_uri(t));
				if (goSameSite(host, reload_site)) webkit_web_view_reload(t->view);
				free(host);
			}
		}
	}
	goFilterApplied(f != NULL);
}

static void update_filter(gboolean reload) {
	char *json = goConsentFilterJSON();
	GBytes *b = g_bytes_new(json, strlen(json));
	free(json);
	webkit_user_content_filter_store_save(fstore, "cookie-consent", b, NULL, filter_saved, GINT_TO_POINTER(reload));
	g_bytes_unref(b);
}

/* Startup and exit cleanup. Cookies: every site when "Clear cookies when
 * browser closes" is on (the default), otherwise only unaccepted sites.
 * Local storage, IndexedDB, service workers etc.: unaccepted sites. Running
 * it at startup too covers a crash or kill. */
#define SITE_STORAGE (WEBKIT_WEBSITE_DATA_LOCAL_STORAGE | WEBKIT_WEBSITE_DATA_SESSION_STORAGE | \
	WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES | WEBKIT_WEBSITE_DATA_SERVICE_WORKER_REGISTRATIONS | \
	WEBKIT_WEBSITE_DATA_DOM_CACHE | WEBKIT_WEBSITE_DATA_OFFLINE_APPLICATION_CACHE)
static int purge_pending;

static void purged(GObject *o, GAsyncResult *r, gpointer quit) {
	webkit_website_data_manager_remove_finish(WEBKIT_WEBSITE_DATA_MANAGER(o), r, NULL);
	if (--purge_pending == 0 && GPOINTER_TO_INT(quit)) gtk_main_quit();
}

static void fetched(GObject *o, GAsyncResult *r, gpointer quit) {
	WebKitWebsiteDataManager *dm = WEBKIT_WEBSITE_DATA_MANAGER(o);
	GList *all = webkit_website_data_manager_fetch_finish(dm, r, NULL), *cookies = NULL, *storage = NULL;
	gboolean everything = goClearOnExit();
	for (GList *l = all; l; l = l->next) {
		WebKitWebsiteData *d = l->data;
		gboolean ok = goCookiesAllowed((char *)webkit_website_data_get_name(d));
		WebKitWebsiteDataTypes types = webkit_website_data_get_types(d);
		if ((types & WEBKIT_WEBSITE_DATA_COOKIES) && (everything || !ok)) cookies = g_list_prepend(cookies, d);
		if ((types & SITE_STORAGE) && !ok) storage = g_list_prepend(storage, d);
	}
	goCookiesPurged(g_list_length(cookies), g_list_length(storage));
	purge_pending = 1;
	if (cookies) {
		purge_pending++;
		webkit_website_data_manager_remove(dm, WEBKIT_WEBSITE_DATA_COOKIES, cookies, NULL, purged, quit);
	}
	if (storage) {
		purge_pending++;
		webkit_website_data_manager_remove(dm, SITE_STORAGE, storage, NULL, purged, quit);
	}
	if (--purge_pending == 0 && GPOINTER_TO_INT(quit)) gtk_main_quit();
	g_list_free(cookies);
	g_list_free(storage);
	g_list_free_full(all, (GDestroyNotify)webkit_website_data_unref);
}

static gboolean force_quit(gpointer d) {
	gtk_main_quit();
	return G_SOURCE_REMOVE;
}

static void purge_cookies(gboolean quit) {
	if (quit) {
		if (quitting) return;
		quitting = TRUE;
		for (GList *l = dls; l; l = l->next) {
			DL *dl = l->data;
			dbg("[download] summary %s %s -> %s", dl_state_name[dl->state], dl->uri,
				dl->state == DL_DONE ? dl->path : dl->error ? dl->error : "");
		}
	}
	webkit_website_data_manager_fetch(webkit_web_context_get_website_data_manager(main_ctx),
		WEBKIT_WEBSITE_DATA_COOKIES | SITE_STORAGE, NULL, fetched, GINT_TO_POINTER(quit));
	if (quit) g_timeout_add_seconds(3, force_quit, NULL);
}

static void on_shield(GtkButton *b, Win *w) {
	char *host = goHostOf((char *)cur_uri(w));
	if (*host) {
		goToggleCookies(host);
		g_free(reload_site);
		reload_site = g_strdup(host);
		for (GList *l = wins; l; l = l->next) refresh_buttons(l->data);
		update_filter(TRUE);
	}
	free(host);
}

/* ---------- favorites ---------- */

static void on_star(GtkButton *b, Win *w) {
	Tab *t = cur_tab(w);
	const char *u = tab_uri(t);
	if (!*u) return;
	const char *title = webkit_web_view_get_title(t->view);
	goToggleFavorite((char *)u, (char *)(title ? title : ""));
	rebuild_favs();
}

static void on_fav_click(GtkButton *b, Win *w) {
	char *u = goFavURL(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "idx")));
	if (cur_tab(w)) load(cur_tab(w), u);
	free(u);
}

static void on_fav_rename(GtkMenuItem *m, Win *w) {
	int i = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m), "idx"));
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Rename favorite", GTK_WINDOW(w->win), GTK_DIALOG_MODAL,
		"_Cancel", GTK_RESPONSE_CANCEL, "_Rename", GTK_RESPONSE_ACCEPT, NULL);
	GtkWidget *e = gtk_entry_new();
	char *name = goFavName(i);
	gtk_entry_set_text(GTK_ENTRY(e), name);
	free(name);
	gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
	GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
	gtk_container_set_border_width(GTK_CONTAINER(area), 10);
	gtk_box_pack_start(GTK_BOX(area), e, TRUE, TRUE, 0);
	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) goRenameFavorite(i, (char *)gtk_entry_get_text(GTK_ENTRY(e)));
	gtk_widget_destroy(dlg);
	rebuild_favs();
}

static void on_fav_remove(GtkMenuItem *m, Win *w) {
	goRemoveFavorite(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m), "idx")));
	rebuild_favs();
}

static void on_fav_tab(GtkMenuItem *m, Win *w) {
	char *u = goFavURL(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(m), "idx")));
	new_tab(w, NULL, u, TRUE, -1);
	free(u);
}

static gboolean on_fav_press(GtkWidget *b, GdkEventButton *e, Win *w) {
	if (e->type != GDK_BUTTON_PRESS) return FALSE;
	gpointer idx = g_object_get_data(G_OBJECT(b), "idx");
	if (e->button == 2) { /* middle-click: background tab */
		char *u = goFavURL(GPOINTER_TO_INT(idx));
		new_tab(w, NULL, u, FALSE, -1);
		free(u);
		return TRUE;
	}
	if (e->button != 3) return FALSE;
	GtkWidget *menu = gtk_menu_new();
	GtkWidget *tab = gtk_menu_item_new_with_label("Open in new tab");
	GtkWidget *ren = gtk_menu_item_new_with_label("Rename…"), *rm = gtk_menu_item_new_with_label("Remove");
	g_object_set_data(G_OBJECT(tab), "idx", idx);
	g_object_set_data(G_OBJECT(ren), "idx", idx);
	g_object_set_data(G_OBJECT(rm), "idx", idx);
	g_signal_connect(tab, "activate", G_CALLBACK(on_fav_tab), w);
	g_signal_connect(ren, "activate", G_CALLBACK(on_fav_rename), w);
	g_signal_connect(rm, "activate", G_CALLBACK(on_fav_remove), w);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), tab);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), ren);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), rm);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)e);
	return TRUE;
}

static void rebuild_favs(void) {
	int n = goFavCount();
	for (GList *l = wins; l; l = l->next) {
		Win *w = l->data;
		GList *kids = gtk_container_get_children(GTK_CONTAINER(w->favbar));
		for (GList *k = kids; k; k = k->next) gtk_widget_destroy(k->data);
		g_list_free(kids);
		for (int i = 0; i < n; i++) {
			char *name = goFavName(i), *url = goFavURL(i);
			GtkWidget *b = gtk_button_new_with_label(name);
			gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
			gtk_widget_set_tooltip_text(b, url);
			gtk_label_set_max_width_chars(GTK_LABEL(gtk_bin_get_child(GTK_BIN(b))), 24);
			gtk_label_set_ellipsize(GTK_LABEL(gtk_bin_get_child(GTK_BIN(b))), PANGO_ELLIPSIZE_END);
			g_object_set_data(G_OBJECT(b), "idx", GINT_TO_POINTER(i));
			g_signal_connect(b, "clicked", G_CALLBACK(on_fav_click), w);
			g_signal_connect(b, "button-press-event", G_CALLBACK(on_fav_press), w);
			gtk_box_pack_start(GTK_BOX(w->favbar), b, FALSE, FALSE, 0);
			free(name);
			free(url);
		}
		if (n == 0) gtk_box_pack_start(GTK_BOX(w->favbar), gtk_label_new("No favorites yet — click ☆ to add this page"), FALSE, FALSE, 6);
		gtk_widget_show_all(w->favbar);
		refresh_buttons(w);
	}
}

static void refresh_buttons(Win *w) {
	const char *u = cur_uri(w);
	gboolean fav = *u && goIsFavorite((char *)u);
	gtk_button_set_label(GTK_BUTTON(w->star), fav ? "★" : "☆");
	gtk_widget_set_tooltip_text(w->star, fav ? "Remove from favorites" : "Add to favorites");
	char *host = goHostOf((char *)u);
	gboolean ok = *host && goCookiesAllowed(host);
	gtk_button_set_label(GTK_BUTTON(w->shield), ok ? "🍪 Cookies on" : "🛡 Cookies off");
	char *tip = *host ? g_strdup_printf(ok ? "%s may keep cookies. Click to block them again." :
		"Cookies are blocked for %s. Click to allow cookies for this site.", host) : g_strdup("No site loaded");
	gtk_widget_set_tooltip_text(w->shield, tip);
	gtk_widget_set_sensitive(w->shield, *host != 0);
	g_free(tip);
	free(host);
}

/* ---------- menu actions ---------- */

static void on_use_current(GtkButton *b, GtkEntry *home) {
	gtk_entry_set_text(home, g_object_get_data(G_OBJECT(b), "uri"));
}

static GtkWidget *grid_label(const char *text) {
	GtkWidget *l = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	return l;
}

static void on_settings(GtkMenuItem *m, Win *w) {
	GtkWidget *dlg = gtk_dialog_new_with_buttons("Settings", GTK_WINDOW(w->win), GTK_DIALOG_MODAL,
		"_Cancel", GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);
	GtkWidget *grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
	gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
	GtkWidget *home = gtk_entry_new();
	char *h = goHomeURL();
	gtk_entry_set_text(GTK_ENTRY(home), h);
	free(h);
	gtk_entry_set_width_chars(GTK_ENTRY(home), 40);
	gtk_entry_set_activates_default(GTK_ENTRY(home), TRUE);
	GtkWidget *use = gtk_button_new_with_label("Use current page");
	g_object_set_data_full(G_OBJECT(use), "uri", g_strdup(cur_uri(w)), g_free);
	g_signal_connect(use, "clicked", G_CALLBACK(on_use_current), home);
	GtkWidget *dark = gtk_switch_new();
	gtk_switch_set_active(GTK_SWITCH(dark), goDark());
	gtk_widget_set_halign(dark, GTK_ALIGN_START);
	GtkWidget *ask = gtk_switch_new();
	gtk_switch_set_active(GTK_SWITCH(ask), goAskDownload());
	gtk_widget_set_halign(ask, GTK_ALIGN_START);
	GtkWidget *clr = gtk_switch_new();
	gtk_switch_set_active(GTK_SWITCH(clr), goClearOnExit());
	gtk_widget_set_halign(clr, GTK_ALIGN_START);
	gtk_widget_set_tooltip_text(clr, "Deletes all cookies, including sites you allowed, so you'll need to sign in again after a restart.");
	GtkWidget *folder = gtk_file_chooser_button_new("Download folder", GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
	char *dir = goDownloadFolder();
	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(folder), dir);
	free(dir);
	gtk_grid_attach(GTK_GRID(grid), grid_label("Homepage"), 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), home, 1, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), use, 2, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), grid_label("Dark mode"), 0, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), dark, 1, 1, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), grid_label("Ask where to save each download"), 0, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), ask, 1, 2, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), grid_label("Download folder"), 0, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), folder, 1, 3, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), grid_label("Clear cookies when browser closes"), 0, 4, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), clr, 1, 4, 1, 1);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))), grid, TRUE, TRUE, 0);
	gtk_widget_show_all(dlg);
	if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
		char *f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(folder));
		goSaveSettings((char *)gtk_entry_get_text(GTK_ENTRY(home)), gtk_switch_get_active(GTK_SWITCH(dark)),
			gtk_switch_get_active(GTK_SWITCH(ask)), gtk_switch_get_active(GTK_SWITCH(clr)), f ? f : "");
		g_free(f);
		apply_dark();
	}
	gtk_widget_destroy(dlg);
}

static void cleared(GObject *o, GAsyncResult *r, gpointer win) {
	GError *err = NULL;
	gboolean ok = webkit_website_data_manager_clear_finish(WEBKIT_WEBSITE_DATA_MANAGER(o), r, &err);
	GtkWidget *d = gtk_message_dialog_new(NULL, 0, ok ? GTK_MESSAGE_INFO : GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
		ok ? "Browsing data cleared." : "Couldn't clear data: %s", ok ? "" : err->message);
	g_signal_connect(d, "response", G_CALLBACK(gtk_widget_destroy), NULL);
	gtk_widget_show(d);
	if (err) g_error_free(err);
}

static void on_clear_data(GtkMenuItem *m, Win *w) {
	GtkWidget *dlg = gtk_message_dialog_new(GTK_WINDOW(w->win), GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
		"Clear all cookies, cache, and site storage (local storage, IndexedDB, service workers)?");
	gtk_dialog_add_buttons(GTK_DIALOG(dlg), "_Cancel", GTK_RESPONSE_CANCEL, "C_lear", GTK_RESPONSE_ACCEPT, NULL);
	int r = gtk_dialog_run(GTK_DIALOG(dlg));
	gtk_widget_destroy(dlg);
	if (r != GTK_RESPONSE_ACCEPT) return;
	webkit_web_context_clear_cache(main_ctx);
	webkit_website_data_manager_clear(webkit_web_context_get_website_data_manager(main_ctx),
		WEBKIT_WEBSITE_DATA_ALL, 0, NULL, cleared, NULL);
}

static void open_home_tab(Win *w) {
	char *h = goHomeURL();
	new_tab(w, NULL, h, TRUE, -1);
	free(h);
	gtk_widget_grab_focus(w->entry);
}

static void open_home_win(gboolean priv) {
	char *h = goHomeURL();
	make_win(priv, h, FALSE);
	free(h);
}

static void on_new_tab(GtkWidget *x, Win *w) { open_home_tab(w); }
static void on_new_win(GtkMenuItem *m, Win *w) { open_home_win(FALSE); }
static void on_private(GtkMenuItem *m, Win *w) { open_home_win(TRUE); }
static void on_save_page(GtkMenuItem *m, Win *w) { save_page(cur_tab(w)); }
static void on_downloads(GtkWidget *x, Win *w) { show_downloads(); }
static void on_ghost_item(GtkMenuItem *m, Win *w) { toggle_panel(w); }
static void on_panel_close(GtkButton *b, Win *w) { close_panel(w); }

/* ---------- tabs ---------- */

static void close_tab(Tab *t) {
	Win *w = t->w;
	if (w->dead) return;
	if (n_tabs(w) <= 1) {
		dbg("[tabs] closed last tab #%d: closing window", t->id);
		gtk_widget_destroy(w->win);
		return;
	}
	int i = tab_index(t);
	if (is_cur(t)) gtk_notebook_set_current_page(GTK_NOTEBOOK(w->nb), i + 1 < n_tabs(w) ? i + 1 : i - 1);
	dbg("[tabs] closed tab #%d (%d left)", t->id, n_tabs(w) - 1);
	gtk_widget_destroy(t->page);
}

static void on_tab_close(GtkButton *b, Tab *t) { close_tab(t); }

static gboolean on_tab_press(GtkWidget *x, GdkEventButton *e, Tab *t) {
	if (e->type == GDK_BUTTON_PRESS && e->button == 2) {
		close_tab(t);
		return TRUE;
	}
	return FALSE;
}

static void on_page_destroy(GtkWidget *page, Tab *t) {
	g_signal_handlers_disconnect_by_data(t->view, t);
	g_object_set_data(G_OBJECT(page), "tab", NULL);
	g_free(t->http_uri);
	g_free(t->pending);
	g_free(t);
}

/* A tab: its own web view on the window's session (the private one for
 * private windows), settings and cookie filter. related is the opener for
 * pop-ups; pos -1 appends. */
static Tab *new_tab(Win *w, WebKitWebView *related, const char *uri, gboolean focus, int pos) {
	Tab *t = g_new0(Tab, 1);
	t->w = w;
	t->id = ++tab_seq;
	if (related)
		t->view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "related-view", related,
			"settings", w->settings, "user-content-manager", w->ucm, NULL));
	else
		t->view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW, "web-context", w->priv ? priv_ctx : main_ctx,
			"settings", w->settings, "user-content-manager", w->ucm, NULL));

	t->info = gtk_info_bar_new();
	t->http_btn = gtk_info_bar_add_button(GTK_INFO_BAR(t->info), "Load over HTTP for this session", 1);
	t->cookie_btn = gtk_info_bar_add_button(GTK_INFO_BAR(t->info), "Allow cookies for Google", 2);
	gtk_info_bar_set_message_type(GTK_INFO_BAR(t->info), GTK_MESSAGE_WARNING);
	gtk_info_bar_set_show_close_button(GTK_INFO_BAR(t->info), TRUE);
	t->info_label = gtk_label_new("");
	gtk_label_set_line_wrap(GTK_LABEL(t->info_label), TRUE);
	gtk_container_add(GTK_CONTAINER(gtk_info_bar_get_content_area(GTK_INFO_BAR(t->info))), t->info_label);
	g_signal_connect(t->info, "response", G_CALLBACK(on_info), t);
	gtk_widget_set_no_show_all(t->info, TRUE);
	t->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(t->page), t->info, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(t->page), GTK_WIDGET(t->view), TRUE, TRUE, 0);
	g_object_set_data(G_OBJECT(t->page), "tab", t);
	g_signal_connect(t->page, "destroy", G_CALLBACK(on_page_destroy), t);

	t->spinner = gtk_spinner_new();
	gtk_widget_set_no_show_all(t->spinner, TRUE);
	t->title = gtk_label_new("New Tab");
	gtk_label_set_ellipsize(GTK_LABEL(t->title), PANGO_ELLIPSIZE_END);
	gtk_label_set_width_chars(GTK_LABEL(t->title), 16);
	gtk_label_set_max_width_chars(GTK_LABEL(t->title), 16);
	gtk_label_set_xalign(GTK_LABEL(t->title), 0);
	GtkWidget *x = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
	gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
	gtk_widget_set_focus_on_click(x, FALSE);
	gtk_widget_set_tooltip_text(x, "Close tab (Ctrl+W)");
	g_signal_connect(x, "clicked", G_CALLBACK(on_tab_close), t);
	GtkWidget *lbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(lbox), t->spinner, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(lbox), t->title, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(lbox), x, FALSE, FALSE, 0);
	t->label = gtk_event_box_new();
	gtk_event_box_set_visible_window(GTK_EVENT_BOX(t->label), FALSE);
	gtk_container_add(GTK_CONTAINER(t->label), lbox);
	g_signal_connect(t->label, "button-press-event", G_CALLBACK(on_tab_press), t);
	gtk_widget_show_all(t->label);
	update_label(t);

	g_signal_connect(t->view, "notify::uri", G_CALLBACK(on_uri), t);
	g_signal_connect(t->view, "notify::title", G_CALLBACK(on_title), t);
	g_signal_connect(t->view, "decide-policy", G_CALLBACK(on_policy), t);
	g_signal_connect(t->view, "load-failed", G_CALLBACK(on_failed), t);
	g_signal_connect(t->view, "load-failed-with-tls-errors", G_CALLBACK(on_tls), t);
	g_signal_connect(t->view, "load-changed", G_CALLBACK(on_load_changed), t);
	g_signal_connect(t->view, "create", G_CALLBACK(on_create), t);
	g_signal_connect(t->view, "close", G_CALLBACK(on_view_close), t);
	g_signal_connect(t->view, "permission-request", G_CALLBACK(on_permission), t);
	g_signal_connect(t->view, "user-message-received", G_CALLBACK(on_message), t);
	g_signal_connect(t->view, "resource-load-started", G_CALLBACK(on_res_started), t);
	g_signal_connect(t->view, "context-menu", G_CALLBACK(on_context_menu), t);

	gtk_widget_show_all(t->page);
	int idx = gtk_notebook_insert_page(GTK_NOTEBOOK(w->nb), t->page, t->label, pos);
	gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(w->nb), t->page, TRUE);
	dbg("[tabs] opened tab #%d at %d (%d tabs, %s window)%s%s", t->id, idx + 1, n_tabs(w), w->priv ? "private" : "normal",
		uri ? " " : "", uri ? uri : "");
	if (focus) gtk_notebook_set_current_page(GTK_NOTEBOOK(w->nb), idx);
	if (uri && *uri) {
		if (filter_ready) load(t, uri);
		else t->pending = g_strdup(uri);
	}
	return t;
}

static void go_tab(Win *w, int i) {
	int n = n_tabs(w);
	if (n > 0) gtk_notebook_set_current_page(GTK_NOTEBOOK(w->nb), ((i % n) + n) % n);
}

static gboolean on_key(GtkWidget *win, GdkEventKey *e, Win *w) {
	guint mods = e->state & gtk_accelerator_get_default_mod_mask();
	guint k = gdk_keyval_to_lower(e->keyval);
	int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(w->nb));
	Tab *t = cur_tab(w);
	if (!mods && k == GDK_KEY_F8) toggle_panel(w);
	else if (!mods && k == GDK_KEY_F5) on_reload(NULL, w);
	else if (mods == GDK_MOD1_MASK && k == GDK_KEY_Left) on_back(NULL, w);
	else if (mods == GDK_MOD1_MASK && k == GDK_KEY_Right) on_fwd(NULL, w);
	else if (mods == GDK_CONTROL_MASK) {
		switch (k) {
		case GDK_KEY_l: gtk_widget_grab_focus(w->entry); break;
		case GDK_KEY_t: open_home_tab(w); break;
		case GDK_KEY_w: if (t) close_tab(t); break;
		case GDK_KEY_r: on_reload(NULL, w); break;
		case GDK_KEY_s: save_page(t); break;
		case GDK_KEY_n: open_home_win(FALSE); break;
		case GDK_KEY_Tab: case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: go_tab(w, cur + 1); break;
		case GDK_KEY_ISO_Left_Tab: case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: go_tab(w, cur - 1); break;
		case GDK_KEY_1: case GDK_KEY_2: case GDK_KEY_3: case GDK_KEY_4: case GDK_KEY_5: case GDK_KEY_6: case GDK_KEY_7: case GDK_KEY_8:
			if ((int)(k - GDK_KEY_1) < n_tabs(w)) go_tab(w, k - GDK_KEY_1);
			break;
		case GDK_KEY_9: go_tab(w, n_tabs(w) - 1); break;
		default: return FALSE;
		}
	} else if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) {
		switch (k) {
		case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: go_tab(w, cur - 1); break;
		case GDK_KEY_n: open_home_win(TRUE); break;
		case GDK_KEY_y: show_downloads(); break;
		default: return FALSE;
		}
	} else {
		return FALSE;
	}
	return TRUE;
}

/* ---------- window ---------- */

static void on_destroy(GtkWidget *win, Win *w) {
	close_panel(w);
	w->dead = TRUE;
	wins = g_list_remove(wins, w);
	if (!wins) {
		if (dl_running()) { /* keep going until downloads finish (or the Downloads window is closed) */
			dbg("[download] last window closed; waiting for downloads to finish");
			show_downloads();
		} else {
			purge_cookies(TRUE);
		}
	}
}

static void free_win(gpointer data, GObject *gone) {
	Win *w = data;
	g_object_unref(w->ucm);
	g_object_unref(w->settings);
	g_free(w);
}

static GtkWidget *icon_button(const char *icon, const char *tip, GCallback cb, Win *w) {
	GtkWidget *b = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
	gtk_widget_set_tooltip_text(b, tip);
	g_signal_connect(b, "clicked", cb, w);
	return b;
}

static void menu_item(GtkWidget *menu, const char *label, GCallback cb, Win *w) {
	GtkWidget *it = cb ? gtk_menu_item_new_with_label(label) : gtk_separator_menu_item_new();
	if (cb) g_signal_connect(it, "activate", cb, w);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), it);
}

static GtkWidget *downloads_bar(Win *w) {
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(bar), 3);
	w->dl_label = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(w->dl_label), 0);
	gtk_label_set_ellipsize(GTK_LABEL(w->dl_label), PANGO_ELLIPSIZE_MIDDLE);
	w->dl_progress = gtk_progress_bar_new();
	gtk_widget_set_size_request(w->dl_progress, 140, -1);
	gtk_widget_set_valign(w->dl_progress, GTK_ALIGN_CENTER);
	w->dl_open = small_button("Open", G_CALLBACK(on_bar_open), w);
	w->dl_folder = small_button("Show in folder", G_CALLBACK(on_bar_folder), w);
	gtk_box_pack_start(GTK_BOX(bar), gtk_image_new_from_icon_name("folder-download-symbolic", GTK_ICON_SIZE_BUTTON), FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(bar), w->dl_label, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->dl_progress, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->dl_open, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->dl_folder, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), small_button("All downloads", G_CALLBACK(on_bar_all), w), FALSE, FALSE, 0);
	GtkWidget *x = icon_button("window-close-symbolic", "Hide", G_CALLBACK(on_bar_close), w);
	gtk_button_set_relief(GTK_BUTTON(x), GTK_RELIEF_NONE);
	gtk_box_pack_start(GTK_BOX(bar), x, FALSE, FALSE, 0);
	gtk_widget_set_no_show_all(bar, TRUE);
	return bar;
}

static Win *make_win(gboolean priv, const char *uri, gboolean ghost) {
	Win *w = g_new0(Win, 1);
	w->priv = priv;
	if (priv && !priv_ctx) priv_ctx = make_ctx(TRUE);
	w->ucm = webkit_user_content_manager_new();
	if (consent_filter) webkit_user_content_manager_add_filter(w->ucm, consent_filter);
	w->settings = make_settings();
	w->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(w->win), 1280, 820);
	gtk_window_set_title(GTK_WINDOW(w->win), priv ? "Private — Spectral Browser" : "Spectral Browser");
	gtk_window_set_icon_name(GTK_WINDOW(w->win), "spectral-browser");

	w->entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(w->entry), "Search or enter address");
	g_signal_connect(w->entry, "activate", G_CALLBACK(on_activate), w);
	w->star = gtk_button_new_with_label("☆");
	g_signal_connect(w->star, "clicked", G_CALLBACK(on_star), w);
	w->shield = gtk_button_new_with_label("🛡 Cookies off");
	g_signal_connect(w->shield, "clicked", G_CALLBACK(on_shield), w);

	GtkWidget *menu = gtk_menu_new();
	menu_item(menu, "New tab (Ctrl+T)", G_CALLBACK(on_new_tab), w);
	menu_item(menu, "New window (Ctrl+N)", G_CALLBACK(on_new_win), w);
	menu_item(menu, "New private window (Ctrl+Shift+N)", G_CALLBACK(on_private), w);
	menu_item(menu, NULL, NULL, w);
	menu_item(menu, "Save page as… (Ctrl+S)", G_CALLBACK(on_save_page), w);
	menu_item(menu, "Downloads (Ctrl+Shift+Y)", G_CALLBACK(on_downloads), w);
	menu_item(menu, NULL, NULL, w);
	menu_item(menu, "Settings…", G_CALLBACK(on_settings), w);
	menu_item(menu, "Clear browsing data…", G_CALLBACK(on_clear_data), w);
	menu_item(menu, "Ghost Panel (F8)", G_CALLBACK(on_ghost_item), w);
	gtk_widget_show_all(menu);
	GtkWidget *mb = gtk_menu_button_new();
	gtk_button_set_image(GTK_BUTTON(mb), gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
	gtk_menu_button_set_popup(GTK_MENU_BUTTON(mb), menu);

	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_box_pack_start(GTK_BOX(bar), icon_button("go-previous-symbolic", "Back (Alt+Left)", G_CALLBACK(on_back), w), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), icon_button("go-next-symbolic", "Forward (Alt+Right)", G_CALLBACK(on_fwd), w), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), icon_button("view-refresh-symbolic", "Reload (Ctrl+R)", G_CALLBACK(on_reload), w), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), icon_button("go-home-symbolic", "Home", G_CALLBACK(on_home), w), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->shield, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->entry, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(bar), w->star, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), icon_button("folder-download-symbolic", "Downloads (Ctrl+Shift+Y)", G_CALLBACK(on_downloads), w), FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(bar), mb, FALSE, FALSE, 0);

	w->favbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	w->nb = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(w->nb), TRUE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(w->nb), FALSE);
	GtkWidget *plus = icon_button("list-add-symbolic", "New tab (Ctrl+T)", G_CALLBACK(on_new_tab), w);
	gtk_button_set_relief(GTK_BUTTON(plus), GTK_RELIEF_NONE);
	gtk_widget_show(plus);
	gtk_notebook_set_action_widget(GTK_NOTEBOOK(w->nb), plus, GTK_PACK_END);
	g_signal_connect_after(w->nb, "switch-page", G_CALLBACK(on_switch), w);

	w->paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_paned_pack1(GTK_PANED(w->paned), w->nb, TRUE, FALSE);
	g_signal_connect(w->paned, "notify::position", G_CALLBACK(on_position), w);
	w->dlbar = downloads_bar(w);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), w->favbar, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), w->paned, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(box), w->dlbar, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(w->win), box);

	g_signal_connect(w->win, "destroy", G_CALLBACK(on_destroy), w);
	g_object_weak_ref(G_OBJECT(w->win), free_win, w);
	g_signal_connect(w->win, "key-press-event", G_CALLBACK(on_key), w);

	wins = g_list_append(wins, w);
	gtk_widget_show_all(w->win);
	rebuild_favs();
	new_tab(w, NULL, uri, TRUE, -1);
	if (ghost) toggle_panel(w);
	return w;
}

/* ---------- hidden self-test (SPECTRAL_SELFTEST=<base URL>) ----------
 * Drives the real code paths with synthetic key presses, downloads and
 * save dialogs that accept themselves, logging everything with --debug. */

static int st_step;
static Win *st_main, *st_priv;

static gboolean alive(Win *w) { return w && g_list_find(wins, w); }

static void send_key(Win *w, guint keyval, guint mods, const char *name) {
	GdkEvent *e = gdk_event_new(GDK_KEY_PRESS);
	e->key.window = g_object_ref(gtk_widget_get_window(w->win));
	e->key.keyval = keyval;
	e->key.state = mods;
	e->key.time = GDK_CURRENT_TIME;
	GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
	if (seat && gdk_seat_get_keyboard(seat)) gdk_event_set_device(e, gdk_seat_get_keyboard(seat));
	dbg("[selftest] key %s", name);
	if (!gtk_widget_event(w->win, e)) dbg("[selftest] key %s NOT handled", name);
	gdk_event_free(e);
}

static void send_click(Tab *t, guint button, double x, double y, const char *name) {
	GdkWindow *gw = gtk_widget_get_window(GTK_WIDGET(t->view));
	GdkSeat *seat = gdk_display_get_default_seat(gdk_display_get_default());
	dbg("[selftest] %s (button %u at %.0f,%.0f in tab #%d)", name, button, x, y, t->id);
	for (int i = 0; i < 2; i++) {
		GdkEvent *e = gdk_event_new(i ? GDK_BUTTON_RELEASE : GDK_BUTTON_PRESS);
		e->button.window = g_object_ref(gw);
		e->button.x = x;
		e->button.y = y;
		e->button.x_root = x;
		e->button.y_root = y;
		e->button.button = button;
		e->button.time = GDK_CURRENT_TIME;
		if (i) e->button.state = button == 2 ? GDK_BUTTON2_MASK : button == 3 ? GDK_BUTTON3_MASK : GDK_BUTTON1_MASK;
		if (seat && gdk_seat_get_pointer(seat)) gdk_event_set_device(e, gdk_seat_get_pointer(seat));
		gtk_widget_event(GTK_WIDGET(t->view), e);
		gdk_event_free(e);
	}
}

static void js_done(GObject *o, GAsyncResult *r, gpointer label) {
	GError *err = NULL;
	JSCValue *v = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(o), r, &err);
	if (v) {
		char *s = jsc_value_to_string(v);
		dbg("[selftest] js %s -> %s", (char *)label, s);
		g_free(s);
		g_object_unref(v);
	} else {
		dbg("[selftest] js %s error: %s", (char *)label, err->message);
		g_error_free(err);
	}
}

static void st_js(Tab *t, const char *code, const char *label) {
	if (t) webkit_web_view_evaluate_javascript(t->view, code, -1, NULL, NULL, NULL, js_done, (gpointer)label);
}

static char *st_url(const char *path) { return g_strconcat(selftest, path, NULL); }

/* SPECTRAL_SELFTEST=google: open Google sign-in, report what it says. */
static gboolean selftest_google(gpointer x) {
	static int n;
	Win *w = wins ? wins->data : NULL;
	Tab *t = w ? cur_tab(w) : NULL;
	const char *dump = "location.href + ' | UA=' + navigator.userAgent + ' | cookieEnabled=' + navigator.cookieEnabled + "
		"' | cookieLen=' + document.cookie.length + ' | TEXT: ' + document.body.innerText.replace(/\\s+/g,' ').slice(0,600)";
	switch (n++) {
	case 3: st_js(t, dump, "google page"); break;
	case 4:
		st_js(t, "var e=document.querySelector('#identifierId,input[type=email],input[name=identifier]'); if(!e) 'no email field'; else {e.focus();"
			"document.execCommand('insertText',false,'spectral.selftest.nobody.zz9@gmail.com');"
			"var b=[...document.querySelectorAll('button')].find(b=>/next/i.test(b.innerText)); if(b) b.click(); 'typed+next:'+!!b}",
			"google identifier");
		break;
	case 8: st_js(t, dump, "google after next"); break;
	case 10:
		if (w) gtk_widget_destroy(w->win);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static long rss_kb(void) {
	char *txt = NULL;
	long kb = -1;
	if (g_file_get_contents("/proc/self/status", &txt, NULL, NULL)) {
		char *p = strstr(txt, "VmRSS:");
		if (p) kb = strtol(p + 6, NULL, 10);
		g_free(txt);
	}
	return kb;
}

static int store_rows(void) {
	int n = 0;
	for (int i = 0; i < 3; i++)
		if (stores[i]) n += gtk_tree_model_iter_n_children(GTK_TREE_MODEL(stores[i]), NULL);
	return n;
}

/* SPECTRAL_SELFTEST=monitor: closed 30 s, F8 open 40 s, F8 closed 30 s, with RSS. */
static gboolean selftest_monitor(gpointer x) {
	static int n;
	Win *w = wins ? wins->data : NULL;
	n++;
	if (n % 5 == 0 || n == 1)
		dbg("[selftest] t=%ds monitoring=%d panel=%s rows=%d rss=%ld kB msgs_while_off=%d", n * 2, monitoring,
			w && w->panel ? "open" : "closed", store_rows(), rss_kb(), msgs_while_off);
	if (n == 15 || n == 35) send_key(w, GDK_KEY_F8, 0, "F8");
	if (n == 50) {
		gtk_widget_destroy(w->win);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static gboolean selftest_step(gpointer x) {
	Win *w = alive(st_main) ? st_main : NULL;
	Tab *t = w ? cur_tab(w) : NULL;
	char *u = NULL, *js = NULL;
	int step = st_step++;
	dbg("[selftest] step %d", step);
	switch (step) {
	case 0:
		st_main = wins->data;
		dbg("[selftest] start: %d window(s), %d tab(s)", g_list_length(wins), n_tabs(st_main));
		break;
	case 1: send_key(w, GDK_KEY_t, GDK_CONTROL_MASK, "Ctrl+T"); break;
	case 2: /* context menu "Save Image As…" */
		u = st_url("/pic.png");
		save_uri(t, u, TRUE);
		break;
	case 3: /* "Download Image" / stock "Download Linked File" */
	case 4: /* same again: must not overwrite */
		u = st_url("/pic.png");
		save_uri(t, u, FALSE);
		break;
	case 5: /* a link to a type WebKit can't show */
		send_key(w, GDK_KEY_1, GDK_CONTROL_MASK, "Ctrl+1");
		u = st_url("/data.bin");
		load(cur_tab(w), u);
		break;
	case 6: /* Content-Disposition: attachment */
		u = st_url("/attach");
		load(t, u);
		break;
	case 7:
		send_key(w, GDK_KEY_2, GDK_CONTROL_MASK, "Ctrl+2");
		send_key(w, GDK_KEY_s, GDK_CONTROL_MASK, "Ctrl+S (HTML)");
		break;
	case 8:
		selftest_name = g_strdup("page.mhtml");
		send_key(w, GDK_KEY_s, GDK_CONTROL_MASK, "Ctrl+S (MHTML)");
		break;
	case 9:
		st_js(t, "document.getElementById('t').click(); 'clicked target=_blank link'", "click _blank");
		break;
	case 10:
		js = g_strdup_printf("window.open('%s/?popup') ? 'opened' : 'blocked'", selftest);
		st_js(nth_tab(w, 1), js, "window.open");
		break;
	case 11:
		dbg("[selftest] tabs now %d", n_tabs(w));
		send_key(w, GDK_KEY_1, GDK_CONTROL_MASK, "Ctrl+1");
		send_key(w, GDK_KEY_Tab, GDK_CONTROL_MASK, "Ctrl+Tab");
		send_key(w, GDK_KEY_ISO_Left_Tab, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "Ctrl+Shift+Tab");
		send_key(w, GDK_KEY_Page_Down, GDK_CONTROL_MASK, "Ctrl+PgDn");
		send_key(w, GDK_KEY_Page_Up, GDK_CONTROL_MASK, "Ctrl+PgUp");
		send_key(w, GDK_KEY_9, GDK_CONTROL_MASK, "Ctrl+9");
		send_key(w, GDK_KEY_1, GDK_CONTROL_MASK, "Ctrl+1");
		break;
	case 12: send_click(t, 3, 60, 60, "right-click image"); break;
	case 13: send_click(t, 3, 100, 170, "right-click link"); break;
	case 14: send_click(t, 3, 600, 500, "right-click page"); break;
	case 15: send_click(t, 2, 100, 240, "middle-click link"); break;
	case 16:
		u = st_url("/missing.bin");
		save_uri(t, u, FALSE);
		break;
	case 17:
		u = st_url("/?private");
		st_priv = make_win(TRUE, u, FALSE);
		break;
	case 18:
		u = st_url("/data.bin");
		save_uri(cur_tab(st_priv), u, FALSE);
		send_key(st_priv, GDK_KEY_t, GDK_CONTROL_MASK, "Ctrl+T (private)");
		break;
	case 19:
		send_key(st_priv, GDK_KEY_w, GDK_CONTROL_MASK, "Ctrl+W (private)");
		dbg("[selftest] private window tabs %d", n_tabs(st_priv));
		break;
	case 20:
		for (int i = 0; i < 20 && alive(st_main); i++) send_key(st_main, GDK_KEY_w, GDK_CONTROL_MASK, "Ctrl+W");
		dbg("[selftest] main window %s", alive(st_main) ? "still open" : "closed");
		break;
	default:
		if (alive(st_priv)) send_key(st_priv, GDK_KEY_w, GDK_CONTROL_MASK, "Ctrl+W (private, last tab)");
		g_free(u);
		return G_SOURCE_REMOVE;
	}
	g_free(u);
	g_free(js);
	return G_SOURCE_CONTINUE;
}

void sb_run(char *first_url, int open_ghost) {
	gtk_init(NULL, NULL);
	apply_dark();
	char *data = goDataDir();
	char *fdir = g_build_filename(data, "filters", NULL);
	fstore = webkit_user_content_filter_store_new(fdir);
	g_free(fdir);
	free(data);
	char *st = goSelftest();
	if (*st) selftest = g_strdup(st);
	free(st);
	main_ctx = make_ctx(FALSE);
	purge_cookies(FALSE);
	make_win(FALSE, first_url, open_ghost);
	update_filter(FALSE);
	if (selftest && strcmp(selftest, "monitor") == 0) g_timeout_add(2000, selftest_monitor, NULL);
	else if (selftest && strcmp(selftest, "google") == 0) g_timeout_add(2000, selftest_google, NULL);
	else if (selftest) g_timeout_add(2500, selftest_step, NULL);
	gtk_main();
}
