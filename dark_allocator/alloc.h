#ifndef _ALLOC_H_
#define _ALLOC_H_

static int kleak(void);
static int kfill(void);
static int kwork(void*);
static int kstore(long long unsigned int);
int start_module(void);
void stop_module(void);

#endif
