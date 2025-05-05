#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <errno.h>

#define CTEXTEDIT_VERSION "0.1.0"
#define TAB_SIZE 4
#define QUIT_TIMES 3

/* Define CTRL_KEY macro - converts a character to its control key equivalent */
#define CTRL_KEY(k) ((k) & 0x1F)

// Define special key codes
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
    DEL_KEY
};

// Editor row structure
typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
} erow;

// Editor config structure
typedef struct editorConfig {
    int cx, cy;         // Cursor position
    int rx;             // Render position
    int rowoff;         // Row offset for scrolling
    int coloff;         // Column offset for scrolling
    int screenrows;     // Number of rows in the terminal
    int screencols;     // Number of columns in the terminal
    int numrows;        // Number of rows in file
    erow *row;          // Array of rows
    int dirty;          // Flag to indicate unsaved changes
    char *filename;     // Current filename
    char statusmsg[80]; // Status message
    time_t statusmsg_time;
    struct termios orig_termios; // Original terminal attributes
} editorConfig;

// Global editor state
extern editorConfig E;

// Function prototypes

// Terminal handling
void enableRawMode(void);
void disableRawMode(void);
void die(const char *s);
int editorReadKey(void);
int getCursorPosition(int *rows, int *cols);
int getWindowSize(int *rows, int *cols);

// Row operations
void editorUpdateRow(erow *row);
void editorInsertRow(int at, char *s, size_t len);
void editorFreeRow(erow *row);
void editorDelRow(int at);
char *editorRowsToString(int *buflen);
void editorRowInsertChar(erow *row, int at, int c);
void editorRowDelChar(erow *row, int at);
void editorRowAppendString(erow *row, char *s, size_t len);

// Editor operations
void editorInsertChar(int c);
void editorInsertNewline(void);
void editorDelChar(void);

// File I/O
void editorOpen(char *filename);
void editorSave(void);

// Find operations
void editorFind(void);

// Output
void editorScroll(void);
void editorDrawRows(void);
void editorDrawStatusBar(void);
void editorDrawMessageBar(void);
void editorRefreshScreen(void);
void editorSetStatusMessage(const char *fmt, ...);

// Input
void editorMoveCursor(int key);
void editorProcessKeypress(void);

// Init
void initEditor(void);

#endif
