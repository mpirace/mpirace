#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"

#include "mpicall.h"
#include "mpirace.h"

MPIWaitCall::MPIWaitCall(CallBase *CI) {
    MPICallInst = CI;
    APIName = CI->getCalledFunction()->getName();
    if (APIName.equals("MPI_Wait")) {
        WaitCount = ConstantInt::get(Type::getInt32Ty(CI->getContext()), 1);
        MPIRequest = CI->getArgOperand(0);
    } else if (APIName.equals("MPI_Waitall")) {
        WaitCount = CI->getArgOperand(0);
        MPIRequest = CI->getArgOperand(1);
    } else if (APIName.equals("MPI_Waitany")) {
        WaitCount = ConstantInt::get(Type::getInt32Ty(CI->getContext()), 1);
        MPIRequest = CI->getArgOperand(1);
    } else
        OP << "Unsupported wait call\n";
}

MPIWaitCall::~MPIWaitCall(void) {
}

void MPIWaitCall::dumpInfo(void) {
    OP << "   ==" << *MPICallInst << "\n";
}

CallBase *MPIWaitCall::getMPICallInst(void) {
   return MPICallInst;
}

/// Check whether the input MPIRequest matches with this Wait call
bool MPIWaitCall::isMatchedMPIRequest(Value *MR) {
    if (ConstantInt *CI = dyn_cast<ConstantInt>(WaitCount)) {
        uint64_t WCValue = CI->getValue().getZExtValue();
        if (WCValue == 1 && MPIRequest == MR)
            return true;
    }

    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(MR)) {
        Value *MRPtr= GEPI->getPointerOperand();
        if (GetElementPtrInst *MGEPI = dyn_cast<GetElementPtrInst>(MPIRequest)) {
            if (MGEPI->getPointerOperand() == MRPtr) {
                //TODO: more accurate analysis is required here
                return true;
            }
        }
        if (isLoadFromSameAddr(MPIRequest, MRPtr))
            return true;
    }

    if (GetElementPtrInst *MGEPI = dyn_cast<GetElementPtrInst>(MPIRequest)) {
        Value *MGEPIPtr = MGEPI->getPointerOperand();
        if (isLoadFromSameAddr(MGEPIPtr, MR))
            return true;
    }

    if (CallBase *MRCB = dyn_cast<CallBase>(MR)) {
        if (CallBase *MPCB = dyn_cast<CallBase>(MPIRequest)) {
            if (isCPPSTLAPI(MRCB->getCalledFunction()->getName()) &&
                isCPPSTLAPI(MPCB->getCalledFunction()->getName())) {
                if (MRCB->getArgOperand(0) == MPCB->getArgOperand(0))
                    return true;
            }
        }
    }

    OP << KYEL << "\n== Unsupported types when matching MPI request ==\n"
       << "== MPIRequest: " << *MPIRequest << "\n"
       << "== MR: " << *MR << "\n" << KNRM;
    return false; 
}

MPIBlockingCall::MPIBlockingCall(CallBase *CI) {
    MPICallInst = CI;
    APIName = CI->getCalledFunction()->getName();
    if (APIName.equals("MPI_Send") ||
        APIName.equals("MPI_Recv")) {
        BufferStart = CI->getArgOperand(0);
        if (BitCastInst *BCI = dyn_cast<BitCastInst>(BufferStart))
            BufferStart = BCI->getOperand(0);
        BufferAccessSize = parseAccessSize(CI->getArgOperand(1), CI->getArgOperand(2));
        isWrite = isMPIWriteAPI(APIName);
    } else
        OP << "== Error: Unsupport MPI nonblocking call: " << APIName << "\n";
}

MPIBlockingCall::~MPIBlockingCall(void) {
}

Value *MPIBlockingCall::getBufferStart(void) {
    return BufferStart;
}

uint64_t MPIBlockingCall::getBufferAccessSize(void) {
    return BufferAccessSize;
}

MPINonblockingCall::MPINonblockingCall(MPIRacePass *MP, CallBase *CI) {
    MPass = MP;
    MPICallInst = CI;
    APIName = CI->getCalledFunction()->getName();
    if (APIName.equals("MPI_Isend") || APIName.equals("MPI_Irsend") ||
        APIName.equals("MPI_Irecv")) {
        BufferStart = CI->getArgOperand(0);
        if (BitCastInst *BCI = dyn_cast<BitCastInst>(BufferStart))
            BufferStart = BCI->getOperand(0);
        BufferAccessSize = parseAccessSize(CI->getArgOperand(1), CI->getArgOperand(2));
        MPIRequest = CI->getArgOperand(6);
        isWrite = isMPIWriteAPI(APIName);
    } else
        OP << "== Error: Unsupport MPI nonblocking call: " << APIName << "\n";
}

