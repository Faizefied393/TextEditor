// kilo.c - minimal terminal text editor (single-file)
// Build: make
// Run:   ./kilo [filename]

#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*config constants*/

#define KILO_VERSION "1.0"
#define TAB_STOP 8
#define QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)

enum Key {
  KEY_BACKSPACE = 127,
  KEY_ARROW_LEFT = 1000,
  KEY_ARROW_RIGHT,
  KEY_ARROW_UP,
  KEY_ARROW_DOWN,
  KEY_DEL,
  KEY_HOME,
  KEY_END,
  KEY_PAGE_UP,
  KEY_PAGE_DOWN
};

enum Highlight {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_MLCOMMENT,
  HL_KEYWORD1,
  HL_KEYWORD2,
  HL_STRING,
  HL_NUMBER,
  HL_MATCH
};

#define HL_NUMBERS   (1 << 0)
#define HL_STRINGS   (1 << 1)

/* data types  */

typedef struct Row {
  int idx;                  // row index in file
  int size;                 // size of chars
  int rsize;                // size of render
  char *chars;              // raw chars
  char *render;             // rendered chars (tabs expanded)
  unsigned char *hl;        // highlight for render
  int hl_open_comment;      // if multiline comment continues
} Row;

typedef struct Syntax {
  const char *filetype;
  const char **filematch;   // extensions / substrings
  const char **keywords;    // words; those ending with | are keyword2
  const char *scs;          // single-line comment start
  const char *mcs;          // multi-line comment start
  const char *mce;          // multi-line comment end
  int flags;
} Syntax;

typedef struct Editor {
  int cx, cy;               // cursor x/y in chars
  int rx;                   // cursor x in render
  int rowoff;               // row scroll offset
  int coloff;               // col scroll offset
  int screenrows;           // terminal rows usable for text
  int screencols;           // terminal cols
  int numrows;
  Row *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  Syntax *syntax;
  struct termios orig_termios;
} Editor;

static Editor E;

/*  filetypes  */

static const char *C_EXTS[] = { ".c", ".h", ".cpp", ".hpp", NULL };

static const char *C_KW[] = {
  "switch","if","while","for","break","continue","return","else",
  "struct","union","typedef","static","enum","class","case",

  "int|","long|","double|","float|","char|","unsigned|","signed|",
  "void|","size_t|","ssize_t|","bool|", NULL
};

static Syntax HLDB[] = {
  { "c", C_EXTS, C_KW, "//", "/*", "*/", HL_NUMBERS | HL_STRINGS },
};

#define HLDB_COUNT ((int)(sizeof(HLDB)/sizeof(HLDB[0])))

/* utilities  */

static void die(const char *msg) {
  // Clear screen and reposition cursor, then print error
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);
  perror(msg);
  exit(1);
}

static void *xmalloc(size_t n) {
  void *p = malloc(n);
  if (!p) die("malloc");
  return p;
}

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n);
  if (!q) die("realloc");
  return q;
}

static char *xstrdup(const char *s) {
  char *p = strdup(s);
  if (!p) die("strdup");
  return p;
}

/* terminal */

static void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    // Can't safely call die() from atexit if terminal is odd; try best effort
  }
}

static void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

static int editorReadKey(void) {
  int nread;
  char c;
  while ((nread = (int)read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN) die("read");
  }

  if (c == '\x1b') {
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
            case '1': return KEY_HOME;
            case '3': return KEY_DEL;
            case '4': return KEY_END;
            case '5': return KEY_PAGE_UP;
            case '6': return KEY_PAGE_DOWN;
            case '7': return KEY_HOME;
            case '8': return KEY_END;
          }
        }
      } else {
        switch (seq[1]) {
          case 'A': return KEY_ARROW_UP;
          case 'B': return KEY_ARROW_DOWN;
          case 'C': return KEY_ARROW_RIGHT;
          case 'D': return KEY_ARROW_LEFT;
          case 'H': return KEY_HOME;
          case 'F': return KEY_END;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return KEY_HOME;
        case 'F': return KEY_END;
      }
    }
    return '\x1b';
  }

  return c;
}

static int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if (buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;
  return 0;
}

static int getWindowSize(int *rows, int *cols) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/* append buffer */

typedef struct {
  char *b;
  int len;
} ABuf;

#define ABUF_INIT {NULL, 0}

static void abAppend(ABuf *ab, const char *s, int len) {
  char *newb = xrealloc(ab->b, (size_t)ab->len + (size_t)len);
  memcpy(&newb[ab->len], s, (size_t)len);
  ab->b = newb;
  ab->len += len;
}

