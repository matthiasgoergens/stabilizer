#include <set>

#include "llvm/Pass.h"

#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "IntrinsicLibcalls.h"

using namespace llvm;

#define DEBUG_TYPE "lower_intrinsics"

bool lowerInstrinsicsPass(Module &m)
{
    std::set<Function*> toDelete;

    for(llvm::Function &f : m) {
        if(!f.isIntrinsic()) {
            continue;
        }

        StringRef r = GetLibcall(f.getName());
        if(r.empty()) {
            // Most intrinsics are expected to be lowered by LLVM codegen.
            // We only rewrite the intrinsics we explicitly map to libcalls.
            continue;
        }

        Function *f_extern = m.getFunction(r);
        if(!f_extern) {
            f_extern = Function::Create(
                f.getFunctionType(),
                Function::ExternalLinkage,
                r,
                &m
            );
        }
        // Assignment tracking (-g at -O1+) attaches !DIAssignID to memory
        // intrinsics; it is invalid on ordinary calls, so drop the markers.
        for(User* u : f.users()) {
            if(auto* call = dyn_cast<CallInst>(u)) {
                at::deleteAssignmentMarkers(call);
                call->setMetadata(LLVMContext::MD_DIAssignID, nullptr);
            }
        }
        f.replaceAllUsesWith(f_extern);
        toDelete.insert(&f);
    }

    for(Function* iter : toDelete) {
        iter->eraseFromParent();
    }

    return true;
}

struct LowerIntrinsicsLegacy : public ModulePass
{
    static char ID;

    LowerIntrinsicsLegacy()
      : ModulePass(ID)
    {
    }

    virtual bool runOnModule(Module& m)
    {
        return ::lowerInstrinsicsPass(m);
    }
};

// -----------------------------------------------------------------------------
// Legacy Pass Manager Registration
// -----------------------------------------------------------------------------
char LowerIntrinsicsLegacy::ID = 0;
static RegisterPass<LowerIntrinsicsLegacy> X(
    "lower-intrinsics", "Replace all intrinsics with direct libcalls");
