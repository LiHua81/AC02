#ifndef PORT_KEY_H
#define PORT_KEY_H

typedef enum {
    KEY_NONE,
    KEY0_SHORT,
    KEY1_SHORT
} key_event_t;

key_event_t key_scan(void);

#endif