static void abFree(ABuf *ab) {
  free(ab->b);
}

/* syntax highlighting */

static int isSeparator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[]:;{}", c) != NULL;
}

static void editorUpdateSyntax(Row *row);

static int syntaxToColor(int hl) {
  switch (hl) {
    case HL_COMMENT:
    case HL_MLCOMMENT: return 36; // cyan
    case HL_KEYWORD1:  return 33; // yellow
    case HL_KEYWORD2:  return 32; // green
    case HL_STRING:    return 35; // magenta
    case HL_NUMBER:    return 31; // red
    case HL_MATCH:     return 34; // blue
    default:           return 37; // white
  }
}

static void editorSelectSyntax(void) {
  E.syntax = NULL;
  if (!E.filename) return;

  const char *ext = strrchr(E.filename, '.');

  for (int j = 0; j < HLDB_COUNT; j++) {
    Syntax *s = &HLDB[j];
    for (int i = 0; s->filematch[i]; i++) {
      bool is_ext = s->filematch[i][0] == '.';
      if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
          (!is_ext && strstr(E.filename, s->filematch[i]))) {
        E.syntax = s;
        for (int r = 0; r < E.numrows; r++) editorUpdateSyntax(&E.row[r]);
        return;
      }
    }
  }
}

static void editorUpdateSyntax(Row *row) {
  row->hl = xrealloc(row->hl, (size_t)row->rsize);
  memset(row->hl, HL_NORMAL, (size_t)row->rsize);

  if (!E.syntax) return;

  const char **keywords = E.syntax->keywords;

  const char *scs = E.syntax->scs;
  const char *mcs = E.syntax->mcs;
  const char *mce = E.syntax->mce;

  int scs_len = scs ? (int)strlen(scs) : 0;
  int mcs_len = mcs ? (int)strlen(mcs) : 0;
  int mce_len = mce ? (int)strlen(mce) : 0;

  int prev_sep = 1;
  int in_string = 0;
  int in_comment = (row->idx > 0 && E.row[row->idx - 1].hl_open_comment);

  int i = 0;
  while (i < row->rsize) {
    char c = row->render[i];
    unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

    // single-line comment
    if (scs_len && !in_string && !in_comment) {
      if (!strncmp(&row->render[i], scs, (size_t)scs_len)) {
        memset(&row->hl[i], HL_COMMENT, (size_t)(row->rsize - i));
        break;
      }
    }

    // multi-line comments
    if (mcs_len && mce_len && !in_string) {
      if (in_comment) {
        row->hl[i] = HL_MLCOMMENT;
        if (!strncmp(&row->render[i], mce, (size_t)mce_len)) {
          memset(&row->hl[i], HL_MLCOMMENT, (size_t)mce_len);
          i += mce_len;
          in_comment = 0;
          prev_sep = 1;
          continue;
        }
        i++;
        continue;
      } else if (!strncmp(&row->render[i], mcs, (size_t)mcs_len)) {
        memset(&row->hl[i], HL_MLCOMMENT, (size_t)mcs_len);
        i += mcs_len;
        in_comment = 1;
        continue;
      }
    }

    // strings
    if (E.syntax->flags & HL_STRINGS) {
      if (in_string) {
        row->hl[i] = HL_STRING;
        if (c == '\\' && i + 1 < row->rsize) {
          row->hl[i + 1] = HL_STRING;
          i += 2;
          continue;
        }
        if (c == in_string) in_string = 0;
        i++;
        prev_sep = 1;
        continue;
      } else {
        if (c == '"' || c == '\'') {
          in_string = c;
          row->hl[i] = HL_STRING;
          i++;
          continue;
        }
      }
    }

    // numbers (digits / decimals)
    if (E.syntax->flags & HL_NUMBERS) {
      if ((isdigit((unsigned char)c) && (prev_sep || prev_hl == HL_NUMBER)) ||
          (c == '.' && prev_hl == HL_NUMBER)) {
        row->hl[i] = HL_NUMBER;
        i++;
        prev_sep = 0;
        continue;
      }
    }

    // keywords
    if (prev_sep) {
      for (int k = 0; keywords[k]; k++) {
        int klen = (int)strlen(keywords[k]);
        int kw2 = keywords[k][klen - 1] == '|';
        if (kw2) klen--;

        if (!strncmp(&row->render[i], keywords[k], (size_t)klen) &&
            isSeparator(row->render[i + klen])) {
          memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, (size_t)klen);
          i += klen;
          prev_sep = 0;
          goto next_char;
        }
      }
    }

    prev_sep = isSeparator(c);
    i++;

  next_char:
    ;
  }

  int changed = (row->hl_open_comment != in_comment);
  row->hl_open_comment = in_comment;
  if (changed && row->idx + 1 < E.numrows) editorUpdateSyntax(&E.row[row->idx + 1]);
}

