#include <fstream>

#include "llvm/IR/Constants.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/CFG.h"

#include "common.h"

static const string MPINonblockingAPIs[] = {
    "MPI_Isend", "MPI_Irsend", "MPI_Irecv"
};

static const string MPIBlockingAPIs[] = {
    "MPI_Send", "MPI_Recv"
};

static const string MPIWaitAPIs[] = {
    "MPI_Wait", "MPI_Waitall", "MPI_Waitany"
};

static const string MPIWriteAPIs[] = {
    "MPI_Irecv", "MPI_Recv"
};

static const string CPPSTLAPIs[] = {
    "_ZNSt6vectorIiSaIiEEixEm"
};

bool isMPINonblockingAPI(StringRef Name) {
    for (auto API: MPINonblockingAPIs) {
        if (Name.equals(API))
            return true;
    }
    return false;
}

bool isMPIBlockingAPI(StringRef Name) {
    for (auto API: MPIBlockingAPIs) {
        if (Name.equals(API))
            return true;
    }
    return false;
}

bool isMPIWaitAPI(StringRef Name) {
    for (auto API: MPIWaitAPIs) {
        if (Name.equals(API))
            return true;
    }
    return false;
}

bool isMPIWriteAPI(StringRef Name) {
    for (auto API: MPIWriteAPIs) {
        if (Name.equals(API))
            return true;
    }
    return false;
}

bool isCPPSTLAPI(StringRef Name) {
    for (auto API: CPPSTLAPIs) {
        if (Name.equals(API))
            return true;
    }
    return false;
}

/// Check whether the indices of a GetElementPtrInst are constants
bool isConstantIdx(GetElementPtrInst *G) {
    Value *GPtr = G->getPointerOperand();
    if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(GPtr)) {
        if (!isConstantIdx(GEPI))
            return false;
    }
    unsigned OpdNum = G->getNumOperands();
    for (unsigned i = 1; i < OpdNum; ++i) {
        if (!dyn_cast<ConstantInt>(G->getOperand(i)))
            return false;
    }
    return true;
}

/// Add successor blocks of the input block into a list
void addSuccessorBlocks(BasicBlock *BB, list<BasicBlock *> &BBL) {
    Instruction *TI = BB->getTerminator();
    for (unsigned i = 0; i < TI->getNumSuccessors(); ++i) {
        BasicBlock *Succ = TI->getSuccessor(i);
        BBL.push_back(Succ);
    }
}

/// Add predecessor blocks of the input block into a list
void addPredecessorBlocks(BasicBlock *BB, list<BasicBlock *> &BBL) {
    for (auto it = pred_begin(BB), ie = pred_end(BB); it != ie; ++it) {
        BasicBlock *Pred = *it;
        BBL.push_back(Pred);
    }
}

/// Check whether the Dst block is reachable from the Src block
bool isReachable(BasicBlock *Src, BasicBlock *Dst) {
    if (Src == Dst)
        return true;

    set<BasicBlock *> visitedBBs;
    list<BasicBlock *> toBeVisitedBBs;
    addSuccessorBlocks(Src, toBeVisitedBBs);
    while (!toBeVisitedBBs.empty()) {
        BasicBlock *curBB = toBeVisitedBBs.front();
        toBeVisitedBBs.pop_front();
        if (visitedBBs.count(curBB) != 0)
            continue;
        visitedBBs.insert(curBB);
        if (curBB == Dst)
            return true;
        addSuccessorBlocks(curBB, toBeVisitedBBs);
    }

    return false;
}

bool isLoadFromSameAddr(Value *A, Value *B) {
    LoadInst *ALI = dyn_cast<LoadInst>(A);
    LoadInst *BLI = dyn_cast<LoadInst>(B);

    if (!ALI || !BLI)
        return false;

    return ALI->getPointerOperand() == BLI->getPointerOperand();
}

string getSourceLine(Instruction *I) {
    string SrcLineInfo = "";
    MDNode *MN = I->getMetadata("dbg");
    if (!MN)
        return SrcLineInfo;

    DILocation *Loc = dyn_cast<DILocation>(MN);
    if (!Loc)
        return SrcLineInfo;

    unsigned LineNo = Loc->getLine();
    if (LineNo < 1)
        return SrcLineInfo;

    string SrcDir = Loc->getDirectory().str();
    string SrcFileName = Loc->getFilename().str();
    ifstream SrcFile(SrcDir + '/' + SrcFileName);
    unsigned cl = 0;

    while(1) {
        string SrcLine;
        getline(SrcFile, SrcLine);
        cl++;
        if (cl != LineNo)
            continue;

        SrcLineInfo = SrcFileName + ":" +
                      to_string(LineNo) + ": " + SrcLine;
        return SrcLineInfo;
    }
}

