#include <llvm/Pass.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>

#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/DerivedTypes.h>

#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/ErrorHandling.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#define DEBUG_TYPE "stabilizer"

using namespace llvm;

static std::string toStdString(const std::string& s) {
    return s;
}

template <typename T>
static std::string toStdString(const T& t) {
    return t.str();
}

enum {
    ALIGN = 64
};

// Randomization configuration options
cl::opt<bool> stabilize_heap   ("stabilize-heap",    cl::init(false), cl::desc("Randomize heap object placement"));
cl::opt<bool> stabilize_stack  ("stabilize-stack",   cl::init(false), cl::desc("Randomize stack frame placement"));
cl::opt<bool> stabilize_code   ("stabilize-code",    cl::init(false), cl::desc("Randomize function placement"));

struct StabilizerImpl {
    Function* registerFunction;
    Function* registerConstructor;
    Function* registerStackPad;

    StabilizerImpl() {}

    enum Platform {
        x86_64,
        x86_32,
        PowerPC,
        INVALID
    };

    /**
     * \brief Get the architecture targeted by a given module
     * \arg m The module being transformed
     * \returns A Platform value
     */
    Platform getPlatform(Module& m) {
        std::string triple = toStdString(m.getTargetTriple());

        // Convert the target-triple to lowercase
        std::transform(triple.begin(), triple.end(), triple.begin(), ::tolower);

        if(triple.find("x86_64") != std::string::npos
            || triple.find("amd64") != std::string::npos) {
            return x86_64;

        } else if(triple.find("i386") != std::string::npos
            || triple.find("i486") != std::string::npos
            || triple.find("i586") != std::string::npos
            || triple.find("i686") != std::string::npos) {
            return x86_32;

        } else if(triple.find("powerpc") != std::string::npos) {
            return PowerPC;

        } else {
            return INVALID;
        }
    }

    /**
     * \brief Get the intptr_t type for the given platform
     * \arg m The module being transformed
     * \returns The width of a pointer in bits
     */
    Type* getIntptrType(Module& m) {
        if(m.getDataLayout().getPointerSize() == 4) {
            return Type::getInt32Ty(m.getContext());
        } else {
            return Type::getInt64Ty(m.getContext());
        }
    }

    size_t getIntptrSize(Module& m) {
        if(m.getDataLayout().getPointerSize() == 4) {
            return 32;
        } else {
            return 64;
        }
    }

    Constant* getInt(Module& m, size_t bits, uint64_t value, bool is_signed) {
        return Constant::getIntegerValue(Type::getIntNTy(m.getContext(), bits), APInt(bits, value, is_signed));
    }

    Constant* getIntptr(Module& m, uint64_t value, bool is_signed) {
        return getInt(m, getIntptrSize(m), value, is_signed);
    }

    /**
     * \brief Check if the target platform uses PC-relative addressing for data
     * \arg m The module being transformed
     * \returns true if the platform supports PC-relative data addressing modes
     */
    bool isDataPCRelative(Module& m) {
        switch(getPlatform(m)) {
            case x86_64:
                return true;

            case x86_32:
            case PowerPC:
                return false;

            default:
                return true;
        }
    }

