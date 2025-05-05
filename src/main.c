#include "main.h"
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

editorConfig E;

void *editorMalloc(size_t size) {
  void *ptr = malloc(size);
  if (!ptr)
    die("Memory allocation failed");
  return ptr;
}

void editorFree(void *ptr) {
  if (ptr)
    free(ptr);
}

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode(void) {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(void) {
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
    die("tcgetattr");
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey(void) {
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  if (c == '\x1b') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

void initColors(void) {
  E.colors[COLOR_BACKGROUND] = 16;
  E.colors[COLOR_FOREGROUND] = 250;
  E.colors[COLOR_SELECTION] = 59;
  E.colors[COLOR_COMMENT] = 102;
  E.colors[COLOR_KEYWORD] = 175;
  E.colors[COLOR_NUMBER] = 175;
  E.colors[COLOR_STRING] = 108;
  E.colors[COLOR_STATUS_BG] = 238;
  E.colors[COLOR_STATUS_FG] = 250;
  E.colors[COLOR_LINENUMBER] = 242;
  E.colors[COLOR_CURSOR] = 250;
}

void setColor(int color) {
  char buf[16];
  int len;
  len = snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", E.colors[color]);
  write(STDOUT_FILENO, buf, len);
}

void resetColor(void) { write(STDOUT_FILENO, "\x1b[39m", 5); }

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int j;
  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t')
      tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs * (TAB_SIZE - 1) + 1);

  int idx = 0;
  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % TAB_SIZE != 0)
        row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {
  if (at < 0 || at > E.numrows)
    return;

  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);

  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}

void editorDelRow(int at) {
  if (at < 0 || at >= E.numrows)
    return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

char *editorRowsToString(int *buflen) {
  int totlen = 0;
  int j;
  for (j = 0; j < E.numrows; j++)
    totlen += E.row[j].size + 1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j = 0; j < E.numrows; j++) {
    memcpy(p, E.row[j].chars, E.row[j].size);
    p += E.row[j].size;
    *p = '\n';
    p++;
  }

  return buf;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size)
    return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (TAB_SIZE - 1) - (cur_rx % TAB_SIZE);
    cur_rx++;

    if (cur_rx > rx)
      return cx;
  }
  return cx;
}

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

void editorInsertNewline(void) {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar(void) {
  if (E.cy == E.numrows)
    return;
  if (E.cx == 0 && E.cy == 0)
    return;

  erow *row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }
}

void editorCopy(int start_y, int start_x, int end_y, int end_x) {
  if ((start_y > end_y) || (start_y == end_y && start_x > end_x)) {
    int temp_y = start_y;
    int temp_x = start_x;
    start_y = end_y;
    start_x = end_x;
    end_y = temp_y;
    end_x = temp_x;
  }

  E.clipboardLength = 0;

  for (int y = start_y; y <= end_y; y++) {
    if (y >= E.numrows)
      break;

    erow *row = &E.row[y];
    int copy_start = (y == start_y) ? start_x : 0;
    int copy_end = (y == end_y) ? end_x : row->size;

    if (copy_start > row->size)
      copy_start = row->size;
    if (copy_end > row->size)
      copy_end = row->size;

    int len = copy_end - copy_start;
    if (len <= 0)
      continue;

    if (E.clipboardLength + len + 1 >= MAX_CLIPBOARD_SIZE)
      break;

    memcpy(E.clipboard + E.clipboardLength, &row->chars[copy_start], len);
    E.clipboardLength += len;

    if (y < end_y && E.clipboardLength < MAX_CLIPBOARD_SIZE - 1) {
      E.clipboard[E.clipboardLength++] = '\n';
    }
  }

  E.clipboard[E.clipboardLength] = '\0';
  editorSetStatusMessage("Copied %d bytes to clipboard", E.clipboardLength);
}

void editorCopyLine(void) {
  if (E.cy >= E.numrows)
    return;

  erow *row = &E.row[E.cy];
  memcpy(E.clipboard, row->chars, row->size);
  E.clipboardLength = row->size;
  E.clipboard[E.clipboardLength] = '\0';

  editorSetStatusMessage("Copied line to clipboard");
}

