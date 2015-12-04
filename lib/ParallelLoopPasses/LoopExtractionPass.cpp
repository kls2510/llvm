#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
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
							cerr << "This loop has no dependencies so can be extracted\n";
							//find no of iterations and the start iteration value
							Instruction *inst2 = (loopData->getLoop())->getHeader()->begin();
							Value *noIterations;
							while (loopData->getLoop()->contains(inst2)) {
								cerr << (inst2->getValueName())->getKeyData() << "\n";
								if (strcmp((inst2->getValueName())->getKeyData(),"%exitcond")) {
									int noOperands = inst2->getNumOperands();
									noIterations = (inst2->getOperand(noOperands - 1));
								}
							}
							Value *startIt = ((loopData->getLoop())->getCanonicalInductionVariable())->getIncomingValue(0);

							//get pointer to the basic block we'll insert the new instructions into
							BasicBlock *insertPos = ((loopData->getLoop())->getLoopPredecessor());

							//create the struct we'll use to pass data to/from the threads
							StructType *myStruct = StructType::create(context, "ThreadPasser");
							//send the startItvalue, no of iterations, Total number of threads and thread number
							Type *elts[] = { startIt->getType(), noIterations->getType(), Type::getInt32Ty(context), Type::getInt32Ty(context) };
							myStruct->setBody(elts);

							//setup for inserting instructions before the loop
							Instruction *inst = insertPos->begin();
							IRBuilder<> builder(inst);
							list<Value *> threadStructs;
							for (int i = 0; i < noThreads; i++) {
								//initialise struct in a new instruction at the end of the basic block
								AllocaInst *allocateInst = builder.CreateAlloca(myStruct);
								//store startIt
								Value *getPTR = builder.CreateStructGEP(myStruct, allocateInst, 0);
								builder.CreateStore(startIt, getPTR);
								//store numIt
								getPTR = builder.CreateStructGEP(myStruct, allocateInst, 1);
								builder.CreateStore(noIterations, getPTR);
								//store total num threads
								getPTR = builder.CreateStructGEP(myStruct, allocateInst, 2);
								builder.CreateStore(ConstantInt::get(Type::getInt32Ty(context), noThreads), getPTR);
								//store this one's thread number
								getPTR = builder.CreateStructGEP(myStruct, allocateInst, 3);
								builder.CreateStore(ConstantInt::get(Type::getInt32Ty(context), i), getPTR);
								//load the struct for passing into the function
								LoadInst *loadInst = builder.CreateLoad(allocateInst);
								threadStructs.push_back(loadInst);
							}

							//extract the loop into a new function
							CodeExtractor extractor = CodeExtractor(DT, *(loopData->getLoop()), false);
							Function *extractedLoop = extractor.extractCodeRegion();

							if (extractedLoop != 0) {
								CallInst *callInst = dyn_cast<CallInst>(*(extractedLoop->user_begin()));
								int noOps = callInst->getNumArgOperands();
								vector<Value *> args;
								for (int i = 0; i < noOps; i++) {
									args.push_back(callInst->getOperand(i));
								}
								IRBuilder<> callbuilder(callInst);

								//create a new function with added argument types
								Module * mod = (F.getParent());
								vector<Type *> paramTypes;
								for (int i = 0; i < (extractedLoop->getFunctionType())->getNumParams(); i++) {
									paramTypes.push_back((extractedLoop->getFunctionType())->getParamType(i));
								}
								paramTypes.push_back(myStruct);
								ArrayRef<Type *> types(paramTypes);
								FunctionType *FT = FunctionType::get(extractedLoop->getFunctionType()->getReturnType(), types, false);
								string name = (extractedLoop->getName()).str() + "_";
								Function *newLoopFunc = Function::Create(FT, Function::ExternalLinkage, name, mod);
								cerr << "New function created\n";
								//Constant *c = mod->getOrInsertFunction(name, FT);
								//Function *newLoopFunc = cast<Function>(c);

								//insert calls to this new function
								for (list<Value*>::iterator it = threadStructs.begin(); it != threadStructs.end(); ++it) {
									vector<Value *> argsForCall = args;
									argsForCall.push_back(*it);
									ArrayRef<Value *> args(argsForCall);
									callbuilder.CreateCall(newLoopFunc, args);
									cerr << "New function call created\n";
								}

								//delete the original call instruction
								callInst->eraseFromParent();

								//clone old function into this new one that takes the correct amount of arguments
								ValueToValueMapTy vvmap;
								Function::ArgumentListType &args1 = extractedLoop->getArgumentList();
								Function::ArgumentListType &args2 = newLoopFunc->getArgumentList();
								Argument *arg = (args2.begin());
								for (SymbolTableList<Argument>::iterator i = args1.begin(); i != args1.end(); ++i) {
									vvmap.insert(pair<Value*, Value*>(cast<Value>(i), cast<Value>(arg)));
									arg = args2.getNext(arg);
								}
								SmallVector<ReturnInst *, 0> returns;
								CloneFunctionInto(newLoopFunc, extractedLoop, vvmap, false, returns, "");

								//Debug
								cerr << "Original function rewritten to:\n";
								F.dump();
								cerr << "with new function:\n";
								newLoopFunc->dump();
								//Mark the function to avoid infinite extraction
								//extractedLoop->removeFromParent();
								newLoopFunc->addFnAttr("Extracted", "true");
								extractedLoop->addFnAttr("Extracted", "true");
								F.addFnAttr("Extracted", "true");
								//extractedLoop->eraseFromParent();
							}

						}
						else {
							//must be a problem with Phi nodes
							cerr << "loop has too many PHI nodes, so cannot be parallelized right now\n";
						}
					}
					//must be a problem with dependencies
					else {
						cerr << "This loop has interloop dependencies so cannot be parallelized right now\n";
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