    /**
     * \brief Entry point for the Stabilizer compiler pass
     * \arg m The module being transformed
     * \returns whether or not the module was modified (always true)
     */
    void operator()(Module &m) {
        // LLVM places ENDBR64 before a patchable-function-entry NOP region.
        // The runtime deliberately omits that region from relocated copies,
        // so its body offset is only valid when CET branch protection is off.
        // Refuse the unsupported combination instead of copying from the
        // middle of the prefix or silently removing control-flow protection.
        if(stabilize_code && m.getModuleFlag("cf-protection-branch") != NULL) {
            report_fatal_error(
                "Stabilizer -Rcode does not support -fcf-protection=branch/full");
        }

        // Replace calls to heap functions with Stabilizer's random heap
        if(stabilize_heap) {
            randomizeHeap(m);
        }

        // Build a set of locally-defined functions
        std::set<Function*> local_functions;
        for(Function& f : m)
        {
            if(!f.isIntrinsic()
                && !f.isDeclaration()
                && f.getName() != "__gxx_personality_v0") {

                local_functions.insert(&f);
            }
        }

        declareRuntimeFunctions(m);

        std::map<Function*, GlobalVariable*> stackPads;

        // Declare the stack pad table type
        Type* stackPadType = Type::getInt8Ty(m.getContext());

        // Enable stack randomization
        if(stabilize_stack) {
            // Transform each function
            for(Function* f : local_functions) {
                // Create the stack pad table
                GlobalVariable* pad = new GlobalVariable(
                    m,
                    stackPadType,
                    false,
                    GlobalValue::InternalLinkage,
                    getInt(m, 8, 0, false),
                    f->getName()+".stack_pad"
                );

                stackPads[f] = pad;

                randomizeStack(m, *f, pad);
            }
        }

        // Get any existing module constructors
        std::vector<Value*> old_ctors = getConstructors(m);

        // Create a new constructor
        Function* ctor = makeConstructor(m, "stabilizer.module_ctor");
        BasicBlock* ctor_bb = BasicBlock::Create(m.getContext(), "", ctor);

        // Enable code randomization
        if(stabilize_code) {
            // Transform each function and register it with the stabilizer runtime
            for(Function* f : local_functions) {
                std::vector<Value*> args = randomizeCode(m, *f);

                Value* table = stackPads[f];
                if(table == NULL) {
                    table = Constant::getNullValue(PointerType::get(stackPadType, 0));
                }

                args.push_back(table);

                CallInst::Create(registerFunction, args, "", ctor_bb);
            }
        }

        // Register each existing constructor with the stabilizer runtime
        for(Value* ctor_iter : old_ctors) {
            std::vector<Value*> args;
            args.push_back(ctor_iter);
            CallInst::Create(registerConstructor, args, "", ctor_bb);
        }

        // If we're not randomizing code, declare the stack tables by themselves
        if(stabilize_stack && !stabilize_code) {
            for(auto [func, pad] : stackPads) {
                std::vector<Value*> args;
                args.push_back(pad);
                CallInst::Create(registerStackPad, args, "", ctor_bb);
            }
        }

        ReturnInst::Create(m.getContext(), ctor_bb);

        Function *main = m.getFunction("main");
        if(main != NULL) {
            main->setName("stabilizer_main");
        }
    }

    /**
     * \brief Get a list of module constructors
     * \arg m The module to scan
     */
    std::vector<Value*> getConstructors(Module& m) {
        std::vector<Value*> result;

        // Get the constructor table
        GlobalVariable *ctors = m.getGlobalVariable("llvm.global_ctors", false);

        // If not found, there aren't any constructors
        if(ctors != NULL) {
            // Get the constructor table initializer
            Constant* initializer = ctors->getInitializer();
            if(isa<ConstantArray>(initializer)) {
                ConstantArray* table = dyn_cast<ConstantArray>(initializer);

                // Get each entry in the table
                for(Use& i : table->operands()) {
                    ConstantStruct* entry = dyn_cast<ConstantStruct>(i.get());
                    Constant* f = entry->getOperand(1);
                    result.push_back(f);
                }
            } else {
                // Must be an empty ctor table...
            }
        }

        return result;
    }