void editorPaste(void) {
  if (E.clipboardLength == 0)
    return;

  char *lines = E.clipboard;
  int remaining = E.clipboardLength;

  while (remaining > 0) {
    char *newline = memchr(lines, '\n', remaining);
    int len = newline ? newline - lines : remaining;

    for (int i = 0; i < len; i++) {
      editorInsertChar(lines[i]);
    }

    if (newline) {
      editorInsertNewline();
      lines = newline + 1;
      remaining -= (len + 1);
    } else {
      break;
    }
  }

  editorSetStatusMessage("Pasted %d bytes from clipboard", E.clipboardLength);
}

void editorAddToUndo(enum operationType type, int cx, int cy, const char *data,
                     int dataLen) {
  if (E.undoIndex < E.undoStackSize) {
    E.undoStackSize = E.undoIndex;
  }

  if (E.undoStackSize < MAX_UNDO_OPERATIONS) {
    editorOperation *op = &E.undoStack[E.undoStackSize++];
    op->type = type;
    op->cx = cx;
    op->cy = cy;
    op->dataLen = dataLen < MAX_CLIPBOARD_SIZE ? dataLen : MAX_CLIPBOARD_SIZE;
    memcpy(op->data, data, op->dataLen);
  } else {
    memmove(&E.undoStack[0], &E.undoStack[1],
            sizeof(editorOperation) * (MAX_UNDO_OPERATIONS - 1));
    editorOperation *op = &E.undoStack[MAX_UNDO_OPERATIONS - 1];
    op->type = type;
    op->cx = cx;
    op->cy = cy;
    op->dataLen = dataLen < MAX_CLIPBOARD_SIZE ? dataLen : MAX_CLIPBOARD_SIZE;
    memcpy(op->data, data, op->dataLen);
  }

  E.undoIndex = E.undoStackSize;
}

void editorUndo(void) {
  if (E.undoIndex <= 0) {
    editorSetStatusMessage("Nothing to undo");
    return;
  }

  editorOperation *op = &E.undoStack[--E.undoIndex];

  switch (op->type) {
  case OP_INSERT_CHAR:
    E.cx = op->cx;
    E.cy = op->cy;
    editorDelChar();
    break;

  case OP_DELETE_CHAR:
    E.cx = op->cx;
    E.cy = op->cy;
    for (int i = 0; i < op->dataLen; i++) {
      editorInsertChar(op->data[i]);
    }
    E.cx = op->cx;
    break;

  case OP_INSERT_LINE:
    E.cy = op->cy;
    editorDelRow(E.cy);
    E.cx = 0;
    break;

  case OP_DELETE_LINE:
    editorInsertRow(op->cy, op->data, op->dataLen);
    E.cy = op->cy;
    E.cx = 0;
    break;
  }

  editorSetStatusMessage("Undo successful");
}

void editorRedo(void) {
  if (E.undoIndex >= E.undoStackSize) {
    editorSetStatusMessage("Nothing to redo");
    return;
  }

  editorOperation *op = &E.undoStack[E.undoIndex++];

  switch (op->type) {
  case OP_INSERT_CHAR:
    E.cx = op->cx;
    E.cy = op->cy;
    for (int i = 0; i < op->dataLen; i++) {
      editorInsertChar(op->data[i]);
    }
    break;

  case OP_DELETE_CHAR:
    E.cx = op->cx;
    E.cy = op->cy;
    editorDelChar();
    break;

  case OP_INSERT_LINE:
    E.cy = op->cy;
    editorInsertNewline();
    break;

  case OP_DELETE_LINE:
    E.cy = op->cy;
    editorDelRow(E.cy);
    break;
  }

  editorSetStatusMessage("Redo successful");
}

