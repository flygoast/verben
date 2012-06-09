#ifndef __NOTIFIER_H_INCLUDED__
#define __NOTIFIER_H_INCLUDED__

int notifier_create();
void notifier_close_wr();
void notifier_close_rd();
int notifier_read_fd();
void notifier_read();
void notifier_write();

#endif /* __NOTIFIER_H_INCLUDED__ */
