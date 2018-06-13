/*
 * kbhit() for Linux/UNIX
 * Chris Giese <geezer@execpc.com> http://my.execpc.com/~geezer
 * */

#ifndef _KBHIT_H_
#define _KBHIT_H_ 1

#include <termios.h> /* tcgetattr(), tcsetattr() */

class KBHIT {
public:
    KBHIT();
    ~KBHIT();
    int kbhit();
    void init();
    void deinit();

private:
    bool initialized;
    termios old_kbd_mode;
    termios new_kbd_mode;
};

#endif // _KBHIT_H_ defined
