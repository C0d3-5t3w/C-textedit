#ifndef MAIN_H
#define MAIN_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CTEXTEDIT_VERSION "0.1.0"
#define TAB_SIZE 4
#define QUIT_TIMES 3
#define MAX_CLIPBOARD_SIZE 1024
#define MAX_UNDO_OPERATIONS 100
#define MAX_TABS 16
#define MAX_HELP_ENTRIES 32
#define MAX_FILETYPES 16

#define CTRL_KEY(k) ((k)&0x1F)

enum keys {
  BACKSPACE = 127,
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  PAGE_UP,
  PAGE_DOWN,
  HOME_KEY,
  END_KEY,
  DEL_KEY,
  ESC_KEY = '\x1b'
};

enum editorColors {
  COLOR_DEFAULT = 0,
  COLOR_BACKGROUND,
  COLOR_FOREGROUND,
  COLOR_SELECTION,
  COLOR_COMMENT,
  COLOR_KEYWORD,
  COLOR_TYPE,
  COLOR_NUMBER,
  COLOR_STRING,
  COLOR_FUNCTION,
  COLOR_OPERATOR,
  COLOR_VARIABLE,
  COLOR_PREPROCESSOR,
  COLOR_STATUS_BG,
  COLOR_STATUS_FG,
  COLOR_LINENUMBER,
  COLOR_CURSOR,
  COLOR_HIGHLIGHT_BG,
  COLOR_ERROR
};

enum operationType {
  OP_INSERT_CHAR,
  OP_DELETE_CHAR,
  OP_INSERT_LINE,
  OP_DELETE_LINE,
};

enum languageType {
  LANG_PLAINTEXT = 0,
  LANG_C,
  LANG_GO,
  LANG_RUST,
  LANG_ZIG,
  LANG_HTML,
  LANG_CSS,
  LANG_SASS,
  LANG_JAVASCRIPT,
  LANG_TYPESCRIPT,
  LANG_LUA,
  LANG_PYTHON,
  LANG_JSON,
  LANG_YAML,
  LANG_CSHARP,
  LANG_JAVA,
  LANG_BASH
};

enum tokenType {
  TOKEN_NORMAL,
  TOKEN_COMMENT,
  TOKEN_KEYWORD,
  TOKEN_TYPE,
  TOKEN_STRING,
  TOKEN_NUMBER,
  TOKEN_FUNCTION,
  TOKEN_OPERATOR,
  TOKEN_VARIABLE,
  TOKEN_PREPROCESSOR
};

typedef struct token {
  enum tokenType type;
  int start;
  int length;
} token;

typedef struct languageDef {
  char *name;
  char **extensions;
  int numExtensions;
  char **keywords;
  int numKeywords;
  char **types;
  int numTypes;
  char *singleLineComment;
  char *multiLineCommentStart;
  char *multiLineCommentEnd;
  char *stringDelimiters;
  char *preprocessorStart;
} languageDef;

typedef struct helpEntry {
  char *key;
  char *description;
} helpEntry;

typedef struct editorOperation {
  enum operationType type;
  int cx, cy;
  char data[MAX_CLIPBOARD_SIZE];
  int dataLen;
} editorOperation;

typedef struct editorBuffer {
  char *filename;
  int cx, cy;
  int rx;
  int rowoff, coloff;
  int numrows;
  struct erow *row;
  int dirty;
  enum languageType language;
  int active;
} editorBuffer;

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
  token *tokens;
  int numTokens;
  int hasMultilineComment;
} erow;

typedef struct dirEntry {
  char *name;
  int isDir;
} dirEntry;

typedef struct fileBrowser {
  char currentDir[1024];
  dirEntry *entries;
  int numEntries;
  int scroll;
  int selected;
  int visible;
} fileBrowser;

typedef struct terminal {
  char *buffer;
  int size;
  int visible;
} terminal;

typedef struct helpWindow {
  int visible;
  int scroll;
  helpEntry entries[MAX_HELP_ENTRIES];
  int numEntries;
} helpWindow;

typedef struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  struct termios orig_termios;

  int colors[32];

  int showLineNumbers;

  char clipboard[MAX_CLIPBOARD_SIZE];
  int clipboardLength;

  editorOperation undoStack[MAX_UNDO_OPERATIONS];
  int undoStackSize;
  int undoIndex;

  enum languageType currentLanguage;
  languageDef *languages[MAX_FILETYPES];

  editorBuffer tabs[MAX_TABS];
  int numTabs;
  int currentTab;

  fileBrowser fb;

  terminal term;

  helpWindow help;
} editorConfig;

extern editorConfig E;

void enableRawMode(void);
void disableRawMode(void);
void die(const char *s);
int editorReadKey(void);
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

void *editorMalloc(size_t size);
void editorFree(void *ptr);

void initColors(void);
void setColor(int color);
void resetColor(void);

void editorUpdateRow(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
char *editorRowsToString(int *buflen);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowDelChar(erow *row, int at);
void editorRowAppendString(erow *row, char *s, size_t len);
int editorRowCxToRx(erow *row, int cx);
int editorRowRxToCx(erow *row, int rx);

void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);
void editorCopy(int start_y, int start_x, int end_y, int end_x);
void editorCopyLine(void);
void editorPaste(void);
void editorAddToUndo(enum operationType type, int cx, int cy, const char *data,
                     int dataLen);
void editorUndo(void);
void editorRedo(void);

void editorOpen(char *filename);
void editorReload(void);
void editorSave(void);

void editorFind(void);

void editorAddTab(void);
void editorCloseCurrentTab(void);
void editorSwitchTab(int tab);
void editorDrawTabBar(void);

void editorInitSyntax(void);
void editorDetectLanguage(char *filename);
void editorUpdateSyntax(erow *row);
void editorApplySyntaxToRows(void);
int editorSyntaxToColor(int token);

void editorFileBrowserToggle(void);
void editorFileBrowserUpdate(void);
void editorFileBrowserDraw(void);
void editorFileBrowserProcessKey(int key);

void editorTerminalToggle(void);
void editorTerminalExecute(const char *cmd);
void editorTerminalDraw(void);
void editorTerminalProcessKey(int key);

void editorHelpInit(void);
void editorHelpToggle(void);
void editorHelpDraw(void);
void editorHelpProcessKey(int key);

void editorExitOpenBuffer(void);

void editorScroll(void);
void editorDrawRows(void);
void editorDrawStatusBar(void);
void editorDrawMessageBar(void);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);

void editorMoveCursor(int key);
void editorProcessKeypress(void);

void initEditor(void);

#endif