uint64_t parseAccessSize(Value *Count, Value *DataType) {
    uint64_t CountValue = 0;
    if (ConstantInt *CI = dyn_cast<ConstantInt>(Count)) {
        CountValue = CI->getValue().getZExtValue();
    } else if (BinaryOperator *BO = dyn_cast<BinaryOperator>(Count)) {
        //TODO: support non-constant count values
    }

    if (ConstantInt *CI = dyn_cast<ConstantInt>(DataType)) {
        auto DataTypeValue = CI->getValue().getZExtValue();
        if (DataTypeValue == 0x4c000101) // MPI_CHAR
            return CountValue * 1;
        else if (DataTypeValue == 0x4c000102) // MPI_UNSIGNED_CHAR
            return CountValue * 1;
        else if (DataTypeValue == 0x4c00010d) // MPI_BYTE
            return CountValue * 1;
        else if (DataTypeValue == 0x4c000405) // MPI_INT
            return CountValue * 4;
        else if (DataTypeValue == 0x4c000406) // MPI_UNSIGNED
            return CountValue * 4;
        else if (DataTypeValue == 0x4c00040a) // MPI_FLOAT
            return CountValue * 4;
        else if (DataTypeValue == 0x4c00080b) // MPI_DOUBLE
            return CountValue * 8;
    } else if (LoadInst *LI = dyn_cast<LoadInst>(DataType)) {
        //TODO: support derived datatypes
        return 0;
    }

    OP << "== Error: Unsupported type of MPI_Datatype: "
       << *DataType << "\n";

    return 0;
}

uint64_t getAccessSizeFromPointerType(Type *PtrType) {
    Type *ElemType = PtrType;

    if (!ElemType)
        return 0;

    if (ElemType->isPointerTy())
        ElemType = ElemType->getPointerElementType();

    if (ElemType->isIntegerTy())
        return ElemType->getIntegerBitWidth() / 8;
    else if (ElemType->isPointerTy() || ElemType->isDoubleTy())
        return 8; // A pointer is 64-bit on 64-bit platforms
    else
        OP << "Error: Unsupported pointer type: " << *PtrType << "\n";

    return 0;
}

void collectRootPointers(Value *Ptr, set<Value *> &RPtrs) {
    if (AllocaInst *AI = dyn_cast<AllocaInst>(Ptr))
        RPtrs.insert(Ptr);
    else if (GlobalValue *GV = dyn_cast<GlobalValue>(Ptr))
        RPtrs.insert(Ptr);
    else if (ConstantPointerNull *CPN = dyn_cast<ConstantPointerNull>(Ptr))
        RPtrs.insert(Ptr);
    else if (BitCastInst *BCI = dyn_cast<BitCastInst>(Ptr))
        return collectRootPointers(BCI->getOperand(0), RPtrs);
    else if (GetElementPtrInst *GEPI = dyn_cast<GetElementPtrInst>(Ptr))
        return collectRootPointers(GEPI->getPointerOperand(), RPtrs);
    else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Ptr))
        return collectRootPointers(CE->getAsInstruction(), RPtrs);
    else if (CallBase *CI = dyn_cast<CallBase>(Ptr)) {
        Function *CalledFunc = CI->getCalledFunction();
        StringRef CalledFuncName = CalledFunc->getName();
        if (CalledFuncName.equals("malloc"))
            RPtrs.insert(Ptr);
        else if (isCPPSTLAPI(CalledFuncName))
            RPtrs.insert(CI->getArgOperand(0));
        else
            OP << "== Error: unsupported call: " << *CI << "\n";
    } else if (LoadInst *LI = dyn_cast<LoadInst>(Ptr)) {
        Value *Addr = LI->getPointerOperand();
        // Try to find a store instruction that stores to the same address
        Instruction *PrevInst = LI->getPrevNonDebugInstruction();
        while(PrevInst) {
            if (StoreInst *SI = dyn_cast<StoreInst>(PrevInst)) {
                if (SI->getPointerOperand() == Addr)
                    return collectRootPointers(SI->getValueOperand(), RPtrs);
            }
            PrevInst = PrevInst->getPrevNonDebugInstruction();
        }
        // Continue to check predecessor blocks
        set<BasicBlock *> visitedBBs;
        list<BasicBlock *> toBeVisitedBBs;
        addPredecessorBlocks(LI->getParent(), toBeVisitedBBs);
        while (!toBeVisitedBBs.empty()) {
            BasicBlock *curBB = toBeVisitedBBs.front();
            toBeVisitedBBs.pop_front();
            if (visitedBBs.count(curBB) != 0)
                continue;
            visitedBBs.insert(curBB);
            PrevInst = curBB->getTerminator();
            bool stop = false;
            while(PrevInst) {
                //OP << " == " << *PrevInst << "\n";
                if (StoreInst *SI = dyn_cast<StoreInst>(PrevInst)) {
                    if (SI->getPointerOperand() == Addr) {
                        collectRootPointers(SI->getValueOperand(), RPtrs);
                        stop = true;
                        break;
                    }
                }
                PrevInst = PrevInst->getPrevNonDebugInstruction();
            }
            if (!stop)
                addPredecessorBlocks(curBB, toBeVisitedBBs);
        }
    } else
        OP << KYEL "== Unsupported pointer in collectRootPointers(): \n"
           <<  *Ptr << "\n" << KNRM;
}