void editorOpen(char *filename) {
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if (!fp)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave(void) {
  if (E.filename == NULL) {
    E.filename = malloc(128);
    E.filename[0] = '\0';
    editorSetStatusMessage("Save as: %s (ESC to cancel)", E.filename);
    return;
  }

  int len;
  char *buf = editorRowsToString(&len);

  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
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

void editorReload(void) {
  if (E.filename == NULL) {
    editorSetStatusMessage("No file to reload");
    return;
  }

  for (int i = 0; i < E.numrows; i++) {
    editorFreeRow(&E.row[i]);
  }
  free(E.row);
  E.row = NULL;
  E.numrows = 0;

  FILE *fp = fopen(E.filename, "r");
  if (!fp) {
    die("fopen");
  }

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  while ((linelen = getline(&line, &linecap, fp)) != -1) {
    while (linelen > 0 &&
           (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
      linelen--;
    editorInsertRow(E.numrows, line, linelen);
  }

  free(line);
  fclose(fp);
  E.dirty = 0;
  editorSetStatusMessage("File reloaded successfully");
}

void editorFind(void) {
  editorSetStatusMessage("Search functionality not implemented");
}

void editorFileBrowserUpdate(void) {
  for (int i = 0; i < E.fb.numEntries; i++) {
    free(E.fb.entries[i].name);
  }
  free(E.fb.entries);
  E.fb.entries = NULL;
  E.fb.numEntries = 0;

  DIR *dir = opendir(E.fb.currentDir);
  if (!dir) {
    editorSetStatusMessage("Cannot open directory: %s", E.fb.currentDir);
    return;
  }

  struct dirent *entry;
  int count = 0;
  while ((entry = readdir(dir)) != NULL) {
    count++;
  }
  rewinddir(dir);

  E.fb.entries = malloc(count * sizeof(dirEntry));

  while ((entry = readdir(dir)) != NULL) {
    dirEntry *de = &E.fb.entries[E.fb.numEntries++];
    de->name = strdup(entry->d_name);
    de->isDir = entry->d_type == DT_DIR;
  }

  closedir(dir);
  editorSetStatusMessage("File browser updated: %s", E.fb.currentDir);
}

void editorFileBrowserToggle(void) {
  E.fb.visible = !E.fb.visible;

  if (E.fb.visible) {
    if (E.fb.currentDir[0] == '\0') {
      getcwd(E.fb.currentDir, sizeof(E.fb.currentDir));
    }
    editorFileBrowserUpdate();
  }
}

void editorFileBrowserDraw(void) {
  if (!E.fb.visible)
    return;

  int width = E.screencols / 4;
  if (width < 20)
    width = 20;
  if (width > 40)
    width = 40;

  char buf[32];
  int rows = E.screenrows - 2;

  for (int y = 0; y < rows; y++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, 1);
    write(STDOUT_FILENO, buf, strlen(buf));

    setColor(COLOR_BACKGROUND);

    for (int i = 0; i < width; i++) {
      write(STDOUT_FILENO, " ", 1);
    }
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", 1, 1);
  write(STDOUT_FILENO, buf, strlen(buf));
  setColor(COLOR_STATUS_BG);

  char title[41] = " File Browser ";
  int titleLen = strlen(title);
  write(STDOUT_FILENO, title, titleLen);
  for (int i = titleLen; i < width; i++) {
    write(STDOUT_FILENO, " ", 1);
  }

  int visible_rows = rows - 2;
  int start = E.fb.scroll;
  int end = start + visible_rows;
  if (end > E.fb.numEntries)
    end = E.fb.numEntries;

  for (int i = start, y = 2; i < end; i++, y++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y, 1);
    write(STDOUT_FILENO, buf, strlen(buf));

    if (i == E.fb.selected) {
      setColor(COLOR_SELECTION);
    } else {
      setColor(COLOR_FOREGROUND);
    }

    dirEntry *entry = &E.fb.entries[i];
    char line[41];
    if (entry->isDir) {
      snprintf(line, sizeof(line), " [%s]", entry->name);
    } else {
      snprintf(line, sizeof(line), " %s", entry->name);
    }

    int len = strlen(line);
    if (len > width - 1) {
      line[width - 4] = '.';
      line[width - 3] = '.';
      line[width - 2] = '.';
      line[width - 1] = '\0';
      len = width - 1;
    }

    write(STDOUT_FILENO, line, len);

    for (int j = len; j < width; j++) {
      write(STDOUT_FILENO, " ", 1);
    }
  }

  resetColor();
}

void editorFileBrowserProcessKey(int key) {
  switch (key) {
  case ARROW_UP:
    if (E.fb.selected > 0) {
      E.fb.selected--;
      if (E.fb.selected < E.fb.scroll) {
        E.fb.scroll = E.fb.selected;
      }
    }
    break;

  case ARROW_DOWN:
    if (E.fb.selected < E.fb.numEntries - 1) {
      E.fb.selected++;
      if (E.fb.selected >= E.fb.scroll + E.screenrows - 4) {
        E.fb.scroll++;
      }
    }
    break;

  case '\r':
    if (E.fb.selected >= 0 && E.fb.selected < E.fb.numEntries) {
      dirEntry *entry = &E.fb.entries[E.fb.selected];

      if (entry->isDir) {
        char newPath[1024];
        if (strcmp(entry->name, "..") == 0) {
          char *lastSlash = strrchr(E.fb.currentDir, '/');
          if (lastSlash != NULL && lastSlash != E.fb.currentDir) {
            *lastSlash = '\0';
          } else {
            strcpy(E.fb.currentDir, "/");
          }
        } else if (strcmp(entry->name, ".") == 0) {
        } else {
          if (E.fb.currentDir[strlen(E.fb.currentDir) - 1] != '/') {
            snprintf(newPath, sizeof(newPath), "%s/%s", E.fb.currentDir,
                     entry->name);
          } else {
            snprintf(newPath, sizeof(newPath), "%s%s", E.fb.currentDir,
                     entry->name);
          }
          strcpy(E.fb.currentDir, newPath);
        }

        editorFileBrowserUpdate();
        E.fb.selected = 0;
        E.fb.scroll = 0;
      } else {
        char filePath[1024];
        if (E.fb.currentDir[strlen(E.fb.currentDir) - 1] != '/') {
          snprintf(filePath, sizeof(filePath), "%s/%s", E.fb.currentDir,
                   entry->name);
        } else {
          snprintf(filePath, sizeof(filePath), "%s%s", E.fb.currentDir,
                   entry->name);
        }

        if (E.dirty) {
          editorSetStatusMessage(
              "WARNING!!! File has unsaved changes. Save first!");
        } else {
          editorOpen(filePath);
          editorFileBrowserToggle();
        }
      }
    }
    break;

  case CTRL_KEY('h'):
    strcpy(E.fb.currentDir, getenv("HOME"));
    editorFileBrowserUpdate();
    E.fb.selected = 0;
    E.fb.scroll = 0;
    break;
  }
}

void editorTerminalToggle(void) {
  E.term.visible = !E.term.visible;

  if (E.term.visible && E.term.buffer == NULL) {
    E.term.size = E.screencols * (E.screenrows / 2);
    E.term.buffer = malloc(E.term.size);
    memset(E.term.buffer, 0, E.term.size);
  }
}

void editorTerminalExecute(const char *cmd) {
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    editorSetStatusMessage("Failed to create pipe");
    return;
  }

  pid_t pid = fork();
  if (pid < 0) {
    editorSetStatusMessage("Fork failed");
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  } else if (pid == 0) {
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);
    execl("/bin/sh", "sh", "-c", cmd, NULL);
    exit(1);
  } else {
    close(pipefd[1]);
    ssize_t n;
    char buffer[1024];
    size_t total = 0;

    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
      if (total + n >= E.term.size) {
        memmove(E.term.buffer, E.term.buffer + n, E.term.size - n);
        memcpy(E.term.buffer + (E.term.size - n), buffer, n);
      } else {
        memcpy(E.term.buffer + total, buffer, n);
        total += n;
      }
    }

    if (total < E.term.size) {
      E.term.buffer[total] = '\0';
    } else {
      E.term.buffer[E.term.size - 1] = '\0';
    }

    close(pipefd[0]);
    int status;
    waitpid(pid, &status, 0);
    editorSetStatusMessage("Command executed: %s", cmd);
  }
}

