#ifndef SPECTRAL_BROWSER_H
#define SPECTRAL_BROWSER_H
void sb_run(char *first_url, int open_ghost);
void sb_add_row(int which, char *tab, char *a, char *b, char *c, char *d, char *e, char *color);
void sb_clear_rows(int which);
void sb_set_row(int which, int idx, char *a, char *b, char *c, char *d);
void sb_trim_rows(int which, int n);
void sb_set_headers(char *conn, char *out, char *in);
const char *sb_xdg_download_dir(void);
#endif