MPINonblockingCall::~MPINonblockingCall(void) {
}

void MPINonblockingCall::dumpInfo(void) {
    OP << "\n== Nonblocking call: " << *MPICallInst << "\n";
    OP << "== Corresponding wait call (" << MPIWaitCalls.size() << "): \n";
    for (set<MPIWaitCall *>::iterator it = MPIWaitCalls.begin(), ie = MPIWaitCalls.end();
         it != ie; ++it) {
        MPIWaitCall *WC = *it;
        WC->dumpInfo();
    }
}

CallBase *MPINonblockingCall::getMPICallInst(void) {
    return MPICallInst;
}

Value *MPINonblockingCall::getBufferStart(void) {
    return BufferStart;
}

uint64_t MPINonblockingCall::getBufferAccessSize(void) {
    return BufferAccessSize;
}

bool MPINonblockingCall::isBufferWrite(void) {
    return isWrite;
}

Value *MPINonblockingCall::getMPIRequest(void) {
    return MPIRequest;
}

void MPINonblockingCall::addWaitCall(MPIWaitCall *WC) {
    MPIWaitCalls.insert(WC);
}

bool MPINonblockingCall::isWantedWaitCall(Instruction *I,
                                       map<CallBase *, MPIWaitCall *> &WCalls) {
    CallBase *CI = dyn_cast<CallBase>(I);
    if (!CI)
        return false;
    Function *Callee = CI->getCalledFunction();
    if (!Callee)
        return false;
    StringRef CalleeName = Callee->getName();
    if (isMPIWaitAPI(CalleeName)) {
        MPIWaitCall *WC;
        if (WCalls.count(CI) > 0)
            WC = WCalls[CI];
        else
            OP << "Error, cannot get wait call for: " << *CI << "\n";
        return WC->isMatchedMPIRequest(MPIRequest);
    }
    return false;
}

/// Identify the MPI_Wait call(s) that correspond to
/// this non-blocking call
void MPINonblockingCall::identifyWaitCalls(map<CallBase *, MPIWaitCall *> &WCalls) {
    // Check instructions in the current block
    Instruction *prevInsn = MPICallInst;
    while (Instruction *curInsn = prevInsn->getNextNonDebugInstruction()) {
        if (isWantedWaitCall(curInsn, WCalls)) {
            addWaitCall(WCalls[dyn_cast<CallBase>(curInsn)]);
            return;
        }
        prevInsn = curInsn;
    }

    // Check instructions in the successor blocks
    BasicBlock *BB = MPICallInst->getParent();
    set<BasicBlock *> visitedBBs;
    list<BasicBlock *> toBeVisitedBBs;
    addSuccessorBlocks(BB, toBeVisitedBBs);
    while (!toBeVisitedBBs.empty()) {
        BasicBlock *curBB = toBeVisitedBBs.front();
        toBeVisitedBBs.pop_front();
        if (visitedBBs.count(curBB) != 0)
            continue;
        visitedBBs.insert(curBB);
        bool found = false;
        for (BasicBlock::iterator it = curBB->begin(), ie = curBB->end();
             it != ie; ++it) {
            Instruction *I = &*it;
            if (isWantedWaitCall(I, WCalls)) {
                addWaitCall(WCalls[dyn_cast<CallBase>(I)]);
                found = true;
                break;
            }
        }
        if (found)
            continue;
        addSuccessorBlocks(curBB, toBeVisitedBBs);
    }
}

