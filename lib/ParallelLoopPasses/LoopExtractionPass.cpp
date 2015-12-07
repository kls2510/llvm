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

							//get pointer to the basic block we'll insert the new instructions into
							BasicBlock *insertPos = ((loopData->getLoop())->getLoopPredecessor());

							//extract the loop into a new function
							CodeExtractor extractor = CodeExtractor(DT, *(loopData->getLoop()), false);
							Function *extractedLoop = extractor.extractCodeRegion();

							if (extractedLoop != 0) {
								//for now - change when we include non-canonicalised loops
								int noIterations = SE.getSmallConstantTripCount(loopData->getLoop());
								Value *startIt = ((loopData->getLoop())->getCanonicalInductionVariable())->getIncomingValue(0);

								//create the struct we'll use to pass data to/from the threads
								StructType *myStruct = StructType::create(context, "ThreadPasser");
								//send the: data required, startItvalue and endIt value
								vector<Type *> elts;

								//setup for inserting instructions before the loop
								Instruction *inst = insertPos->begin();
								IRBuilder<> builder(inst);
								list<Value *> threadStructs;
								Value *iterationsEach = builder.CreateExactSDiv(ConstantInt::get(Type::getInt32Ty(context), noIterations), ConstantInt::get(Type::getInt32Ty(context), noThreads));
								Value *threadStartIt;
								Value *endIt;
								//setup struct type
								CallInst *callInst = dyn_cast<CallInst>(*(extractedLoop->user_begin()));
								int noOps = callInst->getNumArgOperands();
								for (int i = 0; i < noOps; i++) {
									elts.push_back(callInst->getOperand(i)->getType());
								}
								elts.push_back(threadStartIt->getType());
								elts.push_back(endIt->getType());
								myStruct->setBody(elts);

								for (int i = 0; i < noThreads; i++) {
									Value *startItMult = builder.CreateMul(iterationsEach, ConstantInt::get(Type::getInt32Ty(context), i));
									Value *threadStartIt = builder.CreateAdd(startIt, startItMult);
									if (i == (noThreads - 1)) {
										endIt = builder.CreateAdd(threadStartIt, ConstantInt::get(Type::getInt32Ty(context), noIterations));
									}
									else {
										endIt = builder.CreateAdd(threadStartIt, iterationsEach);
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
									//load the struct for passing into the function
									LoadInst *loadInst = builder.CreateLoad(allocateInst);
									threadStructs.push_back(loadInst);
								}

								IRBuilder<> callbuilder(callInst);

								//create a new function with added argument types
								Module * mod = (F.getParent());
								SmallVector<Type *, 8> paramTypes;
								paramTypes.push_back(myStruct);
								FunctionType *FT = FunctionType::get(extractedLoop->getFunctionType()->getReturnType(), paramTypes, false);
								string name = "_" + (extractedLoop->getName()).str() + "_";
								Function *newLoopFunc = Function::Create(FT, Function::ExternalLinkage, name, mod);

								//insert calls to this new function - for now just call function, add threads later
								for (list<Value*>::iterator it = threadStructs.begin(); it != threadStructs.end(); ++it) {
									SmallVector<Value *,8> argsForCall;
									argsForCall.push_back(*it);
									callbuilder.CreateCall(newLoopFunc, argsForCall);
								}

								//delete the original call instruction
								callInst->eraseFromParent();

								//clone old function into this new one that takes the correct amount of arguments
								ValueToValueMapTy vvmap;
								Function::ArgumentListType &args1 = extractedLoop->getArgumentList();
								Function::ArgumentListType &args2 = newLoopFunc->getArgumentList();
								StructType *structArg = dyn_cast<StructType *>(args2);
								vector<Type *> body = structArg->elements();
								vector<Type *>::iterator it = body.begin();
								for (auto &i : args1) {
									vvmap.insert(std::make_pair(cast<Value>(&i), cast<Value>((*it))));
									it++;
								}
								//need to return changed local values but for now return nothing
								SmallVector<ReturnInst *, 0> returns;
								CloneFunctionInto(newLoopFunc, extractedLoop, vvmap, false, returns, "");


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