/* rows */

static int rowCxToRx(const Row *row, int cx) {
  int rx = 0;
  for (int j = 0; j < cx; j++) {
    if (row->chars[j] == '\t') rx += (TAB_STOP - 1) - (rx % TAB_STOP);
    rx++;
  }
  return rx;
}

static int rowRxToCx(const Row *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t') cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

static void editorUpdateRow(Row *row) {
  int tabs = 0;
  for (int j = 0; j < row->size; j++) if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = xmalloc((size_t)row->size + (size_t)tabs * (TAB_STOP - 1) + 1);

  int idx = 0;
  for (int j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;

  editorUpdateSyntax(row);
}

static void editorInsertRow(int at, const char *s, size_t len) {
  if (at < 0 || at > E.numrows) return;

  E.row = xrealloc(E.row, sizeof(Row) * (size_t)(E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(Row) * (size_t)(E.numrows - at));

  for (int j = at + 1; j <= E.numrows; j++) E.row[j].idx++;

  Row *row = &E.row[at];
  row->idx = at;
  row->size = (int)len;
  row->chars = xmalloc(len + 1);
  memcpy(row->chars, s, len);
  row->chars[len] = '\0';

  row->rsize = 0;
  row->render = NULL;
  row->hl = NULL;
  row->hl_open_comment = 0;

  editorUpdateRow(row);

  E.numrows++;
  E.dirty++;
}

static void editorFreeRow(Row *row) {
  free(row->chars);
  free(row->render);
  free(row->hl);
}

static void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows) return;

  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(Row) * (size_t)(E.numrows - at - 1));
  for (int j = at; j < E.numrows - 1; j++) E.row[j].idx--;

  E.numrows--;
  E.dirty++;
}

static void rowInsertChar(Row *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = xrealloc(row->chars, (size_t)row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], (size_t)(row->size - at + 1));
  row->size++;
  row->chars[at] = (char)c;
  editorUpdateRow(row);
  E.dirty++;
}