bool MPINonblockingCall::checkBufferOverlap(Value *Ptr, uint64_t AccessSize) {
    if (Ptr == NULL)
        return false;

    //OP << "== Ptr: " << *Ptr << ", AccessSize: " << AccessSize << "\n"
    //   << "== BufferStart: " << *BufferStart
    //   << ", BufferAccessSize: " << BufferAccessSize << "\n";

    GetElementPtrInst *PtrGEPI = dyn_cast<GetElementPtrInst>(Ptr);
    GetElementPtrInst *BSGEPI = dyn_cast<GetElementPtrInst>(BufferStart);
    if (PtrGEPI && BSGEPI) {
        unsigned OpdNum = PtrGEPI->getNumOperands();
        if (BSGEPI->getNumOperands() != OpdNum)
            return false;
        for (unsigned i = 1; i < OpdNum; ++i) {
            Value *Opd0 = PtrGEPI->getOperand(i);
            Value *Opd1 = BSGEPI->getOperand(i);
            if (Opd0 == Opd1)
                continue;
            ConstantInt *CI0 = dyn_cast<ConstantInt>(Opd0);
            ConstantInt *CI1 = dyn_cast<ConstantInt>(Opd1);
            if (CI0 && CI1 && AccessSize != 0 &&
                BufferAccessSize != 0) {
                uint64_t Val0 = CI0->getValue().getZExtValue();
                uint64_t Val1 = CI1->getValue().getZExtValue();
                if ((Val0 <= Val1 &&
                     Val1 + BufferAccessSize <= Val0 + AccessSize) ||
                     (Val1 <= Val0 &&
                     Val0 + AccessSize <= Val1 + BufferAccessSize))
                    continue;
            }
            //TODO: more accurate analysis is required to avoid
            // potential false negatives.
            return false;
        }
        Value *PO1 = PtrGEPI->getPointerOperand();
        Value *PO2 = BSGEPI->getPointerOperand();
        if (PO1 == PO2)
            return true;
        GetElementPtrInst *POGEP1 = dyn_cast<GetElementPtrInst>(PO1);
        GetElementPtrInst *POGEP2 = dyn_cast<GetElementPtrInst>(PO2);
        if (POGEP1 && POGEP2) {
            OpdNum = POGEP1->getNumOperands();
            if (POGEP2->getNumOperands() != OpdNum)
                return false;
            for (unsigned i = 0; i < OpdNum; ++i) {
                if (POGEP1->getOperand(i) != POGEP2->getOperand(i))
                    return false;
            }
        }
    }

    set<Value *> PtrRootPtrs;
    set<Value *> BufferStartRootPtrs;
    collectRootPointers(Ptr, PtrRootPtrs);
    collectRootPointers(BufferStart, BufferStartRootPtrs);

    for (set<Value *>::iterator it = PtrRootPtrs.begin(), ie = PtrRootPtrs.end();
         it != ie; ++it) {
        Value *V = *it;
        if (dyn_cast<ConstantPointerNull>(V))
            continue;
        if (BufferStartRootPtrs.count(V) > 0) {
            //OP << "== Ptr: " << *Ptr << ", AccessSize: " << AccessSize << "\n"
            //   << "== BufferStart: " << *BufferStart
            //   << ", BufferAccessSize: " << BufferAccessSize << "\n"
            //   << "== V: " << *V << "\n";
            return true;
        }
    }

    // TODO: check potential overlaps

    return false;
}

void MPINonblockingCall::checkInstruction(Instruction *I) {
    Value *Ptr = NULL;
    uint64_t AccessSize = 0;

    if (isWrite) {
        // Nonblocking call is a write, so we need to check read and write.
        // We also need to check other MPI calls.
        if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
            Ptr = LI->getPointerOperand();
            AccessSize = getAccessSizeFromPointerType(LI->getPointerOperandType());
        } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
            Ptr = SI->getPointerOperand();
            AccessSize = getAccessSizeFromPointerType(SI->getPointerOperandType());
        } else if (CallBase *CI = dyn_cast<CallBase>(I)) {
            if (CI == MPICallInst) {
                // This is a call in a loop. Let's check whether the accessed
                // buffer address is loop invariant.
                if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(BufferStart)) {
                    // TODO: more accurate analysis to avoid false negatives
                    // caused by overlapped buffer accesses across loop iterations
                    if (!isConstantIdx(GEPI))
                        return;
                }
                CallBase *CB = dyn_cast<CallBase>(BufferStart);
                if (CB && isCPPSTLAPI(CB->getCalledFunction()->getName())) {
                    Value *Idx = CB->getArgOperand(1);
                    // TODO: more accurate analysis to avoid false negatives in
                    // C++ programs
                    if (!dyn_cast<ConstantInt>(Idx))
                        return;
                }
            }
            MPINonblockingCall *TempNBCall = MPass->getNonblockingCall(CI);
            if (TempNBCall) {
                Ptr = TempNBCall->getBufferStart();
                AccessSize = TempNBCall->getBufferAccessSize();
            }
            MPIBlockingCall *TempBCall = MPass->getBlockingCall(CI);
            if (TempBCall) {
                Ptr = TempBCall->getBufferStart();
                AccessSize = TempBCall->getBufferAccessSize();
            }
        }
    } else {
        // Nonblocking call is a read, so we only need to check write
        if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
            Ptr = SI->getPointerOperand();
            AccessSize = getAccessSizeFromPointerType(SI->getPointerOperandType());
        } else if (CallBase *CI = dyn_cast<CallBase>(I)) {
            MPINonblockingCall *TempCall = MPass->getNonblockingCall(CI);
            if (TempCall && TempCall->isBufferWrite()) {
                Ptr = TempCall->getBufferStart();
                AccessSize = TempCall->getBufferAccessSize();
            }
        }
    }
    if (checkBufferOverlap(Ptr, AccessSize)) {
        OP << KGRN << "== Found a data race:\n"
           KMAG << "   ==" << *MPICallInst << "\n"
           KYEL << "       == " << getSourceLine(MPICallInst) << "\n"
           KMAG << "   ==" << *I << "\n"
           KYEL << "       == " << getSourceLine(I) << "\n" << KNRM;
    }
}

