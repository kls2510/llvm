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

					if ((loopData->getDependencies()).size() == 0) {
						if (loopData->isParallelizable()) {
							//extract the loop
							cerr << "This loop has no dependencies so can be extracted\n";
							//find no of iterations and the start iteration value
							int noIterations = SE.getSmallConstantTripCount(loopData->getLoop());
							cerr << "No of Iterations = " << noIterations << "\n";
							if (noIterations == 0) {
								//TODO: Need to work out how to do this if maxIter is a variable value
							}
							Value *startIt = ((loopData->getLoop())->getCanonicalInductionVariable())->getIncomingValue(0);
							bool exact = (noIterations % noThreads == 0);

							//get pointer to the basic block we'll insert the new instructions into
							BasicBlock *insertPos = ((loopData->getLoop())->getLoopPredecessor());
							LLVMContext &context = insertPos->getContext();

							//create the struct we'll use to pass data to/from the threads
							StructType *myStruct = StructType::create(context, "ThreadPasser");
							//send the startItvalue, begin offset and end offset to each thread
							Type *elts[] = { startIt->getType(), Type::getInt32Ty(context), Type::getInt32Ty(context) };
							myStruct->setBody(elts);

							//setup for inserting instructions before the loop
							Instruction *inst = insertPos->begin();
							IRBuilder<> builder(inst);
							list<Value *> threadStructs;
							for (int i = 0; i < noThreads; i++) {
								int firstIterNo = i*(noIterations / noThreads);
								int lastIterNo = firstIterNo + ((noIterations / noThreads) - 1);
								if ((i == noThreads - 1) && !exact) {
									lastIterNo = (noIterations - 1);
								}
								cerr << "Alloc thread " << (i + 1) << " iterations: (startIt + " << firstIterNo << ") to (startIt + " << lastIterNo << ")\n";
								//initialise struct in a new instruction at the end of the basic block
								AllocaInst *allocateInst = builder.CreateAlloca(myStruct);
								//store startIt
								Value *getPTR = builder.CreateStructGEP(myStruct, allocateInst, 0);
								builder.CreateStore(startIt, getPTR);
								//store firstIterOffset
								getPTR = builder.CreateStructGEP(myStruct, allocateInst, 1);
								builder.CreateStore(ConstantInt::get(Type::getInt32Ty(context), firstIterNo), getPTR);
								//store lastIterOffset
								getPTR = builder.CreateStructGEP(myStruct, allocateInst, 2);
								builder.CreateStore(ConstantInt::get(Type::getInt32Ty(context), lastIterNo), getPTR);
								threadStructs.push_back(allocateInst);
							}

							//extract the loop into a new function
							CodeExtractor extractor = CodeExtractor(DT, *(loopData->getLoop()), false);
							Function *extractedLoop = extractor.extractCodeRegion();

							//add struct argument to function
							Argument *newArg = new Argument(myStruct, "iterationHolder", extractedLoop);

							cerr << "Function no of Args: " << (extractedLoop->getArgumentList()).size() << "\n";
							cerr << "Types of args:\n";
							for (Function::ArgumentListType::iterator i = (extractedLoop->getArgumentList()).begin(); i != (extractedLoop->getArgumentList()).end(); ++i) {
								cerr << "[" << i << "] : " << ((*i).getType()) << "\n";
							}

							//edit calls to add struct argument
							CallInst *callInst = dyn_cast<CallInst>(*(extractedLoop->user_begin()));
							vector<Value *> args(callInst->value_op_begin(), callInst->value_op_end());
							IRBuilder<> callbuilder(callInst);
							for (list<Value*>::iterator it = threadStructs.begin(); it != threadStructs.end(); ++it) {
								vector<Value *> argsForCall = args;
								argsForCall.push_back(*it);
								callbuilder.CreateCall(extractedLoop, argsForCall);
							}
							//delete the original call instruction
							callInst->eraseFromParent();

							//Debug
							cerr << "rewritten to:\n";
							for (Function::iterator bb = F.begin(); bb != F.end(); ++bb) {
								bb->dump();
								for (BasicBlock::iterator i = bb->begin(); i != bb->end(); i++) {
									i->dump();
								}
							}

							if (extractedLoop != 0) {
								cerr << "with new function:\n";
								extractedLoop->dump();
								for (Function::iterator bb = extractedLoop->begin(); bb != extractedLoop->end(); ++bb) {
									for (BasicBlock::iterator i = bb->begin(); i != bb->end(); i++) {
										i->dump();
									}
								}
								//Mark the function to avoid infinite extraction
								//extractedLoop->removeFromParent();
								extractedLoop->addFnAttr("Extracted", "true");
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
