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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include <iostream>
#include <string>
#include <set>
#include <math.h>
#include <algorithm>
#include <stdlib.h>

using namespace llvm;
using namespace std;
using namespace parallelize;

namespace {
	static const int DEFAULT_THREAD_COUNT = 2;

	static cl::opt<unsigned> ThreadLimit(
		"thread-limit", cl::init(DEFAULT_THREAD_COUNT), cl::value_desc("threadNo"),
		cl::desc("The number of threads to use for parallelization (default 2)"));

	/*

	*/
	struct LoopExtractionPass : public ModulePass {
		int noThreads = ThreadLimit.getValue();
		StructType *threadStruct;
		StructType *returnStruct;
		BasicBlock *setupBlock;
		BasicBlock *loadBlock;

		//ID of the pass
		static char ID;

		//Constructor
		LoopExtractionPass() : ModulePass(ID) {}

		const char *getPassName() const override {
			return "Loop extraction pass";
		}

		//Set LoopInfo pass to run before this one so we can access its results
		void getAnalysisUsage(AnalysisUsage &AU) const override {
			AU.addRequired<IsParallelizableLoopPass>();
			AU.addPreserved<IsParallelizableLoopPass>();
		}

		bool extract(Function &F, LoopDependencyData *loopData, LLVMContext &context) {
			//get start and end of loop
			Value *startIt = loopData->getStartIt();
			Value *finalIt = loopData->getFinalIt();

			//setup helper functions so declarations are there to be linked later
			Module * mod = (F.getParent());
			ValueSymbolTable &symTab = mod->getValueSymbolTable();

			//Setup structs to pass data to/from the threads
			list<Value *> threadStructs = setupStructs(&F, context, loopData, startIt, finalIt, symTab);

			//create the thread function and calls to it, and setup the return of local variables afterwards
			Function *threadFunction = createThreadFunction(threadStructs.front()->getType(), context, mod, symTab, loopData, threadStructs, &F);

			//Add load instructions to the created function and replace values in the loop with them
			list<Value *> loadedArrayAndLocalValues = loadStructValuesInFunctionForLoop(threadFunction, context, loopData);

			//replace the array and local value Values in the loop with the ones loaded from the struct
			replaceLoopValues(loopData, context, threadFunction, loadedArrayAndLocalValues, loopData->getLoop(), loopData->getArgumentArgValues(), loopData->getReturnValues());

			//copy loop blocks into the function and delete them from the caller
			extractTheLoop(loopData->getLoop(), threadFunction, &F, context);

			//Mark the function to avoid infinite extraction
			threadFunction->addFnAttr("Extracted", "true");
			F.addFnAttr("Extracted", "true");
			return true;
			
		}

