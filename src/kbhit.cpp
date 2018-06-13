/*
 * kbhit() and getch() for Linux/UNIX
 * Chris Giese <geezer@execpc.com> http://my.execpc.com/~geezer
 * kbhit improved to not crash on x-window resize Bjorn Ekwall <bj0rn@blox.se> 
 * If the program crashes without resetting the attributes, the settings on 20180424 were:
 * iflag = 11520
 * oflag = 5
 * cflag = 191
 * lflag = 35387
 * c_cc = [3,28,127,21,4,0,1,0,17,19,26,255,18,15,23,22,255,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]
 * */
#include "kbhit.h"
#include <sys/time.h> /* struct timeval, select() */
#include <unistd.h> /* read() */
#include <string.h> /* memcpy() */

KBHIT::KBHIT() {
    tcgetattr(0, &old_kbd_mode);
    old_kbd_mode.c_lflag = 35387;
    tcsetattr(0, TCSANOW, &old_kbd_mode);
    memcpy(&new_kbd_mode, &old_kbd_mode, sizeof(struct termios));
    new_kbd_mode.c_lflag &= ~(ICANON | ECHO);
    new_kbd_mode.c_cc[VTIME] = 0;
    new_kbd_mode.c_cc[VMIN] = 1;
}

KBHIT::~KBHIT() {
    tcsetattr(0, TCSANOW, &old_kbd_mode);
}

void KBHIT::init() {
    tcsetattr(0, TCSANOW, &new_kbd_mode);
}

void KBHIT::deinit() {
    tcsetattr(0, TCSANOW, &old_kbd_mode);
}

int KBHIT::kbhit() {
    struct timeval timeout;
    fd_set read_handles;
    int status;

    /* check stdin (fd 0) for activity */
    FD_ZERO(&read_handles);
    FD_SET(0, &read_handles);
    timeout.tv_sec = timeout.tv_usec = 0;
    status = select(0 + 1, &read_handles, NULL, NULL, &timeout);
    if(status>0 && FD_ISSET(0,&read_handles))
        return (status);
    else
        return 0;
}

/*
static struct termios g_old_kbd_mode;

static void cooked(void)
{
    tcsetattr(0, TCSANOW, &g_old_kbd_mode);
}

static void raw(void)
{

    static char init;
    
    struct termios new_kbd_mode;

    if(init) return;
    // put keyboard (stdin, actually) in raw, unbuffered mode 
    tcgetattr(0, &g_old_kbd_mode);
    memcpy(&new_kbd_mode, &g_old_kbd_mode, sizeof(struct termios));
    new_kbd_mode.c_lflag &= ~(ICANON | ECHO);
    new_kbd_mode.c_cc[VTIME] = 0;
    new_kbd_mode.c_cc[VMIN] = 1;
    tcsetattr(0, TCSANOW, &new_kbd_mode);
    // when we exit, go back to normal, "cooked" mode 
    atexit(cooked);

    init = 1;
}
*/
