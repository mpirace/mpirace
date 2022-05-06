#ifndef _MPIRACE_H_
#define _MPIRACE_H_

#include "global.h"
#include "mpicall.h"

class MPIRacePass : public IterativeModulePass {
private:
    map<CallBase *, MPINonblockingCall *> NBCalls;
    map<CallBase *, MPIBlockingCall *> BCalls;
    map<CallBase *, MPIWaitCall *> WCalls;

    // Function we are currently working on
    Function *CurrentFunc;

    // LoopInfo of the current function
    LoopInfo *CurrentLoopInfo;

public:
    MPIRacePass(GlobalContext *Ctx_) :
        IterativeModulePass(Ctx_, "MPIRacePass") {
    }

    ~MPIRacePass(void) {
        OP << "== Done ==\n";
    }

    void collectMPICalls();

    MPINonblockingCall *getNonblockingCall(CallBase *);

    MPIBlockingCall *getBlockingCall(CallBase *);

    bool isLoopInvariant(Value *);

    void detectDataRaces(MPINonblockingCall *);

    virtual bool doInitialization(Module *);

    virtual bool doFinalization(Module *);

    virtual bool doModulePass(Module *);
};

#endif