void editorTerminalDraw(void) {
  if (!E.term.visible || E.term.buffer == NULL)
    return;

  int height = E.screenrows / 2;
  int startRow = E.screenrows - height;

  char buf[32];

  for (int y = 0; y < height; y++) {
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", startRow + y + 1, 1);
    write(STDOUT_FILENO, buf, strlen(buf));

    setColor(COLOR_BACKGROUND);

    for (int i = 0; i < E.screencols; i++) {
      write(STDOUT_FILENO, " ", 1);
    }
  }

  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", startRow + 1, 1);
  write(STDOUT_FILENO, buf, strlen(buf));
  setColor(COLOR_STATUS_BG);

  char title[80] = " Terminal ";
  int titleLen = strlen(title);
  write(STDOUT_FILENO, title, titleLen);
  for (int i = titleLen; i < E.screencols; i++) {
    write(STDOUT_FILENO, " ", 1);
  }

  setColor(COLOR_FOREGROUND);

  int bufferLen = strlen(E.term.buffer);

  int linesNeeded = height - 2;
  int lineCount = 0;
  int startPos = bufferLen;

  for (int i = bufferLen - 1; i >= 0 && lineCount < linesNeeded; i--) {
    if (E.term.buffer[i] == '\n') {
      lineCount++;
      if (lineCount >= linesNeeded) {
        startPos = i + 1;
        break;
      }
    }
  }

  if (lineCount < linesNeeded)
    startPos = 0;

  int currentLine = 0;
  int row = startRow + 2;

  for (int i = startPos; i < bufferLen && row < E.screenrows; i++) {
    if (E.term.buffer[i] == '\n') {
      row++;
      currentLine = 0;
    } else {
      if (currentLine == 0) {
        snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, currentLine + 1);
        write(STDOUT_FILENO, buf, strlen(buf));
      }

      write(STDOUT_FILENO, &E.term.buffer[i], 1);
      currentLine++;

      if (currentLine >= E.screencols) {
        currentLine = 0;
        row++;
      }
    }
  }

  resetColor();
}

