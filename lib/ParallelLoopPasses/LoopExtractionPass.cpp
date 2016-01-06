#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/TableGen/Record.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ParallelLoopPasses/IsParallelizableLoopPass.h"
#include "llvm/Support/CommandLine.h"
#include <iostream>
#include <string>
#include <set>
#include <math.h>

using namespace llvm;
using namespace std;
using namespace parallelize;

namespace {
	static const int DEFAULT_THREAD_COUNT = 1;
	static const int DEFAULT_MIN_LOOP_COUNT = 100;

	static cl::opt<unsigned> ThreadLimit(
		"thread-limit", cl::init(DEFAULT_THREAD_COUNT), cl::value_desc("threadNo"),
		cl::desc("The number of threads to use for parallelization (default 1)"));

	static cl::opt<unsigned> MinLoopIterations(
		"min-iter", cl::init(DEFAULT_MIN_LOOP_COUNT), cl::value_desc("iterationNo"),
		cl::desc("The minimum number of iterations for a loop to be parallelized (default 100)"));
	/*

	*/
	struct LoopExtractionPass : public FunctionPass {
		int noThreads = ThreadLimit.getValue();
		int minNoIter = MinLoopIterations.getValue();

		//ID of the pass
		static char ID;

		//Constructor
		LoopExtractionPass() : FunctionPass(ID) {}

		//Set LoopInfo pass to run before this one so we can access its results
		void getAnalysisUsage(AnalysisUsage &AU) const {
			AU.addRequired<IsParallelizableLoopPass>();
			AU.addRequired<DominatorTreeWrapperPass>();
		}

		bool extract(Function &F, LoopDependencyData *loopData, DominatorTree &DT, LLVMContext &context) {
			//extract the loop
			//cerr << "This loop has no dependencies so can be extracted\n";

			//get start and end of loop
			Value *startIt = loopData->getStartIt();
			Value *finalIt = loopData->getFinalIt();

			//only continue if the loop has at least the min number of iterations
			if (loopData->getTripCount() > 0) {
				if (loopData->getTripCount() < minNoIter) {
					return false;
				}
			}
			else {
				cerr << "unable to calculate trip count\n";
				return false;
			}

			//extract the loop into a new function
			CodeExtractor extractor = CodeExtractor(DT, *(loopData->getLoop()), false);
			Function *extractedLoop = extractor.extractCodeRegion();

			if (extractedLoop != 0) {
				return editExtraction(F, extractedLoop, startIt, finalIt, context);
			}
			else {
				return false;
			}
		}

