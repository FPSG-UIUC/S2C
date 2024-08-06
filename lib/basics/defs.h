#ifndef __DEFS_H__
#define __DEFS_H__

#define TIMER   0
#define LLSC    1

#define DOUBLE_TRAVERSE
//#define TRIPLE_TRAVERSE

#define BARRIER1
#define BARRIER2

//#define DEBUG

#ifdef DEBUG
#define _dprintf printf
#else
#define _dprintf(...)
#endif


#endif
