#include "editor_types.h"
#include <stdarg.h>
#include <string.h>

static int     scr_x, scr_y;
static uint8_t scr_fg, scr_bg, scr_attr;

/* Set by the GUI: keep filling the cell buffer (it renders the cells itself)
 * but write nothing to the terminal. */
bool screen_headless = false;

int scr_printf(const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    int needed = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    assert(needed >= 0 && (size_t)needed < sizeof(buf) && "scr_printf output truncated");

    int printed = 0;
    for (int i = 0; buf[i] != '\0'; ) {
        if (buf[i] == '\033' && buf[i+1] == '[') {
            /* Parse CSI sequence */
            i += 2;

            int params[16];
            int nparams = 0;
            memset(params, 0, sizeof(params));

            while ((buf[i] >= '0' && buf[i] <= '9') || buf[i] == ';') {
                if (buf[i] == ';') {
                    nparams++;
                    if (nparams >= 16) nparams = 15;
                    i++;
                } else {
                    params[nparams] = params[nparams] * 10 + (buf[i] - '0');
                    i++;
                }
            }
            nparams++; /* count the last param */

            char cmd = buf[i];
            i++;

            if (cmd == 'H') {
                /* CUP: \033[row;colH */
                int row = (nparams >= 1 && params[0] > 0) ? params[0] : 1;
                int col = (nparams >= 2 && params[1] > 0) ? params[1] : 1;
                scr_y = row - 1;
                scr_x = col - 1;
            } else if (cmd == 'm') {
                /* SGR */
                if (nparams == 1 && params[0] == 0) {
                    scr_fg = 0; scr_bg = 0; scr_attr = 0;
                } else {
                    for (int p = 0; p < nparams; p++) {
                        int v = params[p];
                        if (v == 0)                   { scr_fg = 0; scr_bg = 0; scr_attr = 0; }
                        else if (v == 1)              scr_attr |= ATTR_BOLD;
                        else if (v == 4)              scr_attr |= ATTR_UNDER;
                        else if (v == 5)              scr_attr |= ATTR_BLINK;
                        else if (v >= 30 && v <= 37)  scr_fg = (uint8_t)(v - 29);
                        else if (v >= 40 && v <= 47)  scr_bg = (uint8_t)(v - 39);
                    }
                }
            }
            /* ED (0J, 2J), cursor visibility, alt screen, etc: no-op */
        } else if (buf[i] == '\n') {
            scr_y++;
            scr_x = 0;
            i++;
        } else if ((unsigned char)buf[i] >= 0x20 && (unsigned char)buf[i] != 0x7f) {
            /* Printable char */
            if (scr_y >= 0 && scr_y < SCREEN_MAX_HEIGHT &&
                    scr_x >= 0 && scr_x < SCREEN_MAX_WIDTH) {
                Cell *c = &state->screen_curr[scr_y * SCREEN_MAX_WIDTH + scr_x];
                c->ch   = buf[i];
                c->fg   = scr_fg;
                c->bg   = scr_bg;
                c->attr = scr_attr;
            }
            scr_x++;
            printed++;
            i++;
        } else {
            i++;
        }
    }
    return printed;
}

void screen_flush(void) {
    int w = state->screen_dimensions.x;
    int h = state->screen_dimensions.y;
    if (w > SCREEN_MAX_WIDTH)  w = SCREEN_MAX_WIDTH;
    if (h > SCREEN_MAX_HEIGHT) h = SCREEN_MAX_HEIGHT;
    int cur_x = -1, cur_y = -1;
    uint8_t cur_fg = 0, cur_bg = 0, cur_attr = 0;

    for (int y = 0; y < h && !screen_headless; y++) {
        for (int x = 0; x < w; x++) {
            Cell *curr = &state->screen_curr[y * SCREEN_MAX_WIDTH + x];
            Cell *prev = &state->screen_prev[y * SCREEN_MAX_WIDTH + x];
            if (memcmp(curr, prev, sizeof(Cell)) == 0) continue;

            if (cur_y != y || cur_x != x) {
                printf("\033[%d;%dH", y + 1, x + 1);
                cur_y = y; cur_x = x;
            }
            if (curr->fg != cur_fg || curr->bg != cur_bg || curr->attr != cur_attr) {
                printf("\033[0");
                if (curr->attr & ATTR_BOLD)  printf(";1");
                if (curr->attr & ATTR_UNDER) printf(";4");
                if (curr->attr & ATTR_BLINK) printf(";5");
                if (curr->fg) printf(";%d", curr->fg + 29);
                if (curr->bg) printf(";%d", curr->bg + 39);
                printf("m");
                cur_fg = curr->fg; cur_bg = curr->bg; cur_attr = curr->attr;
            }
            printf("%c", curr->ch ? curr->ch : ' ');
            cur_x++;
        }
    }
    if (!screen_headless) {
        printf("\033[m");
        fflush(stdout);
    }

    memcpy(state->screen_prev, state->screen_curr,
           sizeof(Cell) * SCREEN_MAX_HEIGHT * SCREEN_MAX_WIDTH);
    memset(state->screen_curr, 0,
           sizeof(Cell) * SCREEN_MAX_HEIGHT * SCREEN_MAX_WIDTH);

    /* Reset draw-state so next frame's scr_printf calls start with default colour */
    scr_x = 0; scr_y = 0;
    scr_fg = 0; scr_bg = 0; scr_attr = 0;
}
