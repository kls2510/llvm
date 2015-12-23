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
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ParallelLoopPasses/IsParallelizableLoopPass.h"
#include <iostream>
#include <string>
#include <set>

using namespace llvm;
using namespace std;
using namespace parallelize;

namespace {
	/*

	*/
	struct LoopExtractionPass : public FunctionPass {
		int noThreads = 2;

		//ID of the pass
		static char ID;

		//Constructor
		LoopExtractionPass() : FunctionPass(ID) {}

		//Set LoopInfo pass to run before this one so we can access its results
		void getAnalysisUsage(AnalysisUsage &AU) const {
			AU.addRequired<IsParallelizableLoopPass>();
			AU.addRequired<ScalarEvolutionWrapperPass>();
			AU.addRequired<DominatorTreeWrapperPass>();
		}

		virtual bool runOnFunction(Function &F) {
			//get data from the IsParallelizableLoopPass analysis
			IsParallelizableLoopPass &IP = getAnalysis<IsParallelizableLoopPass>();
			ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
			DominatorTree &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();

			if (!F.hasFnAttribute("Extracted")) {
				list<LoopDependencyData *> loopData = IP.getResultsForFunction(F);
				cerr << "In function " << (F.getName()).data() << "\n";
				for (list<LoopDependencyData *>::iterator i = loopData.begin(); i != loopData.end(); i++) {
					cerr << "Found a loop\n";
					LoopDependencyData *loopData = *i;
					LLVMContext &context = (loopData->getLoop())->getHeader()->getContext();

					if ((loopData->getDependencies()).size() == 0) {
						if (loopData->isParallelizable()) {
							//extract the loop
							//cerr << "This loop has no dependencies so can be extracted\n";

							//get start and end of loop
							Value *startIt = loopData->getStartIt();
							Value *finalIt = loopData->getFinalIt();

							//extract the loop into a new function
							CodeExtractor extractor = CodeExtractor(DT, *(loopData->getLoop()), false);
							Function *extractedLoop = extractor.extractCodeRegion();

							if (extractedLoop != 0) {
								//cerr << "loop extracted successfully\n";

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
								Value *iterationsEach = setupBuilder.CreateExactSDiv(noIterations, ConstantInt::get(Type::getInt64Ty(context), noThreads));
								//cerr << "setting up threads\n";
								for (int i = 0; i < noThreads; i++) {
									Value *threadStartIt;
									Value *endIt;
									Value *startItMult = builder.CreateBinOp(Instruction::Mul, iterationsEach, ConstantInt::get(Type::getInt64Ty(context), i));
									//cerr << "here\n";
									threadStartIt = builder.CreateBinOp(Instruction::Add, loadedStartIt, startItMult);
									if (i == (noThreads - 1)) {
										endIt = builder.CreateLoad(end);
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
								//cerr << "threads setup\n";

								//cerr << "creating new function\n";
								//create a new function with added argument types
								Module * mod = (F.getParent());
								SmallVector<Type *, 8> paramTypes;
								paramTypes.push_back((threadStructs.front())->getType());
								FunctionType *FT = FunctionType::get(extractedLoop->getFunctionType()->getReturnType(), paramTypes, false);
								string name = "_" + (extractedLoop->getName()).str() + "_";
								Function *newLoopFunc = Function::Create(FT, Function::ExternalLinkage, name, mod);

								//cerr << "inserting new function calls\n";
								//insert calls to this new function - for now just call function, add threads later
								//TODO: may need to find the name of the thread module
								ValueSymbolTable &symTab = mod->getValueSymbolTable();
								Function *createGroup = cast<Function>(symTab.lookup(StringRef("createGroup")));
								Function *asyncDispatch = cast<Function>(symTab.lookup(StringRef("asyncDispatch")));
								Function *wait = cast<Function>(symTab.lookup(StringRef("wait")));
								Function *release = cast<Function>(symTab.lookup(StringRef("release")));

								/*Value *queueValue = symTab.lookup(StringRef("DISPATCH_QUEUE_CONCURRENT"));
								GlobalVariable *queue = mod->getGlobalVariable(StringRef("dispatch_queue_t"));
								Type *queueType = (queue->getInitializer())->getType();
								SmallVector<Type *, 2> queueParamTypes;
								queueParamTypes.push_back(Type::getInt8PtrTy(context));
								queueParamTypes.push_back(queueValue->getType());
								FunctionType *queueCreateType = FunctionType::get(queueType, queueParamTypes, false);
								Constant *queueCreate = mod->getOrInsertFunction("dispatch_queue_create", queueCreateType);
								Function *queueCreateFunction = cast<Function>(queueCreate);

								GlobalVariable *group = mod->getGlobalVariable(StringRef("dispatch_group_t"));
								Type *groupType = (group->getInitializer())->getType();
								SmallVector<Type *, 1> groupParamTypes;
								groupParamTypes.push_back(Type::getVoidTy(context));
								FunctionType *groupCreateType = FunctionType::get(groupType, groupParamTypes, false);
								Constant *groupCreate = mod->getOrInsertFunction("dispatch_group_create", groupCreateType);
								Function *groupCreateFunction = cast<Function>(groupCreate);

								GlobalVariable *func = mod->getGlobalVariable(StringRef("dispatch_function_t"));
								Type *funcType = (func->getInitializer())->getType();
								SmallVector<Type *, 4> dispatchParamTypes;
								dispatchParamTypes.push_back(cast<Type>(groupType));
								dispatchParamTypes.push_back(cast<Type>(queueType));
								dispatchParamTypes.push_back(ArrayType::get(Type::getVoidTy(context),1));
								dispatchParamTypes.push_back(cast<Type>(funcType));
								FunctionType *dispatchCallType = FunctionType::get(Type::getVoidTy(context), dispatchParamTypes, false);
								Constant *dispatchCall = mod->getOrInsertFunction("dispatch_group_async_f", dispatchCallType);
								Function *dispatchCallFunction = cast<Function>(dispatchCall);

								Value *timeValue = symTab.lookup(StringRef("DISPATCH_TIME_FOREVER"));
								SmallVector<Type *, 2> waitParamTypes;
								waitParamTypes.push_back(groupType);
								waitParamTypes.push_back(timeValue->getType());
								FunctionType *waitType = FunctionType::get(Type::getInt64Ty(context), waitParamTypes, false);
								Constant *wait = mod->getOrInsertFunction("dispatch_group_wait", waitType);
								Function *waitFunction = cast<Function>(wait); */

								Value *groupCall = builder.CreateCall(createGroup, SmallVector<Value *, 0>());
								/* SmallVector<Value *, 2> queueArgTypes;
								Value *arr = ConstantDataArray::getString(context, StringRef("concQueue"));
								queueArgTypes.push_back(arr);
								queueArgTypes.push_back(queueValue);
								Value *queueCall = builder.CreateCall(queueCreateFunction, queueArgTypes); */
								for (list<Value*>::iterator it = threadStructs.begin(); it != threadStructs.end(); ++it) {
									//SmallVector<Value *,8> argsForCall;
									//argsForCall.push_back(*it);
									SmallVector<Value *, 3> argsForDispatch;
									argsForDispatch.push_back(groupCall);
									argsForDispatch.push_back(*it);
									argsForDispatch.push_back(newLoopFunc);
									//builder.CreateCall(newLoopFunc, argsForCall);
									builder.CreateCall(asyncDispatch, argsForDispatch);
								}
								SmallVector<Value *, 2> waitArgTypes;
								waitArgTypes.push_back(groupCall);
								waitArgTypes.push_back(ConstantInt::get(Type::getInt64Ty(context), 10000));
								Value *complete = builder.CreateCall(wait, waitArgTypes);
								//TODO: add condition on complete; if 0 OK, if non zero than force stop
								SmallVector<Value *, 1> releaseArgs;
								releaseArgs.push_back(groupCall);
								builder.CreateCall(release, releaseArgs);

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
									//load each struct element at the start of the function
									//cerr << "here\n";
									//Value *mapVal = loadBuilder.CreateStructGEP(myStruct, castVal, p);
									Value *mapVal = loadBuilder.CreateStructGEP(myStruct, castArgVal, p);
									//cerr << "here\n";
									LoadInst *loadInst = loadBuilder.CreateLoad(mapVal);
									//structElements.push_back(loadInst);
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
								//need to return changed local values but for now return nothing
								SmallVector<ReturnInst *, 0> returns;
								//cerr << "cloning\n";

								CloneFunctionInto(newLoopFunc, extractedLoop, vvmap, false, returns, "");
								//bridge first bb to cloned bbs
								loadBuilder.CreateBr((newLoopFunc->begin())->getNextNode());

								//Replace values with new values in function
								
								//cerr << "replacing old function values\n";
								
								bool startFound = false;
								bool endFound = false;
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
										if (!endFound) {
											if (strcmp((i.getName()).data(), "exitcond") == 0) {
												User::op_iterator operands = i.op_begin();
												operands[1] = *element++;
												endFound = true;
											}
										}
									}
								}

								//Debug
								cerr << "Original function rewritten to:\n";
								F.dump();
								cerr << "with new function:\n";
								newLoopFunc->dump();
								//Mark the function to avoid infinite extraction
								newLoopFunc->addFnAttr("Extracted", "true");
								extractedLoop->addFnAttr("Extracted", "true");
								F.addFnAttr("Extracted", "true");
							}

						}
						else {
							//must be a problem with Phi nodes
							//cerr << "loop has too many PHI nodes, so cannot be parallelized right now\n";
						}
					}
					//must be a problem with dependencies
					else {
						//cerr << "This loop has interloop dependencies so cannot be parallelized right now\n";
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
