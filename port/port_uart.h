#ifndef PORT_UART_H
#define PORT_UART_H

void uart_init(void);
void uart_send(const char *str);
int  uart_read_line(char *buf);
void uart_on_idle(void);

#endif
