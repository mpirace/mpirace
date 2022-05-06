#include "llvm/IR/Module.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/CFG.h"
#include "llvm/Analysis/LoopInfo.h"

#include "mpirace.h"

/// Iterate the instructions in current function to
/// collect non-blocking and wait MPI calls
void MPIRacePass::collectMPICalls() {
    for (Function::iterator bt = CurrentFunc->begin(), be = CurrentFunc->end();
         bt != be; ++bt) {
        BasicBlock *BB = &*bt;
        for (BasicBlock::iterator it = BB->begin(), ie = BB->end();
             it != ie; ++it) {
            Instruction *I = &*it;
            CallBase *CI = dyn_cast<CallBase>(I);
            if (!CI)
                continue;
            Function *Callee = CI->getCalledFunction();
            if (!Callee)
                continue;
            StringRef CalleeName = Callee->getName();
            if (isMPINonblockingAPI(CalleeName))
                NBCalls[CI] = new MPINonblockingCall(this, CI);
            if (isMPIBlockingAPI(CalleeName))
                BCalls[CI] = new MPIBlockingCall(CI);
            if (isMPIWaitAPI(CalleeName))
                WCalls[CI] = new MPIWaitCall(CI);
        }
    }
}

MPINonblockingCall *MPIRacePass::getNonblockingCall(CallBase *CI) {
    if (NBCalls.count(CI) > 0)
        return NBCalls[CI];
    else
        return NULL;
}

MPIBlockingCall *MPIRacePass::getBlockingCall(CallBase *CI) {
    if (BCalls.count(CI) > 0)
        return BCalls[CI];
    else
        return NULL;
}

bool MPIRacePass::isLoopInvariant(Value *V) {
    Instruction *I = dyn_cast<Instruction>(V);
    if (!I)
        return true;

    Loop *L = CurrentLoopInfo->getLoopFor(I->getParent());
    if (!L)
        return true;

    return L->isLoopInvariant(V);
}

/// Detect potential data races for this nonblocking call.
void MPIRacePass::detectDataRaces(MPINonblockingCall *NBC) {
    NBC->doDataRaceDetection(WCalls);
}

bool MPIRacePass::doInitialization(Module *M) {
    return false;
}

bool MPIRacePass::doFinalization(Module *M) {
    return false;
}

bool MPIRacePass::doModulePass(Module *M) {
    for (Module::iterator f = M->begin(), fe = M->end();
         f != fe; ++f) {
        CurrentFunc = &*f;

        if (CurrentFunc->empty())
            continue;

        collectMPICalls();

        if (NBCalls.size() == 0)
            continue;

        DominatorTree DT(*CurrentFunc);
        CurrentLoopInfo = new LoopInfo(DT);

        OP << "\n\n== Identified nonblocking MPI calls in <"
           << CurrentFunc->getName() << ">:\n";
        for (map<CallBase *, MPINonblockingCall *>::iterator
               it = NBCalls.begin(), ie = NBCalls.end(); it != ie; ++it) {
            MPINonblockingCall *NBC = it->second;
            detectDataRaces(NBC);
        }

        // Do clearnup
        for (map<CallBase *, MPINonblockingCall *>::iterator
               it = NBCalls.begin(), ie = NBCalls.end(); it != ie; ++it) {
            MPINonblockingCall *NBC = it->second;
            delete NBC;
        }
        NBCalls.clear();
        for (map<CallBase *, MPIBlockingCall *>::iterator
               it = BCalls.begin(), ie = BCalls.end(); it != ie; ++it) {
            MPIBlockingCall *BC = it->second;
            delete BC;
        }
        BCalls.clear();
        for (map<CallBase *, MPIWaitCall *>::iterator
               it = WCalls.begin(), ie = WCalls.end(); it != ie; ++it) {
            MPIWaitCall *WC = it->second;
            delete WC;
        }
        WCalls.clear();

        delete CurrentLoopInfo;
    }

    return false;
}