static void rowAppendString(Row *row, const char *s, size_t len) {
  row->chars = xrealloc(row->chars, (size_t)row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += (int)len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

static void rowDelChar(Row *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->size - at));
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/* editor operations */

static void editorInsertChar(int c) {
  if (E.cy == E.numrows) editorInsertRow(E.numrows, "", 0);
  rowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

static void editorInsertNewline(void) {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    Row *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], (size_t)(row->size - E.cx));
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

static void editorDelChar(void) {
  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;

  Row *row = &E.row[E.cy];
  if (E.cx > 0) {
    rowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    rowAppendString(&E.row[E.cy - 1], row->chars, (size_t)row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

/* file I/O */

static char *editorRowsToString(int *buflen) {
  int totlen = 0;
  for (int j = 0; j < E.numrows; j++) totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = xmalloc((size_t)totlen);
  char *p = buf;
  for (int j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, (size_t)E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

static void editorOpen(const char *filename) {
  free(E.filename);
  E.filename = xstrdup(filename);
  editorSelectSyntax();

  FILE *fp = fopen(filename, "r");
  if (!fp) die("fopen");

  char *line = NULL;
  size_t cap = 0;
  ssize_t len;
  while ((len = getline(&line, &cap, fp)) != -1) {
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    editorInsertRow(E.numrows, line, (size_t)len);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

static void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

static char *editorPrompt(const char *prompt, void (*callback)(char *q, int key));

static void editorSave(void) {
  if (!E.filename) {
    char *name = editorPrompt("Save as: %s (ESC to cancel)", NULL);
    if (!name) {
      editorSetStatusMessage("Save aborted");
      return;
    }
    E.filename = name;
    editorSelectSyntax();
  }

  int len = 0;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, (size_t)len) == len) {
        close(fd);
        free(buf);
        E.dirty = 0;
        editorSetStatusMessage("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }

  free(buf);
  editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

/* search */

static void editorFindCallback(char *query, int key) {
  static int last_match = -1;
  static int direction = 1;

  static int saved_hl_line = -1;
  static unsigned char *saved_hl = NULL;

  // restore saved highlight from previous match
  if (saved_hl) {
    memcpy(E.row[saved_hl_line].hl, saved_hl, (size_t)E.row[saved_hl_line].rsize);
    free(saved_hl);
    saved_hl = NULL;
    saved_hl_line = -1;
  }

  if (key == '\r' || key == '\x1b') {
    last_match = -1;
    direction = 1;
    return;
  } else if (key == KEY_ARROW_RIGHT || key == KEY_ARROW_DOWN) {
    direction = 1;
  } else if (key == KEY_ARROW_LEFT || key == KEY_ARROW_UP) {
    direction = -1;
  } else {
    last_match = -1;
    direction = 1;
  }

  if (last_match == -1) direction = 1;
  int current = last_match;

  for (int i = 0; i < E.numrows; i++) {
    current += direction;
    if (current == -1) current = E.numrows - 1;
    if (current == E.numrows) current = 0;

    Row *row = &E.row[current];
    char *match = strstr(row->render, query);
    if (match) {
      last_match = current;
      E.cy = current;
      E.cx = rowRxToCx(row, (int)(match - row->render));
      E.rowoff = E.numrows; // force scroll

      // save current highlight and paint match
      saved_hl_line = current;
      saved_hl = xmalloc((size_t)row->rsize);
      memcpy(saved_hl, row->hl, (size_t)row->rsize);

      memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
      break;
    }
  }
}

static void editorFind(void) {
  int saved_cx = E.cx, saved_cy = E.cy;
  int saved_coloff = E.coloff, saved_rowoff = E.rowoff;

  char *query = editorPrompt("Search: %s (ESC/Arrows/Enter)", editorFindCallback);
  if (query) {
    free(query);
  } else {
    // cursor position and restoration
    E.cx = saved_cx;
    E.cy = saved_cy;
    E.coloff = saved_coloff;
    E.rowoff = saved_rowoff;
  }
}

/* output */

static void editorScroll(void) {
  E.rx = 0;
  if (E.cy < E.numrows) E.rx = rowCxToRx(&E.row[E.cy], E.cx);

  if (E.cy < E.rowoff) E.rowoff = E.cy;
  if (E.cy >= E.rowoff + E.screenrows) E.rowoff = E.cy - E.screenrows + 1;

  if (E.rx < E.coloff) E.coloff = E.rx;
  if (E.rx >= E.coloff + E.screencols) E.coloff = E.rx - E.screencols + 1;
}

static void editorDrawRows(ABuf *ab) {
  for (int y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                 "Kilo -- v%s", KILO_VERSION);
        if (welcomelen > E.screencols) welcomelen = E.screencols;

        int padding = (E.screencols - welcomelen) / 2;
        if (padding) {
          abAppend(ab, "~", 1);
          padding--;
        }
        while (padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      } else {
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;

      char *c = &E.row[filerow].render[E.coloff];
      unsigned char *hl = &E.row[filerow].hl[E.coloff];

      int current_color = -1;
      for (int j = 0; j < len; j++) {
        if (iscntrl((unsigned char)c[j])) {
          char sym = (c[j] <= 26) ? '@' + c[j] : '?';
          abAppend(ab, "\x1b[7m", 4);
          abAppend(ab, &sym, 1);
          abAppend(ab, "\x1b[m", 3);
          if (current_color != -1) {
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
            abAppend(ab, buf, clen);
          }
        } else if (hl[j] == HL_NORMAL) {
          if (current_color != -1) {
            abAppend(ab, "\x1b[39m", 5);
            current_color = -1;
          }
          abAppend(ab, &c[j], 1);
        } else {
          int color = syntaxToColor(hl[j]);
          if (color != current_color) {
            current_color = color;
            char buf[16];
            int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
            abAppend(ab, buf, clen);
          }
          abAppend(ab, &c[j], 1);
        }
      }
      abAppend(ab, "\x1b[39m", 5);
    }

    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2);
  }
}

static void editorDrawStatusBar(ABuf *ab) {
  abAppend(ab, "\x1b[7m", 4);

  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]",
                     E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d",
                      E.syntax ? E.syntax->filetype : "no ft",
                      E.cy + 1, E.numrows);

  if (len > E.screencols) len = E.screencols;
  abAppend(ab, status, len);

  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      abAppend(ab, rstatus, rlen);
      break;
    } else {
      abAppend(ab, " ", 1);
      len++;
    }
  }

  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

static void editorDrawMessageBar(ABuf *ab) {
  abAppend(ab, "\x1b[K", 3);
  int msglen = (int)strlen(E.statusmsg);
  if (msglen > E.screencols) msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

static void editorRefreshScreen(void) {
  editorScroll();

  ABuf ab = ABUF_INIT;

  abAppend(&ab, "\x1b[?25l", 6); // hide cursor
  abAppend(&ab, "\x1b[H", 3);    // cursor to top-left

  editorDrawRows(&ab);
  editorDrawStatusBar(&ab);
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH",
           (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1);
  abAppend(&ab, buf, (int)strlen(buf));

  abAppend(&ab, "\x1b[?25h", 6); // show cursor

  write(STDOUT_FILENO, ab.b, (size_t)ab.len);
  abFree(&ab);
}

/* input */

static char *editorPrompt(const char *prompt, void (*callback)(char *q, int key)) {
  size_t bufsize = 128;
  char *buf = xmalloc(bufsize);
  size_t buflen = 0;
  buf[0] = '\0';

  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();

    int c = editorReadKey();

    if (c == KEY_DEL || c == CTRL_KEY('h') || c == KEY_BACKSPACE) {
      if (buflen) buf[--buflen] = '\0';
    } else if (c == '\x1b') {
      editorSetStatusMessage("");
      if (callback) callback(buf, c);
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen) {
        editorSetStatusMessage("");
        if (callback) callback(buf, c);
        return buf;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = xrealloc(buf, bufsize);
      }
      buf[buflen++] = (char)c;
      buf[buflen] = '\0';
    }

    if (callback) callback(buf, c);
  }
}

static void editorMoveCursor(int key) {
  Row *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case KEY_ARROW_LEFT:
      if (E.cx != 0) E.cx--;
      else if (E.cy > 0) { E.cy--; E.cx = E.row[E.cy].size; }
      break;
    case KEY_ARROW_RIGHT:
      if (row && E.cx < row->size) E.cx++;
      else if (row && E.cx == row->size) { E.cy++; E.cx = 0; }
      break;
    case KEY_ARROW_UP:
      if (E.cy != 0) E.cy--;
      break;
    case KEY_ARROW_DOWN:
      if (E.cy < E.numrows) E.cy++;
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) E.cx = rowlen;
}

static void editorProcessKeypress(void) {
  static int quit_times = QUIT_TIMES;
  int c = editorReadKey();

  switch (c) {
    case '\r':
      editorInsertNewline();
      break;

    case CTRL_KEY('q'):
      if (E.dirty && quit_times > 0) {
        editorSetStatusMessage("WARNING: Unsaved changes. Press Ctrl-Q %d more times to quit.",
                               quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1b[2J", 4);
      write(STDOUT_FILENO, "\x1b[H", 3);
      exit(0);

    case CTRL_KEY('s'):
      editorSave();
      break;

    case CTRL_KEY('f'):
      editorFind();
      break;

    case KEY_HOME:
      E.cx = 0;
      break;

    case KEY_END:
      if (E.cy < E.numrows) E.cx = E.row[E.cy].size;
      break;

    case KEY_BACKSPACE:
    case CTRL_KEY('h'):
    case KEY_DEL:
      if (c == KEY_DEL) editorMoveCursor(KEY_ARROW_RIGHT);
      editorDelChar();
      break;

    case KEY_PAGE_UP:
    case KEY_PAGE_DOWN: {
      if (c == KEY_PAGE_UP) E.cy = E.rowoff;
      else {
        E.cy = E.rowoff + E.screenrows - 1;
        if (E.cy > E.numrows) E.cy = E.numrows;
      }
      int times = E.screenrows;
      while (times--) editorMoveCursor(c == KEY_PAGE_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN);
    } break;

    case KEY_ARROW_UP:
    case KEY_ARROW_DOWN:
    case KEY_ARROW_LEFT:
    case KEY_ARROW_RIGHT:
      editorMoveCursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1b':
      break;

    default:
      if (!iscntrl(c) && c < 128) editorInsertChar(c);
      break;
  }

  quit_times = QUIT_TIMES;
}

/* init */

static void initEditor(void) {
  E.cx = E.cy = 0;
  E.rx = 0;
  E.rowoff = E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;
  E.syntax = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2; // status + message bars
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();

  if (argc >= 2) editorOpen(argv[1]);

  editorSetStatusMessage("HELP: Ctrl-S save | Ctrl-Q quit | Ctrl-F find");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
  return 0;
}

