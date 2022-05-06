#ifndef _MPICALL_H_
#define _MPICALL_H_

#include <set>

#include "common.h"

class MPIRacePass;

class MPIWaitCall {
private:
    CallBase *MPICallInst;
    StringRef APIName;
    Value *WaitCount;
    Value *MPIRequest;

public:
    MPIWaitCall(CallBase *CI);

    ~MPIWaitCall(void);

    CallBase *getMPICallInst(void);

    bool isMatchedMPIRequest(Value *);

    void dumpInfo(void);
};

class MPIBlockingCall {
private:
    CallBase *MPICallInst;
    StringRef APIName;
    Value *BufferStart;
    uint64_t BufferAccessSize;
    bool isWrite;

public:
    MPIBlockingCall(CallBase *CI);

    ~MPIBlockingCall(void);

    Value *getBufferStart(void);

    uint64_t getBufferAccessSize(void);
};

class MPINonblockingCall {
private:
    MPIRacePass *MPass;
    CallBase *MPICallInst;
    StringRef APIName;
    Value *BufferStart;
    uint64_t BufferAccessSize;
    bool isWrite;
    Value *MPIRequest;
    set<MPIWaitCall *> MPIWaitCalls;

public:
    MPINonblockingCall(MPIRacePass *, CallBase *);

    ~MPINonblockingCall(void);

    void dumpInfo(void);

    CallBase *getMPICallInst(void);

    Value *getBufferStart(void);

    uint64_t getBufferAccessSize(void);

    bool isBufferWrite(void);

    Value *getMPIRequest(void);

    void addWaitCall(MPIWaitCall *);

    bool isWantedWaitCall(Instruction *, map<CallBase *, MPIWaitCall *> &);

    void identifyWaitCalls(map<CallBase *, MPIWaitCall *> &);

    bool checkBufferOverlap(Value *, uint64_t);

    void checkInstruction(Instruction *);

    bool isWaitCallOfThisNonblockingCall(Instruction *);

    void doDataRaceDetection(map<CallBase *, MPIWaitCall *> &);
};

#endif
