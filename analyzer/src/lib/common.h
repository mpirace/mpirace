#ifndef _COMMON_H_
#define _COMMON_H_

#include <set>
#include <list>
#include <vector>
#include <unordered_map>

using namespace llvm;
using namespace std;

// Output
#define OP llvm::errs()

// Different colors for output
#define KNRM  "\x1B[0m"   /* Normal */
#define KRED  "\x1B[31m"  /* Red */
#define KGRN  "\x1B[32m"  /* Green */
#define KYEL  "\x1B[33m"  /* Yellow */
#define KBLU  "\x1B[34m"  /* Blue */
#define KMAG  "\x1B[35m"  /* Magenta */
#define KCYN  "\x1B[36m"  /* Cyan */
#define KWHT  "\x1B[37m"  /* White */

extern bool isMPINonblockingAPI(StringRef);

extern bool isMPIBlockingAPI(StringRef);

extern bool isMPIWaitAPI(StringRef);

extern bool isMPIWriteAPI(StringRef);

extern bool isCPPSTLAPI(StringRef);

extern bool isConstantIdx(GetElementPtrInst *);

extern void addSuccessorBlocks(BasicBlock *, list<BasicBlock *> &);

extern bool isReachable(BasicBlock *, BasicBlock *);

extern bool isLoadFromSameAddr(Value *, Value *);

extern string getSourceLine(Instruction *);

extern uint64_t parseAccessSize(Value *, Value *);

extern uint64_t getAccessSizeFromPointerType(Type *);

extern void collectRootPointers(Value *, set<Value *> &);

#endif