    /**
     * \brief Create a single module constructor
     * Replaces any existing constructors
     * \arg m The module to add a constructor to
     * \arg name The name of the new constructor function
     * \returns The new constructor function
     */
    Function* makeConstructor(Module& m, StringRef name) {
        // Void type
        Type* void_t = Type::getVoidTy(m.getContext());

        // 32 bit integer type
        Type* i32_t = Type::getInt32Ty(m.getContext());

        // Void pointer type
        Type* void_p_t = PointerType::get(m.getContext(), 0);

        // Constructor function type
        FunctionType* ctor_fn_t = FunctionType::get(void_t, false);
        PointerType* ctor_fn_p_t = PointerType::get(ctor_fn_t, 0);

        // Constructor table entry type { i32, ptr, ptr }
        StructType* ctor_entry_t = StructType::get(i32_t, ctor_fn_p_t, void_p_t);

        // Create constructor function
        Function* init = Function::Create(ctor_fn_t, Function::InternalLinkage, name, &m);

        // Sequence of constructor table entries
        std::vector<Constant*> ctor_entries;

        // Add the entry for the new constructor
        // { i32 65535, ptr @stabilizer.module_ctor, ptr null }
        ctor_entries.push_back(
            ConstantStruct::get(ctor_entry_t,
                ConstantInt::get(i32_t, 65535, false),
                init,
                Constant::getNullValue(void_p_t)
            )
        );

        // set up the constant initializer for the new constructor table
        // [1 x { .... }] [{ ... } { ... }]
        Constant *ctor_array_const = ConstantArray::get(
            ArrayType::get(
                ctor_entries[0]->getType(),
                ctor_entries.size()
            ),
            ctor_entries
        );

        // create the new constructor table
        // @0 = appending constant [1 x { .... }] [{ ... } { ... }]
        GlobalVariable *new_ctors = new GlobalVariable(
            m,
            ctor_array_const->getType(),
            true,
            GlobalVariable::AppendingLinkage,
            ctor_array_const,
            ""
        );

        // Get the existing constructor array from the module, if any
        GlobalVariable *ctors = m.getGlobalVariable("llvm.global_ctors", false);
        // errs() << "Existing constructor table    " << *ctors << "\n";

        // give the new constructor table the appropriate name, taking it from the current table if one exists
        if(ctors) {
            new_ctors->takeName(ctors);
            ctors->setName("old.llvm.global_ctors");
            ctors->setLinkage(GlobalVariable::PrivateLinkage);
            ctors->eraseFromParent();
        } else {
            new_ctors->setName("llvm.global_ctors");
        }
        // errs() << "New constructor table         " << *new_ctors << "\n";

        return init;
    }

    /**
     * \brief Randomize the program stack on each function call
     * Adds a random pad (obtained from the Stabilizer runtime) to the stack
     * pointer prior to each function call, then restores the stack after the call.
     *
     * \arg m The module being transformed
     * \arg f The function being transformed
     */
    void randomizeStack(Module& m, llvm::Function& f, GlobalVariable* stackPad) {
        Type* ptrTy = m.getDataLayout().getAllocaPtrType(m.getContext());
        Function* stacksave = Intrinsic::getOrInsertDeclaration(&m, Intrinsic::stacksave, {ptrTy});
        Function* stackrestore = Intrinsic::getOrInsertDeclaration(&m, Intrinsic::stackrestore, {ptrTy});

        // Get all the callsites in this function
        std::vector<CallInst*> calls;

        for(BasicBlock& b : f) {
            for(Instruction& i : b) {
                if(isa<CallInst>(&i)) {
                    CallInst* c = dyn_cast<CallInst>(&i);
                    calls.push_back(c);
                }
            }
        }

        //////////////////////////////////

        // Pad the stack before each callsite
        for(CallInst* c : calls) {
            Instruction* next = c->getNextNode();

            // Load the stack pad size and widen it to an intptr
            Value* pad = new LoadInst(Type::getInt8Ty(m.getContext()), stackPad, "pad", c);
            Value* wide_pad = ZExtInst::CreateZExtOrBitCast(pad, getIntptrType(m), "", c);

            // Multiply the pad by the required stack alignment
            BinaryOperator* padSize = BinaryOperator::CreateNUWMul(
                wide_pad,
                getIntptr(m, 16, false),
                "aligned_pad",
                c
            );

            CallInst* oldStack = CallInst::Create(stacksave, "", c);
            PtrToIntInst* oldStackInt = new PtrToIntInst(oldStack, getIntptrType(m), "", c);

            BinaryOperator* newStackInt = BinaryOperator::CreateSub(oldStackInt, padSize, "", c);
            IntToPtrInst* newStack = new IntToPtrInst(newStackInt, PointerType::get(m.getContext(), 0), "", c);

            std::vector<Value*> newStackArgs;
            newStackArgs.push_back(newStack);
            CallInst::Create(stackrestore, newStackArgs, "", c);

            std::vector<Value*> oldStackArgs;
            oldStackArgs.push_back(oldStack);
            CallInst::Create(stackrestore, oldStackArgs, "", next);
        }
    }