void editorTerminalProcessKey(int key) {
  if (key == CTRL_KEY('t')) {
    editorTerminalToggle();
  }
}

void editorScroll(void) {
  E.rx = E.cx;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
  }
  if (E.rx >= E.coloff + E.screencols) {
    E.coloff = E.rx - E.screencols + 1;
  }
}

void editorDrawRows(void) {
  int y;
  int lineNumberWidth = E.showLineNumbers ? 4 : 0;

  for (y = 0; y < E.screenrows; y++) {
    int filerow = y + E.rowoff;

    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows / 3) {
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
                                  "CTextEdit -- version %s", CTEXTEDIT_VERSION);
        if (welcomelen > E.screencols - lineNumberWidth)
          welcomelen = E.screencols - lineNumberWidth;

        int padding = (E.screencols - lineNumberWidth - welcomelen) / 2;

        if (E.showLineNumbers) {
          setColor(COLOR_LINENUMBER);
          write(STDOUT_FILENO, "    ", 4);
        }

        if (padding) {
          write(STDOUT_FILENO, "~", 1);
          padding--;
        }

        setColor(COLOR_FOREGROUND);
        while (padding--)
          write(STDOUT_FILENO, " ", 1);

        write(STDOUT_FILENO, welcome, welcomelen);
      } else {
        if (E.showLineNumbers) {
          setColor(COLOR_LINENUMBER);
          write(STDOUT_FILENO, "    ", 4);
        }

        setColor(COLOR_FOREGROUND);
        write(STDOUT_FILENO, "~", 1);
      }
    } else {
      if (E.showLineNumbers) {
        char lineNumBuf[5];
        int lineNumLen =
            snprintf(lineNumBuf, sizeof(lineNumBuf), "%3d ", filerow + 1);
        setColor(COLOR_LINENUMBER);
        write(STDOUT_FILENO, lineNumBuf, lineNumLen);
      }

      setColor(COLOR_FOREGROUND);
      int len = E.row[filerow].rsize - E.coloff;
      if (len < 0)
        len = 0;
      if (len > E.screencols - lineNumberWidth)
        len = E.screencols - lineNumberWidth;

      if (len > 0) {
        write(STDOUT_FILENO, &E.row[filerow].render[E.coloff], len);
      }
    }

    write(STDOUT_FILENO, "\x1b[K", 3);
    write(STDOUT_FILENO, "\r\n", 2);
  }
}

void editorDrawStatusBar(void) {
  write(STDOUT_FILENO, "\x1b[7m", 4);
  char status[80], rstatus[80];
  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     E.filename ? E.filename : "[No Name]", E.numrows,
                     E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy + 1, E.numrows);
  if (len > E.screencols)
    len = E.screencols;
  write(STDOUT_FILENO, status, len);
  while (len < E.screencols) {
    if (E.screencols - len == rlen) {
      write(STDOUT_FILENO, rstatus, rlen);
      break;
    } else {
      write(STDOUT_FILENO, " ", 1);
      len++;
    }
  }
  write(STDOUT_FILENO, "\x1b[m", 3);
  write(STDOUT_FILENO, "\r\n", 2);
}

