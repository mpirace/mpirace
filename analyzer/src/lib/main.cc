#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Analysis/LoopInfo.h"

#include "global.h"
#include "mpirace.h"

cl::list<std::string> InputFileNames(
    cl::Positional, cl::OneOrMore, cl::desc("<input bitcode files>"));

cl::opt<unsigned> VerboseLevel(
    "verbose-level", cl::desc("Print information at which verbose level"),
    cl::init(0));

cl::opt<bool> MPIRace(
    "race",
    cl::desc("Detect data races in target MPI program"),
    cl::NotHidden, cl::init(false));

GlobalContext GlobalCtx;

void IterativeModulePass::run(ModuleList &modules) {
    ModuleList::iterator i, e;
    OP << "[" << ID << "] Initializing " << modules.size() << " modules ";
    bool again = true;
    while (again) {
        again = false;
        for (i = modules.begin(), e = modules.end(); i != e; ++i) {
            again |= doInitialization(i->first);
            OP << ".";
        }
    }
    OP << "\n";

    unsigned iter = 0, changed = 1;
    while (changed) {
        ++iter;
        changed = 0;
        unsigned counter_modules = 0;
        unsigned total_modules = modules.size();
        for (i = modules.begin(), e = modules.end(); i != e; ++i) {
            OP << "[" << ID << "/" << iter << "] "
               << "[" << ++counter_modules << "/" << total_modules << "] "
               << "[" << i->second << "]\n";

            bool ret = doModulePass(i->first);
            if (ret) {
                ++changed;
                OP << "\t [Changed]\n";
            } else
                OP << "\n";
        }
        OP << "[" << ID << "] Updated in " << changed << " modules.\n";
    }

    OP << "[" << ID << "] Postprocessing...\n";
    again = true;
    while (again) {
        again = false;
        for (i = modules.begin(), e = modules.end(); i != e; ++i) {
            // TODO: dump the results
            again |= doFinalization(i->first);
        }
    }

    OP << "[" << ID << "] Done!\n\n";
}

int main(int argc, char **argv)
{
    cl::ParseCommandLineOptions(argc, argv, "Data race detection\n");

    OP << "Total " << InputFileNames.size() << " file(s)\n";
    for (unsigned i = 0; i < InputFileNames.size(); ++i) {
        LLVMContext *LLVMCtx = new LLVMContext();
        SMDiagnostic Err;

        //OP << "== loading bc file: " << InputFileNames[i] << "\n";

        unique_ptr<Module> M = parseIRFile(InputFileNames[i], Err, *LLVMCtx);

        if (M == NULL) {
            OP << argv[0] << ": error loading file '"
               << InputFileNames[i] << "\n";
            continue;
        }

        Module *Module = M.release();
        StringRef MName = StringRef(strdup(InputFileNames[i].data()));
        GlobalCtx.Modules.push_back(make_pair(Module, MName));
        GlobalCtx.ModuleMaps[Module] = InputFileNames[i];
    }

    // Detect data races
    if (MPIRace) {
        MPIRacePass MR(&GlobalCtx);
        MR.run(GlobalCtx.Modules);
    }

    return 0;
}