bool MPINonblockingCall::isWaitCallOfThisNonblockingCall(Instruction *I) {
    for (set<MPIWaitCall *>::iterator it = MPIWaitCalls.begin(), ie = MPIWaitCalls.end();
         it != ie; ++it) {
        MPIWaitCall *WC = *it;
        if (WC->getMPICallInst() == I)
            return true;
    }
    return false;
}

/// We need to check every load/store instruction on
/// the program path from a nonblocking call to a wait call.
void MPINonblockingCall::doDataRaceDetection(map<CallBase *, MPIWaitCall *> &WCalls) {

    identifyWaitCalls(WCalls);

    dumpInfo();

    for (set<MPIWaitCall *>::iterator it = MPIWaitCalls.begin(), ie = MPIWaitCalls.end();
         it != ie; ++it) {
        MPIWaitCall *WC = *it;
        CallBase *WCInst = WC->getMPICallInst();

        // Check instructions in the current block
        Instruction *prevInsn = MPICallInst;
        while (Instruction *curInsn = prevInsn->getNextNonDebugInstruction()) {
            if (isWaitCallOfThisNonblockingCall(curInsn))
                return;
            checkInstruction(curInsn);
            prevInsn = curInsn;
        }

        // Check instructions in successor blocks
        BasicBlock *BB = MPICallInst->getParent();
        set<BasicBlock *> visitedBBs;
        list<BasicBlock *> toBeVisitedBBs;
        Instruction *TI = BB->getTerminator();
        for (unsigned i = 0; i < TI->getNumSuccessors(); ++i) {
            BasicBlock *Succ = TI->getSuccessor(i);
            if (isReachable(Succ, WCInst->getParent()))
                toBeVisitedBBs.push_back(Succ);
        }
        // Check whether we need to remove a successor block
        BranchInst *BI = dyn_cast<BranchInst>(TI);
        if (BI && BI->isConditional()) {
            Value *Cond = BI->getCondition();
            CmpInst *CI = dyn_cast<CmpInst>(Cond);
            if (CI && CI->getPredicate() == CmpInst::ICMP_NE) {
                bool remove = false;
                if (CI->getOperand(0) == MPICallInst) {
                    ConstantInt *Opd1 = dyn_cast<ConstantInt>(CI->getOperand(1));
                    if (Opd1 && Opd1->isZero())
                        remove = true;
                }
                if (CI->getOperand(1) == MPICallInst) {
                    ConstantInt *Opd0 = dyn_cast<ConstantInt>(CI->getOperand(0));
                    if (Opd0 && Opd0->isZero())
                        remove = true;
                }
                if (remove)
                    toBeVisitedBBs.remove(TI->getSuccessor(0));
            }
        }
        while (!toBeVisitedBBs.empty()) {
            BasicBlock *curBB = toBeVisitedBBs.front();
            toBeVisitedBBs.pop_front();
            if (visitedBBs.count(curBB) != 0)
                continue;
            visitedBBs.insert(curBB);
            bool stop = false;
            for (BasicBlock::iterator it = curBB->begin(), ie = curBB->end();
                 it != ie; ++it) {
                Instruction *I = &*it;
                if (isWaitCallOfThisNonblockingCall(I)) {
                    stop = true;
                    break;
                }
                checkInstruction(I);
            }
            if (stop)
                continue;
            TI = curBB->getTerminator();
            for (unsigned i = 0; i < TI->getNumSuccessors(); ++i) {
                BasicBlock *Succ = TI->getSuccessor(i);
                if (isReachable(Succ, WCInst->getParent()))
                    toBeVisitedBBs.push_back(Succ);
            }
        }
    }
}