    /**
     * \brief Transform a function to reference globals only through a relocation table.
     *
     * \arg m The module being transformed
     * \arg f The function being transformed
     * \returns The arguments to be passed to stabilizer_register_function
     */
    std::vector<Value*> randomizeCode(Module& m, Function& f) {
        static const char* PATCHABLE_ENTRY_SIZE = "32";

        // The runtime replaces the first 32 bytes with its trap/forwarding
        // header.  Make those bytes an explicit part of this function instead
        // of assuming that naturally-small functions have spare neighbouring
        // text which may safely be overwritten.
        f.addFnAttr("patchable-function-entry", PATCHABLE_ENTRY_SIZE);
        // The runtime's immutable x86_64 entry loads its destination from an
        // aligned 8-byte slot. Preserve any stronger user/backend alignment.
        if(f.getAlign().valueOrOne() < Align(8)) {
            f.setAlignment(Align(8));
        }

        // Remove stack protection (creates implicit global references)
        f.removeFnAttr(Attribute::StackProtect);
        f.removeFnAttr(Attribute::StackProtectReq);

        // Remove linkonce_odr linkage
        if(f.getLinkage() == GlobalValue::LinkOnceODRLinkage) {
            f.setLinkage(GlobalValue::ExternalLinkage);
        }

        // Replace some floating point operations with calls to un-randomized functions
        //if(isDataPCRelative(m)) {
            // Always do this--required on PowerPC
            extractFloatOperations(f);
        //}

        // Collect all the referenced global values in this function
        std::map<Constant*, std::set<Use*> > references = findPCRelativeUsesIn(f);

        if(references.size() > 0) {
            // Build an ordered list of referenced constants
            std::vector<Constant*> referencedValues;
            for(auto& p : references) {
                referencedValues.push_back(p.first);
            }

            // Create an ordered list of types for the referenced constants
            std::vector<Type*> referencedTypes;
            for(Constant* c : referencedValues) {
                referencedTypes.push_back(c->getType());
            }

            // Create the struct type for the relocation table
            StructType* relocationTableType = StructType::create(
                referencedTypes,
                (f.getName()+".relocation_table_t").str(),
                false
            );

            // Create the relocation table global variable
            GlobalVariable* relocationTable = new GlobalVariable(
                m,
                relocationTableType,
                false,  // No, the table needs to be mutable
                GlobalVariable::InternalLinkage,
                ConstantStruct::get(relocationTableType, referencedValues),
                f.getName()+".relocation_table"
            );

            // Rewrite global references to use the relocation table
            size_t index = 0;
            for(Constant* c : referencedValues) {
                for(Use* u : references[c]) {
                    Instruction* insertion_point = dyn_cast<Instruction>(u->getUser());
                    assert(insertion_point != NULL && "Only instruction uses can be rewritten");

                    if(isa<PHINode>(insertion_point)) {
                        PHINode* phi = dyn_cast<PHINode>(insertion_point);
                        BasicBlock *incoming = phi->getIncomingBlock(*u);
                        insertion_point = incoming->getTerminator();
                    }

                    // Get the relocation table slot
                    std::vector<Constant*> indices;
                    indices.push_back(Constant::getIntegerValue(Type::getInt32Ty(m.getContext()), APInt(32, 0, false)));
                    indices.push_back(Constant::getIntegerValue(Type::getInt32Ty(m.getContext()), APInt(32, (uint64_t)index, false)));

                    Constant* slot = ConstantExpr::getGetElementPtr(
                        relocationTableType,
                        relocationTable,
                        indices,
                        true    // Yes, it is in bounds
                    );

                    Value* loaded = new LoadInst(
                        // The slot address is an opaque pointer, not the type
                        // of its stored value (which may be an integer cast).
                        c->getType(),
                        slot,
                        c->getName()+".indirect",
                        insertion_point
                    );

                    u->set(loaded);
                }

                index++;
            }

            std::vector<Value*> args;

            // The function base
            args.push_back(ConstantExpr::getPointerCast(&f, PointerType::get(m.getContext(), 0)));

            // The global relocation table
            args.push_back(ConstantExpr::getPointerCast(relocationTable, PointerType::get(m.getContext(), 0)));

            // The size of the relocation table
            args.push_back(ConstantExpr::getTrunc(ConstantExpr::getSizeOf(relocationTableType), Type::getInt32Ty(m.getContext())));

            return args;

        } else {
            std::vector<Value*> args;

            // The function base
            args.push_back(ConstantExpr::getPointerCast(&f, PointerType::get(m.getContext(), 0)));

            // The global relocation table (null)
            args.push_back(Constant::getNullValue(PointerType::get(m.getContext(), 0)));

            // The size of the relocation table (0)
            args.push_back(Constant::getIntegerValue(Type::getInt32Ty(m.getContext()), APInt(32, 0, false)));

            return args;
        }
    }

