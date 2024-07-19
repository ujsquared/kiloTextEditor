/*** includes ***/
// these defines are put in the includes part of the code 
// because this is actually gcc feature, including these defines
// defines the exact features one would like to use during compilation
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE
#include<sys/types.h>
#include <asm-generic/ioctls.h>
#include <errno.h>
#include <errno.h>
#include<unistd.h>
#include<errno.h>
#include<sys/ioctl.h>
#include<string.h>
#include<termios.h>
#include<stdio.h>
#include<stdlib.h>
// i can't make a vertical text editor cuz, the way buffer is printed out
// is always horizontal </3 :(
/*defines*/
#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey{
    ARROW_LEFT = 1000 ,
    ARROW_UP ,
    ARROW_RIGHT, 
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY
};
/*** data ***/
typedef struct erow{
    int size;
    char *chars;
} erow;

struct editorConfig{
    int cx,cy;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    struct termios orig_termios;
};
struct editorConfig E;
/***terminal***/
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J" , 4);
    write(STDOUT_FILENO , "\x1b[H" , 3);
    perror(s);
    exit(1);
}
void disableRawMode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}
void enableRawMode(){
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr"); 
    atexit(disableRawMode);
    
    struct termios raw = E.orig_termios;
    raw.c_lflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_lflag &= ~(ICRNL | IXON);
    raw.c_lflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); // this is a genius piece of code right here
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}
int editorReadKey(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1){
        if(nread == -1 && errno != EAGAIN) die("read");
    }
    if( c == '\x1b'){
        char seq[3];
        
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b'; 
                if(seq[2] == '~'){
                    switch (seq[1]) {
                        case '1': return HOME_KEY;
                        case '2': return END_KEY;
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '7': return HOME_KEY;
                        case '8': return END_KEY;
                    }
                }
            }
            else{
            switch(seq[1]){
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT ;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            // here case ABCD represents up,down right left respectively due to escape sequence in VT100 terminals being coded
            // that way.
            }
        }
        }else if(seq[0] == 'O') {
            switch(seq[1]){
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }


        return '\x1b';
    } else{
    return c;
    }
}
int getCursorPosition(int *rows, int *cols){
    char buf[32];
    unsigned int i = 0 ;

    if(write(STDOUT_FILENO, "\x1b[6n", 4) !=4) return -1;

    while(i< sizeof(buf) -1){
        if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if(buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if(buf[0] != '\x1b' || buf[1] != '[') return -1;
    if(sscanf(&buf[2], "%d;%d", rows, cols) !=2) return -1;
    return 0;
}
int getWindowsSize(int *rows, int *cols){
    struct winsize ws;
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12){
            return -1;
        } 
        return getCursorPosition(rows, cols); 
    } else{
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}
/*row operations */
void editorAppendRow( char *s,size_t len){
    E.row = realloc(E.row, sizeof(erow)* (E.numrows+1));

    int at = E.numrows;
    E.row[at].size = len;
    E.row[at].chars = malloc(len+1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.numrows++;
}
/*file input/output */
void editorOpen(char *filename){
    FILE *fp = fopen(filename, "r");
    if(!fp) die("fopen");

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    linelen = getline(&line, &linecap, fp);
    while ((linelen  = getline(&line, &linecap, fp)) != -1){
        while(linelen > 0 && (line[linelen-1] == '\n' ||
                    line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, linelen);
    }
    free(line);
    fclose(fp);
}
/*append buffer */
// this section creates functions to implement the functionality of dynamic strings to our program
// this is important because instead of using multiple write function which may cause flicker issue, we will use single write 
// on a string that we'll keep adding and mutating. we'll call this string a buffer. 

struct abuf{
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len){
    char *new = realloc(ab->b , ab->len + len);
    if (new == NULL) return;
    memcpy(&new[ab->len], s,len);
    ab->b = new;
    ab->len += len;
}
void abFree(struct abuf *ab){
    free(ab->b);
}

/* output */
// existence of a function named drawRows symmetrically implies existence of functionnamed DrawColumns, add it
// Make it a box boys 
void editorScroll(){
    if(E.cy < E.rowoff){
        E.rowoff = E.cy;
    }
    if(E.cy >= E.rowoff + E.screenrows){
        E.rowoff = E.cy - E.screenrows  + 1;
    }
    if(E.cx <  E.coloff){
        E.coloff = E.cx;
    }
    if(E.cx >= E.coloff + E.screencols){
        E.coloff = E.cx - E.screencols + 1; 
    }
}
void editorDrawRows(struct abuf *ab){
    int y;
    for(y = 0; y<E.screenrows; y++){
        int filerow = y + E.rowoff;
        if(filerow >= E.numrows){
        if (E.numrows == 0 && y == E.screenrows / 3){
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Kilo editor -- versions %s",KILO_VERSION);
            if(welcomelen > E.screencols) welcomelen = E.screencols;
            int padding = (E.screencols - welcomelen) / 2;
            if(padding) {
                abAppend(ab,"=", 1);
                padding--;
            }
            while(padding--) abAppend(ab," ", 1);
            abAppend(ab, welcome, welcomelen);
        } else{
        abAppend(ab, "=", 1);
        }
        } else{
            int len = E.row[filerow].size - E.coloff;
            if(len < 0 ) len = 0;
            if(len > E.screencols) len  = E.screencols;
            abAppend(ab, &E.row[filerow].chars[E.coloff], len);
        }
        abAppend(ab, "\x1b[K", 3);
        if(y < E.screenrows - 1){
            abAppend(ab, "\n", 2); // what the fuck does the slash r do????? it literally gives the same results overalllllllllllllll
        }
        
    }
       }
void editorRefreshScreen(){ 
    editorScroll();
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6); // turning off the visibility of the cursor
    abAppend(&ab, "\x1b[H", 3);
    
    editorDrawRows(&ab);
    abAppend(&ab, "\x1b[H", 3);
    char buf[32];
    snprintf(buf, sizeof(buf),"\x1b[%d;%dH" ,(E.cy-E.rowoff)+1,(E.cx - E.coloff)+1 );//
    abAppend(&ab, buf, strlen(buf));
    //abAppend(&ab, " Ujjwal Kala Samjha Kya bidhu", 30);/* it comes out to be 30 as there are 29 characters and last character is ending character so ek byte uski */
    abAppend(&ab, "\x1b[?25h", 6); // turning on the visibility of the cursor

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}
/*input */
void editorMoveCursor(int key){
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch(key){
        case ARROW_LEFT:
            if(E.cx!=0) E.cx--;
            else if (E.cy > 0){
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
             ;
            break;
        case ARROW_DOWN:
            if(E.cy  < E.numrows) E.cy++;
            break;
        case ARROW_RIGHT:
             if (row && E.cx < row -> size){
             E.cx++;
             }else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
             }
             break;
        case ARROW_UP:
            if(E.cy != 0) E.cy--;
            break;
    }// one thing we haven't implemented yet is text based movement of cursor
     // jaise vim mei up down krne pe the cursor remember where it left the line(col)
     // waise abhi ni banaaya par kya krskte lets see
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if(E.cx > rowlen){
        E.cx = rowlen;
    }
}
void editorProcessKeypress(){
    int c = editorReadKey();
    switch(c){
        case CTRL_KEY('c'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case PAGE_UP:
            //PAGE UP and PAGE DOWN both are handled in pagedown, using crazy ass ternary operator
        case PAGE_DOWN:
            {
                int times  = E.screenrows;
                while(times--) editorMoveCursor(c == PAGE_UP? ARROW_UP: ARROW_DOWN);
            }
            break;
        case ARROW_UP:
        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screenrows - 1;
            break;
    // clever part incoming------------
    // so instead of making home and key move one by one
    // to the edges of the terminal,
    // the author directly accessed the termios struct E 
    // to directly put the cursor to end or whatever, which is 
    // in my opinion sneakily smart.
    }
}
/*** init ***/
void initEditor(){
    E.cx = 10;
    E.cy = 10;
    E.coloff = 2;
    E.rowoff = 2;
    E.numrows = 0;
    E.row = NULL;

    if(getWindowsSize(&E.screenrows, &E.screencols ) == -1) die("getWindowSize");

}
int main(int argc, char *argv[]){
    enableRawMode();
    initEditor();
    if (argc >=2){
        editorOpen(argv[1]);
    }
    while(1){
        editorRefreshScreen(); 
        editorProcessKeypress();
    }
    return 0;
}
