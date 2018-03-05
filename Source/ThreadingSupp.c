#include "ThreadingSupp.h"

extern inline bool threadInit(threadHandle *, threadCallback, void *);
extern inline void threadTerminate(threadHandle *);
extern inline void threadWait(threadHandle *, threadReturn *);
extern inline void threadClose(threadHandle *);

extern inline bool mutexInit(mutexHandle *);
extern inline void mutexAcquire(mutexHandle *);
extern inline void mutexRelease(mutexHandle *);
extern inline void mutexClose(mutexHandle *);

extern inline bool conditionInit(conditionHandle *);
extern inline void conditionSignal(conditionHandle *);
extern inline void conditionBroadcast(conditionHandle *);
extern inline void conditionSleep(conditionHandle *, mutexHandle *);
extern inline void conditionClose(conditionHandle *);

extern inline bool tlsInit(tlsHandle *ptls);
extern inline void tlsAssign(tlsHandle *ptls, void *ptr);
extern inline void *tlsFetch(tlsHandle *ptls);
extern inline void tlsClose(tlsHandle *ptls);