    /**
     * Check if a value is or contains a global value.
     */
    bool containsGlobal(Value* v) {
        if(isa<Function>(v)) {
            Function* f = dyn_cast<Function>(v);

            if(f->isIntrinsic() || f->getName() == "__gxx_personality_v0") {
                return false;
            } else {
                return true;
            }

        } else if(isa<GlobalValue>(v)) {
            return true;

        } else if(isa<ConstantExpr>(v)) {
            ConstantExpr* e = dyn_cast<ConstantExpr>(v);

            for(Use& use : e->operands()) {
                if(containsGlobal(use.get())) {
                    return true;
                }
            }
        }

        return false;
    }

    /**
     * \brief Find all uses inside instructions that may result in PC-relative addressing.
     *
     * \arg f The function to scan for PC-relative uses
     * \returns A map of all used values, each with a set of uses
     */
    std::map<Constant*, std::set<Use*> > findPCRelativeUsesIn(Function& f) {
        std::map<Constant*, std::set<Use*> > result;

        for(BasicBlock& b : f) {
            for(Instruction& i_iter : b) {
                Instruction* i = &i_iter;

                if(isa<PHINode>(i)) {
                    PHINode* phi = dyn_cast<PHINode>(i);
                    for(size_t index = 0; index < phi->getNumIncomingValues(); index++) {
                        Value* operand = phi->getIncomingValue(index);

                        if(isa<Constant>(operand) && containsGlobal(operand)) {
                            Constant* c = dyn_cast<Constant>(operand);
                            if(result.find(c) == result.end()) {
                                result[c] = std::set<Use*>();
                            }

                            size_t operand_index = phi->getOperandNumForIncomingValue(index);
                            Use& use = phi->getOperandUse(operand_index);

                            result[c].insert(&use);
                        }
                    }
                } else {
                    // TODO: only process control flow targets on platforms that don't have PC-relative data addressing

                    for(Use& use : i->operands()) {
                        Value* operand = use.get();
                        if(isa<Constant>(operand) && containsGlobal(operand)) {
                            Constant* c = dyn_cast<Constant>(operand);
                            if(result.find(c) == result.end()) {
                                result[c] = std::set<Use*>();
                            }

                            result[c].insert(&use);
                        }
                    }
                }
            }
        }

        return result;
    }