		bool editExtraction(Function &F, Function *extractedLoop, Value *startIt, Value *finalIt, LLVMContext &context) {
			//setup helper functions so declararations are there to be linked later
			Module * mod = (F.getParent());
			addHelperFunctionDeclarations(context, mod);
			ValueSymbolTable symTab = mod->getValueSymbolTable();
			Function *integerDiv = cast<Function>(symTab.lookup(StringRef("integerDivide")));
			Function *createGroup = cast<Function>(symTab.lookup(StringRef("createGroup")));
			Function *asyncDispatch = cast<Function>(symTab.lookup(StringRef("asyncDispatch")));
			Function *wait = cast<Function>(symTab.lookup(StringRef("wait")));
			Function *release = cast<Function>(symTab.lookup(StringRef("release")));
			Function *exit = cast<Function>(symTab.lookup(StringRef("abort")));

			//create the struct we'll use to pass data to/from the threads
			StructType *myStruct = StructType::create(context, "ThreadPasser");
			//send the: data required, startItvalue and endIt value
			vector<Type *> elts;

			//setup struct type
			CallInst *callInst = dyn_cast<CallInst>(*(extractedLoop->user_begin()));
			int noOps = callInst->getNumArgOperands();
			for (int i = 0; i < noOps; i++) {
				elts.push_back(callInst->getOperand(i)->getType());
			}

			IRBuilder<> builder(callInst);

			list<Value *> threadStructs;

			IRBuilder<> setupBuilder(F.begin()->begin());
			Value *start = setupBuilder.CreateAlloca(startIt->getType());
			Value *end = setupBuilder.CreateAlloca(finalIt->getType());
			setupBuilder.CreateStore(startIt, start);
			setupBuilder.CreateStore(finalIt, end);
			Value *loadedStartIt = setupBuilder.CreateLoad(start);
			Value *loadedEndIt = setupBuilder.CreateLoad(end);
			Value *noIterations = setupBuilder.CreateBinOp(Instruction::Sub, loadedEndIt, loadedStartIt);
			SmallVector<Value *, 2> divArgs;
			divArgs.push_back(noIterations);
			divArgs.push_back(ConstantInt::get(Type::getInt64Ty(context), noThreads));
			Value *iterationsEach = setupBuilder.CreateCall(integerDiv, divArgs);
			//cerr << "setting up threads\n";
			for (int i = 0; i < noThreads; i++) {
				Value *threadStartIt;
				Value *endIt;
				Value *startItMult = builder.CreateBinOp(Instruction::Mul, iterationsEach, ConstantInt::get(Type::getInt64Ty(context), i));
				//cerr << "here\n";
				threadStartIt = builder.CreateBinOp(Instruction::Add, loadedStartIt, startItMult);
				if (i == (noThreads - 1)) {
					endIt = loadedEndIt;
					//cerr << "here1\n";
				}
				else {
					endIt = builder.CreateBinOp(Instruction::Add, threadStartIt, iterationsEach);
					//cerr << "here2\n";
				}

				//add final types to struct
				if (i == 0) {
					elts.push_back(threadStartIt->getType());
					elts.push_back(endIt->getType());
					myStruct->setBody(elts);
				}

				AllocaInst *allocateInst = builder.CreateAlloca(myStruct);
				//store original arguments in struct
				for (int i = 0; i < noOps; i++) {
					Value *getPTR = builder.CreateStructGEP(myStruct, allocateInst, i);
					builder.CreateStore(callInst->getOperand(i), getPTR);
				}
				//store startIt
				Value *getPTR = builder.CreateStructGEP(myStruct, allocateInst, noOps + 0);
				builder.CreateStore(threadStartIt, getPTR);
				//store endIt
				getPTR = builder.CreateStructGEP(myStruct, allocateInst, noOps + 1);
				builder.CreateStore(endIt, getPTR);
				//store the struct pointer for passing into the function - as type void *
				Value *structInst = builder.CreateCast(Instruction::CastOps::BitCast, allocateInst, Type::getInt8PtrTy(context));
				threadStructs.push_back(structInst);
			}
			
			//create a new function with added argument types
			SmallVector<Type *, 8> paramTypes;
			paramTypes.push_back((threadStructs.front())->getType());
			FunctionType *FT = FunctionType::get(extractedLoop->getFunctionType()->getReturnType(), paramTypes, false);
			string name = "_" + (extractedLoop->getName()).str() + "_";
			Function *newLoopFunc = Function::Create(FT, Function::ExternalLinkage, name, mod);

			//insert calls to this new function in separate threads
			Value *groupCall = builder.CreateCall(createGroup, SmallVector<Value *, 0>());
			for (list<Value*>::iterator it = threadStructs.begin(); it != threadStructs.end(); ++it) {
				SmallVector<Value *, 3> argsForDispatch;
				argsForDispatch.push_back(groupCall);
				argsForDispatch.push_back(*it);
				argsForDispatch.push_back(newLoopFunc);
				builder.CreateCall(asyncDispatch, argsForDispatch);
			}
			SmallVector<Value *, 2> waitArgTypes;
			waitArgTypes.push_back(groupCall);
			waitArgTypes.push_back(ConstantInt::get(Type::getInt64Ty(context), 1000000000));
			Value *complete = builder.CreateCall(wait, waitArgTypes);
			//condition on complete; if 0 OK, if non zero than force stop
			Value *completeCond = builder.CreateICmpEQ(complete, ConstantInt::get(Type::getInt64Ty(context), 0));
			BasicBlock *terminate = BasicBlock::Create(context, "terminate", &F);
			IRBuilder<> termBuilder(terminate);
			SmallVector<Value *, 1> printArgs;
			//what to return when the threads fail to terminate
			Instruction *ret = terminate->getPrevNode()->end()->getPrevNode();
			termBuilder.CreateCall(exit);
			//ret will never be called as program aborts
			termBuilder.CreateRet(cast<Value>(ret->op_begin()));
			Instruction *startInst = builder.GetInsertPoint();
			BasicBlock *cont = startInst->getParent()->splitBasicBlock(startInst->getNextNode(), "continue");
			Instruction *toDelete = startInst->getParent()->end()->getPrevNode();
			toDelete->replaceAllUsesWith(UndefValue::get(toDelete->getType()));
			toDelete->eraseFromParent();
			builder.CreateCondBr(completeCond, cont, terminate);
			SmallVector<Value *, 1> releaseArgs;
			releaseArgs.push_back(groupCall);
			IRBuilder<> cleanup(cont->begin());
			cleanup.CreateCall(release, releaseArgs);

			//delete the original call instruction
			callInst->eraseFromParent();

			//cerr << "cloning function\n";
			//clone old function into this new one that takes the correct amount of arguments
			ValueToValueMapTy vvmap;
			Function::ArgumentListType &args1 = extractedLoop->getArgumentList();
			Function::arg_iterator args2 = newLoopFunc->arg_begin();
			unsigned int p = 0;
			SmallVector<LoadInst *, 8> structElements;
			BasicBlock *writeTo = BasicBlock::Create(context, "loads", newLoopFunc);
			IRBuilder<> loadBuilder(writeTo);
			Value *castArgVal = loadBuilder.CreateBitOrPointerCast(args2, myStruct->getPointerTo());
			//cerr << "creating map\n";
			for (auto &i : args1) {
				Value *mapVal = loadBuilder.CreateStructGEP(myStruct, castArgVal, p);
				LoadInst *loadInst = loadBuilder.CreateLoad(mapVal);
				vvmap.insert(std::make_pair(cast<Value>(&i), loadInst));
				p++;
			}
			//cerr << "loading start and end it too\n";
			//load start and end it too
			Value *val = loadBuilder.CreateStructGEP(myStruct, castArgVal, p);
			LoadInst *loadInst = loadBuilder.CreateLoad(val);
			structElements.push_back(loadInst);
			val = loadBuilder.CreateStructGEP(myStruct, castArgVal, p + 1);
			LoadInst *loadInst2 = loadBuilder.CreateLoad(val);
			structElements.push_back(loadInst2);
			//need to return changed local values
			SmallVector<ReturnInst *, 8> returns;
			for (auto &bb : extractedLoop->getBasicBlockList()) {
				for (auto &i : bb.getInstList()) {
					if (isa<ReturnInst>(i)) {
						ReturnInst *ret = cast<ReturnInst>(&i);
						returns.push_back(ret);
					}
				}
			}
			//cerr << "cloning\n";
			CloneFunctionInto(newLoopFunc, extractedLoop, vvmap, false, returns, "");
			//bridge first bb to cloned bbs
			loadBuilder.CreateBr((newLoopFunc->begin())->getNextNode());

			//Replace values with new values in function

			//cerr << "replacing old function values\n";
			bool startFound = false;
			Instruction *exitCond;
			SmallVector<LoadInst *, 8>::iterator element = structElements.begin();
			//change start and end iter values
			//cerr << "changing iteration bounds\n";
			for (auto &bb : newLoopFunc->getBasicBlockList()) {
				for (auto &i : bb.getInstList()) {
					if (isa<PHINode>(i) && !startFound) {
						User::op_iterator operands = i.op_begin();
						operands[0] = *element++;
						startFound = true;
					}
					if (strncmp((i.getName()).data(), "exitcond", 8) == 0 || strncmp((i.getName()).data(), "cmp", 3) == 0) {
						exitCond = &i;
					}
				}
			}
			User::op_iterator operands = exitCond->op_begin();
			operands[1] = *element++;

			//Mark the function to avoid infinite extraction
			newLoopFunc->addFnAttr("Extracted", "true");
			extractedLoop->addFnAttr("Extracted", "true");
			F.addFnAttr("Extracted", "true");
			return true;
		}