void editorDrawMessageBar(void) {
  write(STDOUT_FILENO, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if (msglen > E.screencols)
    msglen = E.screencols;
  if (msglen && time(NULL) - E.statusmsg_time < 5)
    write(STDOUT_FILENO, E.statusmsg, msglen);
}

void editorRefreshScreen(void) {
  editorScroll();

  write(STDOUT_FILENO, "\x1b[?25l", 6);
  write(STDOUT_FILENO, "\x1b[H", 3);

  setColor(COLOR_BACKGROUND);

  editorDrawRows();
  editorDrawStatusBar();
  editorDrawMessageBar();

  editorFileBrowserDraw();
  editorTerminalDraw();

  char buf[32];
  int lineNumberWidth = E.showLineNumbers ? 4 : 0;
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
           (E.rx - E.coloff) + 1 + lineNumberWidth);
  write(STDOUT_FILENO, buf, strlen(buf));

  resetColor();
  write(STDOUT_FILENO, "\x1b[?25h", 6);
}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    } else if (E.cy > 0) {
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
    if (row && E.cx < row->size) {
      E.cx++;
    } else if (row && E.cx == row->size) {
      E.cy++;
      E.cx = 0;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy < E.numrows) {
      E.cy++;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }
}

void editorProcessKeypress(void) {
  static int quit_times = QUIT_TIMES;

  if (E.fb.visible) {
    int c = editorReadKey();
    if (c == CTRL_KEY('b')) {
      editorFileBrowserToggle();
    } else if (c == CTRL_KEY('q')) {
      editorFileBrowserToggle();
    } else {
      editorFileBrowserProcessKey(c);
    }
    return;
  }

  if (E.term.visible) {
    int c = editorReadKey();
    if (c == CTRL_KEY('t')) {
      editorTerminalToggle();
    } else if (c == CTRL_KEY('q')) {
      editorTerminalToggle();
    } else {
      editorTerminalProcessKey(c);
    }
    return;
  }

  int c = editorReadKey();

  switch (c) {
  case '\r':
    editorInsertNewline();
    break;

  case CTRL_KEY('q'):
    if (E.dirty && quit_times > 0) {
      editorSetStatusMessage("WARNING!!! File has unsaved changes. "
                             "Press Ctrl-Q %d more times to quit.",
                             quit_times);
      quit_times--;
      return;
    }
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    exit(0);
    break;

  case CTRL_KEY('s'):
    editorSave();
    break;

  case CTRL_KEY('r'):
    editorReload();
    break;

  case CTRL_KEY('b'):
    editorFileBrowserToggle();
    break;

  case CTRL_KEY('t'):
    editorTerminalToggle();
    break;

  case CTRL_KEY('f'):
    editorFind();
    break;

  case CTRL_KEY('n'):
    E.showLineNumbers = !E.showLineNumbers;
    editorSetStatusMessage("Line numbers %s",
                           E.showLineNumbers ? "enabled" : "disabled");
    break;

  case CTRL_KEY('z'):
    editorUndo();
    break;

  case CTRL_KEY('y'):
    editorRedo();
    break;

  case CTRL_KEY('c'):
    editorCopyLine();
    break;

  case CTRL_KEY('v'):
    editorPaste();
    break;

  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if (E.cy < E.numrows)
      E.cx = E.row[E.cy].size;
    break;

  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY: {
    char c_char = E.cy < E.numrows && E.cx < E.row[E.cy].size
                      ? E.row[E.cy].chars[E.cx]
                      : '\0';

    editorAddToUndo(OP_DELETE_CHAR, E.cx, E.cy, &c_char, 1);

    if (c == DEL_KEY)
      editorMoveCursor(ARROW_RIGHT);
    editorDelChar();
  } break;

  case PAGE_UP:
  case PAGE_DOWN: {
    if (c == PAGE_UP) {
      E.cy = E.rowoff;
    } else if (c == PAGE_DOWN) {
      E.cy = E.rowoff + E.screenrows - 1;
      if (E.cy > E.numrows)
        E.cy = E.numrows;
    }

    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default: {
    char c_char = c;
    editorAddToUndo(OP_INSERT_CHAR, E.cx, E.cy, &c_char, 1);

    editorInsertChar(c);
  } break;
  }

  quit_times = QUIT_TIMES;
}

void initEditor(void) {
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  E.showLineNumbers = 1;
  E.clipboardLength = 0;
  E.clipboard[0] = '\0';
  E.undoStackSize = 0;
  E.undoIndex = 0;

  E.fb.currentDir[0] = '\0';
  E.fb.entries = NULL;
  E.fb.numEntries = 0;
  E.fb.scroll = 0;
  E.fb.selected = 0;
  E.fb.visible = 0;

  E.term.buffer = NULL;
  E.term.size = 0;
  E.term.visible = 0;

  initColors();

  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  if (argc >= 2) {
    editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }

  return 0;
}
