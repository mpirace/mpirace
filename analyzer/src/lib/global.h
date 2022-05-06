#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include "common.h"

typedef vector<pair<llvm::Module *, llvm::StringRef>> ModuleList;
typedef unordered_map<llvm::Module *, llvm::StringRef> ModuleNameMap;

struct GlobalContext {
    GlobalContext() {
        // Initialize global statistics
        NumFunctions = 0;
    }

    // Global statistics
    unsigned NumFunctions;

    ModuleList Modules;
    ModuleNameMap ModuleMaps;
};

class IterativeModulePass {
protected:
    GlobalContext *Ctx;
    const char *ID;
public:
    IterativeModulePass(GlobalContext *Ctx_, const char *ID_)
      : Ctx(Ctx_), ID(ID_) {}

    // Run on each module before iterative pass
    virtual bool doInitialization(llvm::Module *M) {
        return true;
    }

    // Run on each module after iterative pass
    virtual bool doFinalization(llvm::Module *M) {
        return true;
    }

    // Iterative pass
    virtual bool doModulePass(llvm::Module *M) {
        return false;
    }

    virtual void run(ModuleList &modules);
};

#endif