    /**
     * \brief Replace certain floating point operations with function calls.
     * Some floating point operations (definitely int-to-float and float-to-int)
     * create implicit references to floating point constants.  Replace these
     * with function calls so they don't produce PC-relative data references in
     * randomizable code.
     *
     * \arg f The function to scan for floating point operations
     */
    void extractFloatOperations(Function& f) {
        Module& m = *f.getParent();
        std::vector<Instruction*> to_delete;
        for(BasicBlock& b : f) {
            for(Instruction& i : b) {
                if(isa<FPToSIInst>(&i)
                    || isa<FPToUIInst>(&i)
                    || isa<SIToFPInst>(&i)
                    || isa<UIToFPInst>(&i)
                    || (isa<FPTruncInst>(&i) && getPlatform(m) == PowerPC)) {

                    Function* f = getFloatConversion(m, i.getOpcode(), i.getOperand(0)->getType(), i.getType());

                    std::vector<Value*> args;
                    args.push_back(i.getOperand(0));
                    CallInst *ci = CallInst::Create(f, ArrayRef<Value*>(args), "", &i);

                    i.replaceAllUsesWith(ci);
                    to_delete.push_back(&i);

                } else {
                    for(Use& op_iter : i.operands()) {
                        Value* op = op_iter;

                        if(isa<Constant>(op)) {
                            Constant* c = dyn_cast<Constant>(op);

                            if(containsConstantFloat(c)) {
                                Type* t = op->getType();

                                GlobalVariable* g = new GlobalVariable(m, t, true, GlobalVariable::InternalLinkage, c, "fconst");

                                Instruction* insertion_point = &i;

                                if(isa<PHINode>(insertion_point)) {
                                    PHINode* phi = dyn_cast<PHINode>(insertion_point);
                                    BasicBlock *incoming = phi->getIncomingBlock(op_iter);
                                    insertion_point = incoming->getTerminator();
                                }

                                LoadInst* load = new LoadInst(t, g, "fconst.load", insertion_point);

                                op_iter.set(load);
                            }
                        }
                    }
                }
            }
        }

        for(Instruction* i : to_delete) {
            i->eraseFromParent();
        }
    }

    /**
     * \brief Check if a constant value contains a floating point constant
     * \arg c The constant to check
     * \returns true if c is a ConstantFP or contains a ConstantFP
     */
    bool containsConstantFloat(Constant* c) {
        if(isa<ConstantFP>(c)) {
            return true;

        } else if(isa<ConstantExpr>(c)) {

            for(Use& op_iter : c->operands()) {
                Constant* op = dyn_cast<Constant>(op_iter.get());

                if(containsConstantFloat(op)) {
                    return true;
                }
            }
        }

        return false;
    }