		void addHelperFunctionDeclarations(LLVMContext &context, Module *mod) {
			//Integer divide
			SmallVector<Type *, 2> divParamTypes;
			divParamTypes.push_back(Type::getInt64Ty(context));
			divParamTypes.push_back(Type::getInt64Ty(context));
			FunctionType *intDivFunctionType = FunctionType::get(Type::getInt64Ty(context), divParamTypes, false);
			mod->getOrInsertFunction("integerDivide", intDivFunctionType);

			//create opaque type
			StructType *groupStruct = StructType::create(context, "struct.dispatch_group_s");

			//Create thread group
			SmallVector<Type *, 0> groupParamTypes;
			FunctionType *groupFunctionType = FunctionType::get(groupStruct->getPointerElementType(), groupParamTypes, false);
			mod->getOrInsertFunction("createGroup", groupFunctionType);
			
			//Create async dispatch
			SmallVector<Type *, 3> asyncParamTypes;
			asyncParamTypes.push_back(groupStruct->getPointerElementType());
			asyncParamTypes.push_back(Type::getInt8PtrTy(context));
			asyncParamTypes.push_back((Type::getInt8PtrTy(context))->getPointerElementType());
			FunctionType *asyncFunctionType = FunctionType::get(Type::getVoidTy(context), asyncParamTypes, false);
			mod->getOrInsertFunction("asyncDispatch", asyncFunctionType);

			//Create wait
			SmallVector<Type *, 2> waitParamTypes;
			waitParamTypes.push_back(groupStruct->getPointerElementType());
			waitParamTypes.push_back(Type::getInt64Ty(context));
			FunctionType *waitFunctionType = FunctionType::get(Type::getInt64Ty(context), waitParamTypes, false);
			mod->getOrInsertFunction("wait", waitFunctionType);

			//Create release
			SmallVector<Type *, 1> releaseParamTypes;
			releaseParamTypes.push_back(groupStruct->getPointerElementType());
			FunctionType *releaseFunctionType = FunctionType::get(Type::getVoidTy(context), releaseParamTypes, false);
			mod->getOrInsertFunction("release", releaseFunctionType);

			//Create abort
			SmallVector<Type *, 0> abortParamTypes;
			FunctionType *abortFunctionType = FunctionType::get(Type::getVoidTy(context), abortParamTypes, false);
			mod->getOrInsertFunction("abort", abortFunctionType);
		}

		virtual bool runOnFunction(Function &F) {
			//get data from the IsParallelizableLoopPass analysis
			IsParallelizableLoopPass &IP = getAnalysis<IsParallelizableLoopPass>();
			DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();

			if (!F.hasFnAttribute("Extracted") && noThreads > DEFAULT_THREAD_COUNT) {
				list<LoopDependencyData *> loopData = IP.getResultsForFunction(F);
				cerr << "In function " << (F.getName()).data() << "\n";
				for (list<LoopDependencyData *>::iterator i = loopData.begin(); i != loopData.end(); i++) {
					cerr << "Found a loop\n";
					LoopDependencyData *loopData = *i;
					LLVMContext &context = (loopData->getLoop())->getHeader()->getContext();

					if ((loopData->getDependencies()).size() == 0) {
						if (loopData->isParallelizable()) {
							bool success = extract(F, loopData, DT, context);
						}
					}
					else {
						return false;
					}
				}
				cerr << "Loop extraction for function complete\n";
				return true;
			}
			return false;
		}
	};
}

char LoopExtractionPass::ID = 1;
static RegisterPass<LoopExtractionPass> reg2("LoopExtractionPass",
	"Extracts loops into functions that can be called in separate threads for parallelization");
