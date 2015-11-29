#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
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
		}

		virtual bool runOnFunction(Function &F) {
			//get data from the IsParallelizableLoopPass analysis
			IsParallelizableLoopPass &IP = getAnalysis<IsParallelizableLoopPass>();
			ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();

			list<LoopDependencyData *> loopData = IP.getResultsForFunction(F);
			cout << "In function " << (F.getName()).data() << "\n";
			for (list<LoopDependencyData *>::iterator i = loopData.begin(); i != loopData.end(); i++) {
				cout << "Found a loop\n";
				LoopDependencyData *loopData = *i;
				
				if (loopData->getNoOfPhiNodes() <= 1) {
					if ((loopData->getDependencies()).size() > 0) {
						for (list<Dependence *>::iterator i = (loopData->getDependencies()).begin(); i != (loopData->getDependencies()).end(); i++) {
							int distance = loopData->getDistance(*i);
							Instruction *inst1 = (*i)->getSrc();
							Instruction *inst2 = (*i)->getDst();
							cout << "Dependency between\n";
							inst1->dump();
							inst2->dump();
							cout << "with distance = " << distance << "\n";
						}
						cout << "This loop has interloop dependencies so cannot be parallelized right now\n";
					}
					else {
						//extract the loop
						cout << "This loop has no dependencies so can be extracted\n";
						//find no of iterations and the start iteration value
						int noIterations = SE.getSmallConstantTripCount(loopData->getLoop());
						cout << "No of Iterations = " << noIterations << "\n";
						Value *startIt = ((loopData->getLoop())->getCanonicalInductionVariable())->getIncomingValue(0);
						bool exact = (noIterations % noThreads == 0);

						//get pointer to the basic block we'll insert the new instructions into
						BasicBlock *insertPos = ((loopData->getLoop())->getLoopPredecessor());
						LLVMContext &context = insertPos->getContext();

						//create the struct we'll use to pass data to/from the threads
						StructType *myStruct = StructType::create(context, "ThreadPasser");
						//send the startItvalue, begin offset and end offset to each thread
						Type *elts[] = { startIt->getType(), Type::getInt64Ty(context), Type::getInt64Ty(context) };
						myStruct->setBody(elts);

						//setup for inserting instructions before the loop
						Instruction *inst = insertPos->end();
						IRBuilder<> builder(inst);

						for (int i = 0; i < noThreads; i++) {
							int firstIterNo = i*(noIterations / noThreads);
							int lastIterNo = firstIterNo + ((noIterations / noThreads) - 1);
							if ((i == noThreads - 1) && !exact) {
								lastIterNo = (noIterations - 1);
							}
							cout << "Alloc thread " << (i + 1) << " iterations: (startIt + " << firstIterNo << ") to (startIt + " << lastIterNo << ")\n";
							//initialise struct in a new instruction at the end of the basic block
							AllocaInst *allocateInst = builder.CreateAlloca(myStruct);
							//store startIt
							Value *getPTR = builder.CreateStructGEP(myStruct, allocateInst, 0);
							builder.CreateStore(startIt,getPTR);
							//store firstIterOffset
							getPTR = builder.CreateStructGEP(myStruct, allocateInst, 1);
							builder.CreateStore(ConstantInt::get(Type::getInt64Ty(context), firstIterNo), getPTR);
							//store lastIterOffset
							getPTR = builder.CreateStructGEP(myStruct, allocateInst, 2);
							builder.CreateStore(ConstantInt::get(Type::getInt64Ty(context), lastIterNo), getPTR);
						}
					}
				}
				else {
					cout << "loop has multiple PHI nodes, so cannot be parallelized right now\n";
				}
			}
			
			cout << "Loop extraction for function complete\n";
			return true;
		}

	};
}

char LoopExtractionPass::ID = 1;
static RegisterPass<LoopExtractionPass> reg2("LoopExtractionPass",
	"Extracts loops into functions that can be called in separate threads for parallelization");