    /**
     * \brief Get a function to convert between floating point and integer types
     * Extracts floating point conversion operations into an unrandomized function,
     * which sidesteps issues caused by implicit global references by the ftosi,
     * ftoui, uitof, and sitof instructions.
     *
     * \arg m The module being processed
     * \arg in The type of the input value (some float or int type)
     * \arg out The type of the output value (some float or int type)
     * \arg is_signed If true, the function should generate a signed integer conversion
     * \returns A pointer to a function that performs the required type conversion
     */
    Function* getFloatConversion(Module& m, unsigned opcode, Type* in, Type* out) {
        // LLVM stream bullshit
        std::string name;
        raw_string_ostream ss(name);

        if(opcode == Instruction::FPToUI) {
            ss << "fptoui";

        } else if(opcode == Instruction::FPToSI) {
            ss << "fptosi";

        } else if(opcode == Instruction::UIToFP) {
            ss << "uitofp";

        } else if(opcode == Instruction::SIToFP) {
            ss << "sitofp";

        } else if(opcode == Instruction::FPTrunc) {
            ss << "fptrunc";

        } else {
            errs() << "Invalid float conversion arguments\n";
            errs() << "  opcode: " << opcode << "\n";

            errs() << "  in: ";
            in->print(errs());
            errs() << "\n";

            errs() << "  out: ";
            out->print(errs());
            errs() << "\n";

            abort();
        }

        // Include in and out types in the function name
        ss<<".";
        in->print(ss);
        ss<<".";
        out->print(ss);

        // Check the module for a function with this name
        Function *f = m.getFunction(ss.str());

        // If not found, create the function
        if(f == NULL) {
            std::vector<Type*> params;
            params.push_back(in);

            f = Function::Create(
                FunctionType::get(out, params, false),
                Function::InternalLinkage,
                ss.str(),
                &m
            );

            BasicBlock *b = BasicBlock::Create(m.getContext(), "", f);
            Instruction *r;

            // Insert the required conversion instruction
            if(opcode == Instruction::FPToUI) {
                r = new FPToUIInst(&*f->arg_begin(), out, "", b);

            } else if(opcode == Instruction::FPToSI) {
                r = new FPToSIInst(&*f->arg_begin(), out, "", b);

            } else if(opcode == Instruction::UIToFP) {
                r = new UIToFPInst(&*f->arg_begin(), out, "", b);

            } else if(opcode == Instruction::SIToFP) {
                r = new SIToFPInst(&*f->arg_begin(), out, "", b);

            } else if(opcode == Instruction::FPTrunc) {
                r = new FPTruncInst(&*f->arg_begin(), out, "", b);
            }

            ReturnInst::Create(m.getContext(), r, b);
        }

        return f;
    }

    /**
     * \brief Replace all heap calls with references to Stabilizer's randomized
     * heap.
     *
     * \arg m The module to transform
     */
    void randomizeHeap(Module& m) {
        Function *malloc_fn = m.getFunction("malloc");
        Function *calloc_fn = m.getFunction("calloc");
        Function *realloc_fn = m.getFunction("realloc");
        Function *free_fn = m.getFunction("free");

        if(malloc_fn) {
            // void* stabilizer_malloc(size_t sz)
            Function *stabilizer_malloc = Function::Create(
                 malloc_fn->getFunctionType(),
                 Function::ExternalLinkage,
                 "stabilizer_malloc",
                 &m
            );

            malloc_fn->replaceAllUsesWith(stabilizer_malloc);
        }

        if(calloc_fn) {
            // void* stabilizer_calloc(size_t n, size_t sz)
            Function *stabilizer_calloc = Function::Create(
                 calloc_fn->getFunctionType(),
                 Function::ExternalLinkage,
                 "stabilizer_calloc",
                 &m
            );

            calloc_fn->replaceAllUsesWith(stabilizer_calloc);
        }

        if(realloc_fn) {
            // void* stabilizer_realloc(void *p, size_t sz)
            Function *stabilizer_realloc = Function::Create(
                 realloc_fn->getFunctionType(),
                 Function::ExternalLinkage,
                 "stabilizer_realloc",
                 &m
            );

            realloc_fn->replaceAllUsesWith(stabilizer_realloc);
        }

        if(free_fn) {
            // void stabilizer_free(void *p)
            Function *stabilizer_free = Function::Create(
                 free_fn->getFunctionType(),
                 Function::ExternalLinkage,
                 "stabilizer_free",
                 &m
            );

            free_fn->replaceAllUsesWith(stabilizer_free);
        }
    }