		list<Value *> setupStructs(Function *callingFunction, LLVMContext &context, LoopDependencyData *loopData, Value *startIt, Value *finalIt, ValueSymbolTable &symTab) {
			
			//Obtain the argument values required for passing to/from the function
			list<Value *> argArguments = loopData->getArgumentArgValues();
			list<Value *>  localArgumentsAndReturnVals = loopData->getReturnValues();
			map<PHINode *, pair<const Value *, Value *>> otherPhiNodes = loopData->getOtherPhiNodes();
			set<Value *> lifetimeValues = loopData->getLifetimeValues();

			//create the struct we'll use to pass data to the threads
			threadStruct = StructType::create(context, "ThreadPasser");

			//send the: data required, startItvalue and endIt value
			vector<Type *> elts;

			//setup thread passer
			for (auto a : argArguments) {
				elts.push_back(a->getType());
			}
			//Calc start values for the other phi nodes and add to/from struct
			for (auto a : otherPhiNodes) {
				elts.push_back(a.second.first->getType());
			}

			//create the struct we'll use to return local data variables from the threads (separate store needed for each thread)
			returnStruct = StructType::create(context, "ThreadReturner");
			vector<Type *> retElts;

			//setup return struct type with all values discovered by the analysis pass
			for (auto i : localArgumentsAndReturnVals) {
				retElts.push_back(i->getType());
			}
			returnStruct->setBody(retElts);
			
			//setup a new basic block
			BasicBlock *structSetup = BasicBlock::Create(context, "structSetup", callingFunction);
			setupBlock = structSetup;
			BasicBlock *oldPredecessor = loopData->getLoop()->getLoopPredecessor();
			BasicBlock *loopBegin = *(loopData->getLoop()->block_begin());
			for (auto &inst : oldPredecessor->getInstList()) {
				if (isa<BranchInst>(inst)) {
					User::op_iterator operand = inst.op_begin();
					while (operand != inst.op_end()) {
						if (*operand == loopBegin) {
							//replace block with new one
							*operand = structSetup;
						}
						operand++;
					}
				}
			}

			//setup structs in basic block before the loop
			IRBuilder<> builder(structSetup);

			//store all struct values created in IR
			list<Value *> threadStructs;

			//function to call to get bounds for loops
			Function *getBounds = cast<Function>(symTab.lookup(StringRef("calcBounds")));
			Function *getPhiStartVal = nullptr;

			//add start/end/return struct to struct
			elts.push_back(loopData->getInductionPhi()->getType());
			elts.push_back(loopData->getInductionPhi()->getType());
			//memory on original stack to store return values
			elts.push_back(returnStruct->getPointerTo());
			threadStruct->setBody(elts);

			Value *inductionStep = loopData->getOuterPhiStep();

			if (loopData->getInductionPhi()->getType() == Type::getInt32Ty(context)) {
				//use 32-bit functions for induction PHI instead
				getBounds = cast<Function>(symTab.lookup(StringRef("calcBounds32")));
			}

			//setup the threads in IR
			for (int i = 0; i < noThreads; i++) {
				AllocaInst *allocateInst = builder.CreateAlloca(threadStruct);
				AllocaInst *allocateReturns = builder.CreateAlloca(returnStruct);
				//store arguments in struct
				int k = 0;
				for (auto op : argArguments) {
					Value *getPTR = builder.CreateStructGEP(threadStruct, allocateInst, k);
					if (lifetimeValues.find(op) != lifetimeValues.end()) {
						//these need array memory allocated per thread
						cerr << "creating memory for data live only in loop\n";
						op->dump();
						op->getType()->dump();
						Value *myAlloca = builder.CreateAlloca(op->getType()->getArrayElementType());
						builder.CreateStore(myAlloca, getPTR);
					}
					else {
						//the same value can be shared across all threads
						builder.CreateStore(op, getPTR);
					}
					k++;
				}

				for (auto p : otherPhiNodes) {
					Value *start = *(p.second.first->use_begin());
					Value *step = p.second.second;
					//place to store start val
					Value *getPTR = builder.CreateStructGEP(threadStruct, allocateInst, k);
					SmallVector<Value *, 10> args;
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), i));
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), noThreads));
					CmpInst *inductionBranch = cast<CmpInst>(loopData->getExitCondNode());
					//see about eq and neq too
					if (inductionBranch->getPredicate() == CmpInst::ICMP_SGE || inductionBranch->getPredicate() == CmpInst::ICMP_UGE || (inductionBranch->getPredicate() == CmpInst::ICMP_EQ && loopData->getOuterPhiStep() < 0)) {
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 1));
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 0));
					}
					else if (inductionBranch->getPredicate() == CmpInst::ICMP_SGT || inductionBranch->getPredicate() == CmpInst::ICMP_UGT) {
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 0));
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 0));
					}
					else if (inductionBranch->getPredicate() == CmpInst::ICMP_SLE || inductionBranch->getPredicate() == CmpInst::ICMP_ULE || (inductionBranch->getPredicate() == CmpInst::ICMP_EQ && loopData->getOuterPhiStep() > 0)) {
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 1));
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 1));
					}
					else if (inductionBranch->getPredicate() == CmpInst::ICMP_SLT || inductionBranch->getPredicate() == CmpInst::ICMP_ULT) {
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 0));
						args.push_back(ConstantInt::get(Type::getInt32Ty(context), 1));
					}
					BranchInst *branch = cast<BranchInst>(*(inductionBranch->user_begin()));
					BasicBlock *brIfTrue = cast<BasicBlock>(branch->getOperand(2));
					cerr << "branch to if condition is true:\n";
					brIfTrue->dump();
					int leaveIfCndTrue = 1;
					for (auto bb : loopData->getLoop()->getBlocks()) {
						if (bb == brIfTrue) {
							cerr << "leave loop when condition is false\n";
							bb->dump();
							cerr << "BB in loop\n";
							leaveIfCndTrue = 0;
							break;
						}
					}
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), leaveIfCndTrue));
					int phiInCnd = 0;
					for (auto &op : inductionBranch->operands()) {
						if (cast<Value>(&op) == loopData->getInductionPhi()) {
							phiInCnd = 1;
							break;
						}
					}
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), phiInCnd));
					int cmpBefore = 1;
					BasicBlock *last = loopData->getLoop()->getBlocks().back();
					if (inductionBranch->getParent() == last) {
						cmpBefore = 0;
					}
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), cmpBefore));
					args.push_back(startIt);
					args.push_back(finalIt);
					args.push_back(inductionStep);
					args.push_back(start);
					args.push_back(step);
					args.push_back(getPTR);
					cerr << "adding call to getPhiStartVal\n";
					if (loopData->getInductionPhi()->getType() == Type::getInt32Ty(context)) {
						if (p.first->getType() == Type::getInt32Ty(context)) {
							cerr << "induction:32, phi:32\n";
							getPhiStartVal = cast<Function>(symTab.lookup(StringRef("calcStartValue3232")));
						}
						else {
							cerr << "induction:32, phi:64\n";
							getPhiStartVal = cast<Function>(symTab.lookup(StringRef("calcStartValue3264")));
						}
					}
					else {
						if (p.first->getType() == Type::getInt32Ty(context)) {
							cerr << "induction:64, phi:32\n";
							getPhiStartVal = cast<Function>(symTab.lookup(StringRef("calcStartValue6432")));
						}
						else {
							cerr << "induction:64, phi:64\n";
							getPhiStartVal = cast<Function>(symTab.lookup(StringRef("calcStartValue6464")));
						}
					}
					start->getType()->dump();
					step->getType()->dump();
					builder.CreateCall(getPhiStartVal, args);
					k++;
				}
				//store startIt
				Value *getPTRStart = builder.CreateStructGEP(threadStruct, allocateInst, k);
				//store endIt
				Value *getPTREnd = builder.CreateStructGEP(threadStruct, allocateInst, k + 1);
			
				SmallVector<Value *, 9> args;
				args.push_back(ConstantInt::get(Type::getInt32Ty(context), i));
				args.push_back(ConstantInt::get(Type::getInt32Ty(context), noThreads));
				CmpInst *inductionBranch = cast<CmpInst>(loopData->getExitCondNode());
				//see about eq and neq too
				if (inductionBranch->getPredicate() == CmpInst::ICMP_SGE || inductionBranch->getPredicate() == CmpInst::ICMP_UGE || (inductionBranch->getPredicate() == CmpInst::ICMP_EQ && loopData->getOuterPhiStep() < 0)) {
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), 1));
				}
				else if (inductionBranch->getPredicate() == CmpInst::ICMP_SGT || inductionBranch->getPredicate() == CmpInst::ICMP_UGT) {
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), 0));
				}
				else if (inductionBranch->getPredicate() == CmpInst::ICMP_SLE || inductionBranch->getPredicate() == CmpInst::ICMP_ULE || (inductionBranch->getPredicate() == CmpInst::ICMP_EQ && loopData->getOuterPhiStep() > 0)) {
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), 1));
				}
				else if (inductionBranch->getPredicate() == CmpInst::ICMP_SLT || inductionBranch->getPredicate() == CmpInst::ICMP_ULT) {
					args.push_back(ConstantInt::get(Type::getInt32Ty(context), 0));
				}
				BranchInst *branch = cast<BranchInst>(*(inductionBranch->user_begin()));
				BasicBlock *brIfTrue = cast<BasicBlock>(branch->getOperand(2));
				int leaveIfCndTrue = 1;
				for (auto bb : loopData->getLoop()->getBlocks()) {
					if (bb == brIfTrue) {
						leaveIfCndTrue = 0;
						break;
					}
				}
				args.push_back(ConstantInt::get(Type::getInt32Ty(context), leaveIfCndTrue));
				int phiInCnd = 0;
				for (auto &op : inductionBranch->operands()) {
					if (cast<Value>(&op) == loopData->getInductionPhi()) {
						phiInCnd = 1;
						break;
					}
				}
				args.push_back(ConstantInt::get(Type::getInt32Ty(context), phiInCnd));
				int cmpBefore = 1;
				BasicBlock *last = loopData->getLoop()->getBlocks().back();
				if (inductionBranch->getParent() == last) {
					cmpBefore = 0;
				}
				args.push_back(ConstantInt::get(Type::getInt32Ty(context), cmpBefore));
				args.push_back(startIt);
				args.push_back(finalIt);
				args.push_back(inductionStep);
				args.push_back(getPTRStart);
				args.push_back(getPTREnd);
				cerr << "adding call to getBounds\n";
				builder.CreateCall(getBounds, args);
				
				//return struct therefore at index noCallOperands + 2
				Value *getPTR = builder.CreateStructGEP(threadStruct, allocateInst, k + 2);
				builder.CreateStore(allocateReturns, getPTR);
				
				//store the struct pointer for passing into the function - as type void *
				Value *structInst = builder.CreateCast(Instruction::CastOps::BitCast, allocateInst, Type::getInt8PtrTy(context));
				threadStructs.push_back(structInst);
			}

			return threadStructs;
		}

		Function *createThreadFunction(Type *threadStructPointerType, LLVMContext &context, Module *mod, ValueSymbolTable &symTab, LoopDependencyData *loopData, 
										list<Value *> threadStructs, Function *callingFunction) {
			Function *createGroup = cast<Function>(symTab.lookup(StringRef("createGroup")));
			Function *asyncDispatch = cast<Function>(symTab.lookup(StringRef("asyncDispatch")));
			Function *wait = cast<Function>(symTab.lookup(StringRef("wait")));
			Function *release = cast<Function>(symTab.lookup(StringRef("release")));
			Function *exit = cast<Function>(symTab.lookup(StringRef("abort")));

			//create a new function with added argument types and return type
			SmallVector<Type *, 1> paramTypes;
			paramTypes.push_back(threadStructPointerType);
			FunctionType *FT = FunctionType::get(Type::getVoidTy(context), paramTypes, false);
			Function *newLoopFunc = Function::Create(FT, Function::ExternalLinkage, "threadFunction", mod);

			//add basic block to the new function
			loadBlock = BasicBlock::Create(context, "load", newLoopFunc);

			//add calls to it, one per thread
			IRBuilder<> builder(setupBlock);
			cerr << "adding call to createGroup\n";
			Value *groupCall = builder.CreateCall(createGroup, SmallVector<Value *, 0>());
			for (list<Value*>::iterator it = threadStructs.begin(); it != threadStructs.end(); ++it) {
				SmallVector<Value *, 3> argsForDispatch;
				argsForDispatch.push_back(groupCall);
				argsForDispatch.push_back(*it);
				argsForDispatch.push_back(newLoopFunc);
				cerr << "adding call to asyncDispatch\n";
				builder.CreateCall(asyncDispatch, argsForDispatch);
				//TODO: delete - temporary for debugging
				//SmallVector<Value *, 3> args;
				//args.push_back(*it);
				//builder.CreateCall(newLoopFunc, args);
			}
			//Wait for threads to finish
			SmallVector<Value *, 1> waitArgTypes;
			waitArgTypes.push_back(groupCall);
			cerr << "adding call to wait\n";
			Value *complete = builder.CreateCall(wait, waitArgTypes);
			//TODO: delete - temporary for debugging
			//Value *complete = ConstantInt::get(Type::getInt64Ty(context), 0);

			//condition on thread complete; if 0 OK, if non zero than force stop
			Value *completeCond = builder.CreateICmpEQ(complete, ConstantInt::get(Type::getInt64Ty(context), 0));

			//If thread didn't return, branch to this BB that calls a function to terminate the program. It's return will never be reached
			BasicBlock *terminate = BasicBlock::Create(context, "terminate", callingFunction);
			IRBuilder<> termBuilder(terminate);
			termBuilder.CreateCall(exit);
			if (callingFunction->getReturnType() != Type::getVoidTy(context)) {
				Value *retPtr = termBuilder.CreateAlloca(callingFunction->getReturnType());
				Value *ret = termBuilder.CreateLoad(retPtr);
				termBuilder.CreateRet(ret);
			}
			else {
				termBuilder.CreateRetVoid();
			}

			//If threads returned, delete the thread group and add in local value loads, then continue as before
			BasicBlock *cont = BasicBlock::Create(context, "continue", callingFunction);
			SmallVector<Value *, 1> releaseArgs;
			releaseArgs.push_back(groupCall);
			IRBuilder<> cleanup(cont);
			cerr << "adding call to release\n";
			cleanup.CreateCall(release, releaseArgs);
			loadAndReplaceLocals(cleanup, loopData, threadStructs, context);
			cleanup.CreateBr(loopData->getLoop()->getExitBlock());

			//cleanup old uses
			BasicBlock *toReplace = loopData->getLoop()->getExitingBlock();
			for (auto &i : loopData->getLoop()->getExitBlock()->getInstList()) {
				if (isa<PHINode>(i)) {
					PHINode *phi = cast<PHINode>(&i);
					int i;
					for (i = 0; i < 2; i++) {
						if (phi->getIncomingBlock(i) == toReplace) {
							phi->setIncomingBlock(i, cont);
						}
					}
				}
			}

			//replace all initial entries in phi nodes with the new load block in the new function
			for (auto i : loopData->getOuterLoopNonInductionPHIs()) {
				int j;
				for (j = 0; j < 2; j++) {
					BasicBlock *entryBlock = i->getIncomingBlock(j);
					bool fromLoop = false;
					for (auto &bb : loopData->getLoop()->getBlocks()) {
						if (bb == entryBlock) {
							fromLoop = true;
						}
					}
					if (!fromLoop) {
						i->setIncomingBlock(j, loadBlock);
					}
				}
			}

			//insert the branch to the IR
			builder.CreateCondBr(completeCond, cont, terminate);

			return newLoopFunc;
		}

		Value *replaceValue(unsigned int opcode, LLVMContext &context, Type *type) {
			if (Instruction::BinaryOps(opcode) == Instruction::BinaryOps::Add) {
				return ConstantInt::get(type, 0);
			}
			else if (Instruction::BinaryOps(opcode) == Instruction::BinaryOps::FAdd) {
				return ConstantFP::get(type, (double)0.0);
			}
			else if (Instruction::BinaryOps(opcode) == Instruction::BinaryOps::Mul) {
				return ConstantInt::get(type, 1);
			}
			else if (Instruction::BinaryOps(opcode) == Instruction::BinaryOps::FMul) {
				return ConstantFP::get(type, (double)1.0);
			}
			else if (Instruction::BinaryOps(opcode) == Instruction::BinaryOps::And) {
				return ConstantInt::getAllOnesValue(type);
			}
			else if (Instruction::BinaryOps(opcode) == Instruction::BinaryOps::Or) {
				return ConstantInt::get(type, 0);
			}
			else /*xor*/{
				return ConstantInt::get(type, 0);
			}
		}

		void loadAndReplaceLocals(IRBuilder<> cleanup, LoopDependencyData *loopData, list<Value *> threadStructs, LLVMContext &context) {
			//Obtain the array and local argument values required for passing to/from the function
			list<Value *> argArguments = loopData->getArgumentArgValues();
			list<Value *>  localArgumentsAndReturnVals = loopData->getReturnValues();
			map<PHINode *, pair<const Value *, Value *>> otherPhiNodes = loopData->getOtherPhiNodes();

			list<Value *> returnStructs;

			//load the return struct for each thread if we need to accumulate all values
			int structIndex = argArguments.size() + otherPhiNodes.size() + 2;
			if (loopData->getOuterLoopNonInductionPHIs().size() > 0) {
				for (auto s : threadStructs) {
					Value *structx = s;
					structx = cleanup.CreateBitOrPointerCast(structx, threadStruct->getPointerTo());
					Value *returnStructx = cleanup.CreateStructGEP(threadStruct, structx, structIndex);
					returnStructx = cleanup.CreateLoad(returnStructx);
					returnStructs.push_back(returnStructx);
				}
			}
			//alternatively just the last struct if there are only singular return values
			else if (localArgumentsAndReturnVals.size() > 0) {
				Value *structx = threadStructs.back();
				structx = cleanup.CreateBitOrPointerCast(structx, threadStruct->getPointerTo());
				Value *returnStructx = cleanup.CreateStructGEP(threadStruct, structx, structIndex);
				returnStructx = cleanup.CreateLoad(returnStructx);
				returnStructs.push_back(returnStructx);
			}
			else {
				//don't need to load anything back from the threads if no values are used later
			}

			//now load in our local variable values and update where they are used
			int retValNo = 0;
			int pos;
			Value *lastReturnStruct = returnStructs.back();
			list<PHINode *> accumulativePhiNodes = loopData->getOuterLoopNonInductionPHIs();
			for (auto retVal : localArgumentsAndReturnVals) {
				PHINode *accumulativePhi;
				bool accumulativeValue = false;
				for (auto &p : accumulativePhiNodes) {
					pos = 0;
					for (auto &op : p->operands()) {
						if (op == retVal) {
							accumulativeValue = true;
							accumulativePhi = p;
							break;
						}
						pos++;
					}
					if (accumulativeValue) {
						break;
					}
				}
				if (!accumulativeValue) {
					//replace uses of value with the value loaded from the last return struct if there isn't an associated phi node
					Value *returnedValue = cleanup.CreateStructGEP(returnStruct, lastReturnStruct, retValNo);
					returnedValue = cleanup.CreateLoad(returnedValue);
					for (auto v : loopData->getReplaceReturnValueIn(retVal)) {
						if (isa<Instruction>(v)) {
							Instruction *inst = cast<Instruction>(v);
							User::op_iterator operand = inst->op_begin();
							while (operand != inst->op_end()) {
								if (*operand == retVal) {
									//cerr << "Replacing a returned value in instruction: \n";
									//inst->dump();
									//cerr << "with\n";
									//returnedValue->dump();
									//cerr << "\n";

									*operand = returnedValue;
								}
								operand++;
							}
						}
						else {
							//replace the value???
							cerr << "DANGER REACHED HERE\n";
						}
					}
				}
				else {
					//loop through every return struct and do whatever accumulation need to be done, then replace values
					//cerr << "accumulating values\n";
					unsigned int opcode = loopData->getPhiNodeOpCode(cast<PHINode>(accumulativePhi));
					User::op_iterator operand = accumulativePhi->op_begin();
					Value *initialValue = nullptr;
					int i = 0;
					while (operand != accumulativePhi->op_end()) {
						if (!(*operand == retVal)) {
							//this is the position the initial value will be in, replace it with the identity
							initialValue = *operand;
							*operand = replaceValue(opcode, context, initialValue->getType());
							//cerr << "initial value to acc later = \n";
							//initialValue->dump();
							//cerr << "\n";
							//cerr << "and has been replaced by = \n";
							//(*operand)->dump();
							//cerr << "\n";
							break;
						}
						i++;
						operand++;
					}
					//replace entry BB
					//accumulativePhi->setIncomingBlock(i, loadBlock);

					Value *accumulatedValue = initialValue;
					for (auto retStruct : returnStructs) {
						Value *nextReturnedValue = cleanup.CreateStructGEP(returnStruct, retStruct, retValNo);
						nextReturnedValue = cleanup.CreateLoad(nextReturnedValue);
						if (!(nextReturnedValue->getType() == accumulatedValue->getType())) {
							nextReturnedValue = cleanup.CreateBitOrPointerCast(nextReturnedValue, accumulatedValue->getType());
						}
						accumulatedValue = cleanup.CreateBinOp(Instruction::BinaryOps(opcode), accumulatedValue, nextReturnedValue);
					}
					for (auto v : loopData->getReplaceReturnValueIn(retVal)) {
						if (isa<Instruction>(v)) {
							Instruction *inst = cast<Instruction>(v);
							User::op_iterator operand = inst->op_begin();
							while (operand != inst->op_end()) {
								if (*operand == retVal) {
									//cerr << "Replacing a returned value in instruction: \n";
									//inst->dump();
									//cerr << "with\n";
									//accumulatedValue->dump();
									//cerr << "\n";

									*operand = accumulatedValue;
								}
								operand++;
							}
						}
						else {
							//replace the value???
							cerr << "DANGER REACHED HERE\n";
						}
					}
				}
				retValNo++;
			}

			return;
		}

		list<Value *> loadStructValuesInFunctionForLoop(Function *loopFunction, LLVMContext &context, LoopDependencyData *loopData) {
			//Obtain the array and local argument values required for passing to/from the function
			list<Value *> argArguments = loopData->getArgumentArgValues();
			list<Value *>  localArgumentsAndReturnVals = loopData->getReturnValues();
			map<PHINode *, pair<const Value *, Value *>> otherPhiNodes = loopData->getOtherPhiNodes();
			set<Value *> externalLoads = loopData->getPrivateLoopVarUses();

			//name all values so they don't conflict with value names in the loop later
			int loadedVal = 0;
			char namePrefix[20];

			list<Value *> arrayAndLocalStructElements;

			//create IR for loading the required argument values into the function
			unsigned int p;
			IRBuilder<> loadBuilder(loadBlock);
			
			sprintf(namePrefix, "loadVal_%d", loadedVal);
			Value *castArgVal = loadBuilder.CreateBitOrPointerCast(loopFunction->arg_begin(), threadStruct->getPointerTo(), namePrefix);
			loadedVal++;
			sprintf(namePrefix, "loadVal_%d", loadedVal);

			//load argument and local instructions
			auto originalArg = argArguments.begin();
			for (p = 0; p < argArguments.size(); p++) {
				Value *arrayVal = loadBuilder.CreateStructGEP(threadStruct, castArgVal, p, namePrefix);
				loadedVal++;
				sprintf(namePrefix, "loadVal_%d", loadedVal);
				LoadInst *loadInst = loadBuilder.CreateLoad(arrayVal, namePrefix);
				loadedVal++;
				sprintf(namePrefix, "loadVal_%d", loadedVal);
				//if argument had any loads outside the loop, they need to happen here instead so the loop has access inside the function:
				for (auto u : (*originalArg)->users()) {
					list<Value *> operands;
					Instruction *toMove = nullptr;
					if ((externalLoads.find(u)) != externalLoads.end()) {
						toMove = cast<Instruction>(u);
						for (auto op : toMove->operand_values()) {
							if (op == *originalArg) {
								//replace value
								operands.push_back(loadInst);
							}
							else {
								operands.push_back(op);
							}
						}
					}
					if (toMove != nullptr) {
						//insert new instruction
						Instruction *newInst = toMove->clone();
						int i = 0;
						for (auto op : operands) {
							newInst->setOperand(i, op);
							i++;
						}
						loadBuilder.Insert(newInst);
						//replace uses of old instruction with this one
						for (auto &bb : loopData->getLoop()->getBlocks()) {
							for (auto &inst : bb->getInstList()) {
								auto toreplace = inst.op_begin();
								for (auto &op : inst.operands()) {
									if (op == toMove) {
										*toreplace = newInst;
									}
									toreplace++;
								}
							}
						}
					}
				}
				arrayAndLocalStructElements.push_back(loadInst);
				originalArg++;
			}
			map<PHINode *, pair<const Value *, Value *>>::iterator phiIt = otherPhiNodes.begin();
			for (p = argArguments.size(); p < argArguments.size() + otherPhiNodes.size(); p++) {
				Value *arrayVal = loadBuilder.CreateStructGEP(threadStruct, castArgVal, p, namePrefix);
				loadedVal++;
				sprintf(namePrefix, "loadVal_%d", loadedVal);
				LoadInst *loadInst = loadBuilder.CreateLoad(arrayVal, namePrefix);
				loadedVal++;
				sprintf(namePrefix, "loadVal_%d", loadedVal);
				PHINode *phi = phiIt->first;
				int op;
				for (op = 0; op < 2; op++) {
					//replace initial value
					BasicBlock *entryBlock = phi->getIncomingBlock(op);
					bool fromLoop = false;
					for (auto &bb : loopData->getLoop()->getBlocks()) {
						if (bb == entryBlock) {
							fromLoop = true;
						}
					}
					if (!fromLoop) {
						phi->setIncomingValue(op, loadInst);
						phi->setIncomingBlock(op, loadBlock);
						cerr << "reset a phi node start value:\n";
						phi->dump();
						loadInst->dump();
						cerr << "\n";
					}
				}
				phiIt++;
			}

			//create IR for obtaining pointers to where return values must be stored
			Value *localReturns = loadBuilder.CreateStructGEP(threadStruct, castArgVal, p + 2, namePrefix);
			loadedVal++;
			sprintf(namePrefix, "loadVal_%d", loadedVal);
			localReturns = loadBuilder.CreateLoad(localReturns, namePrefix);
			loadedVal++;
			sprintf(namePrefix, "loadVal_%d", loadedVal);
			for (p = 0; p < localArgumentsAndReturnVals.size(); p++) {
				Value *retVal = loadBuilder.CreateStructGEP(returnStruct, localReturns, p, namePrefix);
				loadedVal++;
				sprintf(namePrefix, "loadVal_%d", loadedVal);
				arrayAndLocalStructElements.push_back(retVal);
			}

			//load the start and end iteration values
			p = argArguments.size() + otherPhiNodes.size();
			Value *val = loadBuilder.CreateStructGEP(threadStruct, castArgVal, p, namePrefix);
			loadedVal++;
			sprintf(namePrefix, "loadVal_%d", loadedVal);
			LoadInst *startIt = loadBuilder.CreateLoad(val, namePrefix);
			loadedVal++;
			sprintf(namePrefix, "loadVal_%d", loadedVal);
			val = loadBuilder.CreateStructGEP(threadStruct, castArgVal, p + 1, namePrefix);
			loadedVal++;
			sprintf(namePrefix, "loadVal_%d", loadedVal);
			LoadInst *endIt = loadBuilder.CreateLoad(val, namePrefix);

			//place them in the loop
			PHINode *phi = cast<PHINode>(loopData->getInductionPhi());
			Instruction *exitCnd = loopData->getExitCondNode();
			User::op_iterator operands = phi->op_begin();
			//replace initial entry block in the phi node + place initial value
			int j;
			for (j = 0; j < 2; j++) {
				BasicBlock *entryBlock = phi->getIncomingBlock(j);
				bool fromLoop = false;
				for (auto &bb : loopData->getLoop()->getBlocks()) {
					if (bb == entryBlock) {
						fromLoop = true;
					}
				}
				if (!fromLoop) {
					phi->setIncomingBlock(j, loadBlock);
					phi->setIncomingValue(j, startIt);
					break;
				}
			}
			//replace entry BB
			operands = exitCnd->op_begin();
			operands[1] = endIt;
			
			return arrayAndLocalStructElements;
		}

		void replaceLoopValues(LoopDependencyData *loopData, LLVMContext &context, Function *loopFunction, list<Value *> loadedArrayAndLocalValues, Loop *loop, list<Value *> argValues, list<Value *> retValues) {
			list<Value *>::iterator loadedVal = loadedArrayAndLocalValues.begin();
			map<PHINode *, pair<const Value *, Value *>> otherPhiNodes = loopData->getOtherPhiNodes();

			//replace arg values
			for (auto a : argValues) {
				for (auto &bb : loop->getBlocks()) {
					for (auto &inst : bb->getInstList()) {
						User::op_iterator operand = inst.op_begin();
						while (operand != inst.op_end()) {
							if (*operand == a) {
								*operand = *loadedVal;
							}
							operand++;
						}
					}
				}
				loadedVal++;
			}

			//store each retVal in the corresponding local value position, and return from the to-be function
			BasicBlock *stores = BasicBlock::Create(context, "store", loopFunction);
			IRBuilder<> builder(stores);
			for (auto v : retValues) {
				builder.CreateStore(v, *loadedVal);
				loadedVal++;
			}
			builder.CreateRetVoid();

			Instruction *cndNode = loopData->getExitCondNode();
			for (auto u : cndNode->users()) {
				if (isa<BranchInst>(u)) {
					User::op_iterator operand = u->op_begin();
					while (operand != u->op_end()) {
						if (isa<BasicBlock>(operand)) {
							BasicBlock *toBranchTo = cast<BasicBlock>(operand);
							bool inLoop = false;
							for (auto bb : loop->getBlocks()) {
								if (toBranchTo == bb) {
									//bb in loop so not an exiting branch - don't replace
									inLoop = true;
									break;
								}
							}
							if (!inLoop) {
								//cerr << "replacing loop exit branch to new basic block\n";
								*operand = stores;
							}
						}
						operand++;
					}
				}
			}
		}

		void extractTheLoop(Loop *loop, Function *function, Function *callingFunction, LLVMContext &context) {
			BasicBlock &insertBefore = function->back();
			BasicBlock *loopEntry;
			map<Value *, Value *> valuemap;
			BasicBlock *current;
			//copy loop into new function
			int i = 0;
			for (auto &bb : loop->getBlocks()) {
				current = BasicBlock::Create(context, bb->getName() + "_", function, &insertBefore);
				if (i == 0) {
					loopEntry = current;
				}
				IRBuilder<> inserter(current);
				valuemap.insert(make_pair(bb, current));
				for (auto &instr : bb->getInstList()) {
					Instruction *inst = instr.clone();
					Instruction *inserted = inserter.Insert(inst);
					valuemap.insert(make_pair(&instr, inserted));
				}
				i++;
			}

			//replace old values with new ones in the loop
			for (auto &bb : loop->getBlocks()) {
				for (auto &inst : bb->getInstList()) {
					Instruction *newInst = cast<Instruction>(valuemap.find(&inst)->second);
					if (!isa<PHINode>(inst)) {
						User::op_iterator oldoperand = inst.op_begin();
						User::op_iterator newoperand = newInst->op_begin();
						while (oldoperand != inst.op_end()) {
							map<Value *, Value *>::iterator pos = valuemap.find(*oldoperand);
							if (pos != valuemap.end()) {
								Value *mappedOp = pos->second;
								//replace in new instruction with new value
								*newoperand = mappedOp;
							}
							oldoperand++;
							newoperand++;
						}
					}
					else {
						PHINode *phi = cast<PHINode>(&inst);
						PHINode *newPhi = cast<PHINode>(newInst);
						map<Value *, Value *>::iterator pos = valuemap.find(phi->getIncomingValue(0));
						if (pos != valuemap.end()) {
							Value *mappedOp = pos->second;
							//replace in new instruction with new value
							newPhi->setIncomingValue(0, mappedOp);
						}
						pos = valuemap.find(phi->getIncomingValue(1));
						if (pos != valuemap.end()) {
							Value *mappedOp = pos->second;
							//replace in new instruction with new value
							newPhi->setIncomingValue(1, mappedOp);
						}
						pos = valuemap.find(phi->getIncomingBlock(0));
						if (pos != valuemap.end()) {
							Value *mappedOp = pos->second;
							//replace in new instruction with new value
							newPhi->setIncomingBlock(0, cast<BasicBlock>(mappedOp));
						}
						pos = valuemap.find(phi->getIncomingBlock(1));
						if (pos != valuemap.end()) {
							Value *mappedOp = pos->second;
							//replace in new instruction with new value
							newPhi->setIncomingBlock(1, cast<BasicBlock>(mappedOp));
						}
					}
				}
			}

			//replace values to store in the last BB too
			for (auto &inst : insertBefore.getInstList()) {
				Instruction *newInst = &inst;
				User::op_iterator newoperand = newInst->op_begin();
				while (newoperand != inst.op_end()) {
					map<Value *, Value *>::iterator pos = valuemap.find(*newoperand);
					if (pos != valuemap.end()) {
						Value *mappedOp = pos->second;
						//replace in new instruction with new value
						*newoperand = mappedOp;
					}
					newoperand++;
				}
			}
			
			//create entry to the loop
			BasicBlock *loads = function->begin();
			IRBuilder<> builder(loads);
			builder.CreateBr(loopEntry);

			//remove old bb from predecessors
			insertBefore.removePredecessor(*(--loop->block_end()));

			for (auto &bb : loop->getBlocks()) {
				bb->dropAllReferences();
				for (auto &i : bb->getInstList()) {
					i.dropAllReferences();
				}
			}
			for (auto &bb : loop->getBlocks()) {
				bb->removeFromParent();
			}
		}

		void addHelperFunctionDeclarations(LLVMContext &context, Module *mod) {
			//Integer divide
			SmallVector<Type *, 2> divParamTypes1;
			divParamTypes1.push_back(Type::getInt64Ty(context));
			divParamTypes1.push_back(Type::getInt64Ty(context));
			FunctionType *intDivFunctionType1 = FunctionType::get(Type::getInt64Ty(context), divParamTypes1, false);
			mod->getOrInsertFunction("integerDivide", intDivFunctionType1);

			//create opaque type
			StructType *groupStruct = StructType::create(context, "struct.dispatch_group_s");

			//Create thread group
			SmallVector<Type *, 0> groupParamTypes;
			FunctionType *groupFunctionType = FunctionType::get(groupStruct->getPointerTo(), groupParamTypes, false);
			mod->getOrInsertFunction("createGroup", groupFunctionType);
			
			//Create async dispatch
			SmallVector<Type *, 3> asyncParamTypes;
			asyncParamTypes.push_back(groupStruct->getPointerTo());
			asyncParamTypes.push_back(Type::getInt8PtrTy(context));
			SmallVector<Type *, 3> voidParamTypes;
			voidParamTypes.push_back(Type::getInt8PtrTy(context));
			FunctionType *voidFunctionType = FunctionType::get(Type::getVoidTy(context), voidParamTypes, false);
			asyncParamTypes.push_back(voidFunctionType->getPointerTo());
			FunctionType *asyncFunctionType = FunctionType::get(Type::getVoidTy(context), asyncParamTypes, false);
			mod->getOrInsertFunction("asyncDispatch", asyncFunctionType);

			//Create wait
			SmallVector<Type *, 2> waitParamTypes;
			waitParamTypes.push_back(groupStruct->getPointerTo());
			FunctionType *waitFunctionType = FunctionType::get(Type::getInt64Ty(context), waitParamTypes, false);
			mod->getOrInsertFunction("wait", waitFunctionType);

			//Create release
			SmallVector<Type *, 1> releaseParamTypes;
			releaseParamTypes.push_back(groupStruct->getPointerTo());
			FunctionType *releaseFunctionType = FunctionType::get(Type::getVoidTy(context), releaseParamTypes, false);
			mod->getOrInsertFunction("release", releaseFunctionType);

			//Create abort
			SmallVector<Type *, 0> abortParamTypes;
			FunctionType *abortFunctionType = FunctionType::get(Type::getVoidTy(context), abortParamTypes, false);
			mod->getOrInsertFunction("abort", abortFunctionType);

			//create modulo
			SmallVector<Type *, 1> modParamTypes;
			modParamTypes.push_back(Type::getInt32Ty(context));
			FunctionType *modFunctionType = FunctionType::get(Type::getInt32Ty(context), modParamTypes, false);
			mod->getOrInsertFunction("modulo", modFunctionType);

			//create loop bounds
			SmallVector<Type *, 11> loopParamTypes;
			loopParamTypes.push_back(Type::getInt32Ty(context));
			loopParamTypes.push_back(Type::getInt32Ty(context));
			loopParamTypes.push_back(Type::getInt32Ty(context));
			loopParamTypes.push_back(Type::getInt32Ty(context));
			loopParamTypes.push_back(Type::getInt32Ty(context));
			loopParamTypes.push_back(Type::getInt32Ty(context));
			loopParamTypes.push_back(Type::getInt64Ty(context));
			loopParamTypes.push_back(Type::getInt64Ty(context));
			loopParamTypes.push_back(Type::getInt64Ty(context));
			loopParamTypes.push_back(Type::getInt64PtrTy(context));
			loopParamTypes.push_back(Type::getInt64PtrTy(context));
			FunctionType *loopFunctionType = FunctionType::get(Type::getVoidTy(context), loopParamTypes, false);
			mod->getOrInsertFunction("calcBounds", loopFunctionType);

			//create find start value
			SmallVector<Type *, 13> startParamTypes;
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt64Ty(context));
			startParamTypes.push_back(Type::getInt64Ty(context));
			startParamTypes.push_back(Type::getInt64Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32Ty(context));
			startParamTypes.push_back(Type::getInt32PtrTy(context));
			FunctionType *startFunctionType = FunctionType::get(Type::getVoidTy(context), startParamTypes, false);
			mod->getOrInsertFunction("calcStartValue6432", startFunctionType);

			//create find start value
			SmallVector<Type *, 13> startParamTypes2;
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt32Ty(context));
			startParamTypes2.push_back(Type::getInt64Ty(context));
			startParamTypes2.push_back(Type::getInt64Ty(context));
			startParamTypes2.push_back(Type::getInt64Ty(context));
			startParamTypes2.push_back(Type::getInt64Ty(context));
			startParamTypes2.push_back(Type::getInt64Ty(context));
			startParamTypes2.push_back(Type::getInt64PtrTy(context));
			FunctionType *startFunctionType2 = FunctionType::get(Type::getVoidTy(context), startParamTypes2, false);
			mod->getOrInsertFunction("calcStartValue6464", startFunctionType2);

			//create loop bounds
			SmallVector<Type *, 11> loopParamTypes32;
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32Ty(context));
			loopParamTypes32.push_back(Type::getInt32PtrTy(context));
			loopParamTypes32.push_back(Type::getInt32PtrTy(context));
			FunctionType *loopFunctionType32 = FunctionType::get(Type::getVoidTy(context), loopParamTypes32, false);
			mod->getOrInsertFunction("calcBounds32", loopFunctionType32);

			//create find start value
			SmallVector<Type *, 13> startParamTypes32;
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32Ty(context));
			startParamTypes32.push_back(Type::getInt32PtrTy(context));
			FunctionType *startFunctionType32 = FunctionType::get(Type::getVoidTy(context), startParamTypes32, false);
			mod->getOrInsertFunction("calcStartValue3232", startFunctionType32);

			//create find start value
			SmallVector<Type *, 13> startParamTypes322;
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt32Ty(context));
			startParamTypes322.push_back(Type::getInt64Ty(context));
			startParamTypes322.push_back(Type::getInt64Ty(context));
			startParamTypes322.push_back(Type::getInt64PtrTy(context));
			FunctionType *startFunctionType322 = FunctionType::get(Type::getVoidTy(context), startParamTypes322, false);
			mod->getOrInsertFunction("calcStartValue3264", startFunctionType322);
		}

		virtual bool runOnModule(Module &M) override {

			for (Function &F : M.functions()) {
				if (!F.isDeclaration() && !F.hasFnAttribute("Extracted") && noThreads >= DEFAULT_THREAD_COUNT) {
					//get data from the IsParallelizableLoopPass analysis
					IsParallelizableLoopPass &IP = getAnalysis<IsParallelizableLoopPass>(F);
					list<LoopDependencyData *> loopData = IP.getResultsForFunction(F);
					cerr << "Extraction in function " << (F.getName()).data() << "\n";
					Module *mod = (F.getParent());
					LLVMContext &context = mod->getContext();
					addHelperFunctionDeclarations(context, mod);
					for (list<LoopDependencyData *>::iterator i = loopData.begin(); i != loopData.end(); i++) {
						LoopDependencyData *loopData = *i;
						int noSubloops = 0;
						vector<Loop *> currentSubloops = loopData->getLoop()->getSubLoops();
						while (currentSubloops.size() != 0) {
							Loop *next = *currentSubloops.begin();
							currentSubloops = next->getSubLoops();
							noSubloops++;
						}
						if (loopData->getTripCount() > noThreads * 100 || noSubloops >= 2) {
							if ((loopData->getDependencies()).size() == 0) {
								if (loopData->isParallelizable()) {
									extract(F, loopData, context);
								}
							}
						}
						else {
							cerr << "trip count not large enough for parallelization\n";
						}
					}
					cerr << "Loop extraction for function complete\n";
				}
			}
			return true;
		}

	};
}

char LoopExtractionPass::ID = 1;
INITIALIZE_PASS_BEGIN(LoopExtractionPass, "parallelizable-loops",
                "Parallelize loops", false, false)
INITIALIZE_PASS_DEPENDENCY(IsParallelizableLoopPass)
INITIALIZE_PASS_END(LoopExtractionPass, "parallelizable-loops",
                "Parallelize loops", false, false)

/* static RegisterPass<LoopExtractionPass> reg2("LoopExtractionPass",
	"Extracts loops into functions that can be called in separate threads for parallelization"); */

ModulePass *parallelize::createParallelizationPass() {
	return new LoopExtractionPass();
}