    /**
     * \brief Declare all of Stabilizer's runtime functions
     * \arg m The module to transform
     */
    void declareRuntimeFunctions(Module& m) {
        // Declare the register_function runtime function
        // void stabilizer_register_function(
        //    void* codeBase, void* tableBase, size_t tableSize,
        //    uint8_t* stackPad)
        std::vector<Type*> register_function_params;
        register_function_params.push_back(PointerType::get(m.getContext(), 0));
        register_function_params.push_back(PointerType::get(m.getContext(), 0));
        register_function_params.push_back(Type::getInt32Ty(m.getContext()));
        register_function_params.push_back(PointerType::get(m.getContext(), 0));

        registerFunction = Function::Create(
             FunctionType::get(Type::getVoidTy(m.getContext()), register_function_params, false),
             Function::ExternalLinkage,
             "stabilizer_register_function",
             &m
        );

        registerFunction->addFnAttr(Attribute::NonLazyBind);

        // Declare the register_constructor runtime function
        // void stabilizer_register_constructor(void* ctor)
        registerConstructor = Function::Create(
            FunctionType::get(Type::getVoidTy(m.getContext()),
                {PointerType::get(m.getContext(), 0)}, false),
            Function::ExternalLinkage,
            "stabilizer_register_constructor",
            &m
        );

        registerConstructor->addFnAttr(Attribute::NonLazyBind);

        // Declare the register_stack_table runtime function
        // void stabilizer_register_stack_pad(uint8_t* pad)
        registerStackPad = Function::Create(
            FunctionType::get(Type::getVoidTy(m.getContext()),
                {PointerType::get(m.getContext(), 0)}, false),
            Function::ExternalLinkage,
            "stabilizer_register_stack_pad",
            &m
        );

        registerStackPad->addFnAttr(Attribute::NonLazyBind);
    }
};

struct Stabilizer : public llvm::PassInfoMixin<Stabilizer>
{
    llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager&)
    {
        stabilizer(M);
        return llvm::PreservedAnalyses::none();
    }
    static bool isRequired()
    {
        return true;
    }

private:
    StabilizerImpl stabilizer;
};

struct StabilizerLegacy : public llvm::ModulePass
{
    static char ID;

    StabilizerLegacy()
      : llvm::ModulePass(ID)
    {
    }

    bool runOnModule(llvm::Module& M) override
    {
        stabilizer(M);
        return true;
    }

private:
    StabilizerImpl stabilizer;
};

// -----------------------------------------------------------------------------
// New Pass Manager Registration
// -----------------------------------------------------------------------------
bool lowerInstrinsicsPass(llvm::Module& m);
struct LowerIntrinsics : public llvm::PassInfoMixin<LowerIntrinsics>
{
    llvm::PreservedAnalyses run(llvm::Module& m, llvm::ModuleAnalysisManager&)
    {
        lowerInstrinsicsPass(m);
        return llvm::PreservedAnalyses::none();
    }

    static bool isRequired()
    {
        return true;
    }
};
namespace {
    bool registerLowerIntrinsicsCallback(llvm::StringRef Name,
        llvm::ModulePassManager& MPM,
        llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
    {
        if (Name == "lower-intrinsics")
        {
            MPM.addPass(LowerIntrinsics());
            return true;
        }
        return false;
    }

    bool registerStabilizeCallback(llvm::StringRef Name,
        llvm::ModulePassManager& MPM,
        llvm::ArrayRef<llvm::PassBuilder::PipelineElement>)
    {
        if (Name == "stabilize")
        {
            MPM.addPass(Stabilizer());
            return true;
        }
        return false;
    }

    void registerPipelineParsingCallback(llvm::PassBuilder& PB)
    {
        PB.registerPipelineParsingCallback(registerLowerIntrinsicsCallback);
        PB.registerPipelineParsingCallback(registerStabilizeCallback);
    }
}    // namespace

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo()
{
    return {LLVM_PLUGIN_API_VERSION, "Stabilizer", LLVM_VERSION_STRING,
        registerPipelineParsingCallback};
}

// -----------------------------------------------------------------------------
// Legacy Pass Manager Registration
// -----------------------------------------------------------------------------
char StabilizerLegacy::ID = 0;
static llvm::RegisterPass<StabilizerLegacy> X(
    "stabilize", "Add support for runtime randomization of program layout");
