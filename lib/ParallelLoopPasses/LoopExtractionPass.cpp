#include "llvm/IR/LLVMContext.h"
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
				//(*i)->print();
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
						cout << "This loop has no dependencies so can be extracted\n";
						//begin extraction
						int noIterations = SE.getSmallConstantTripCount(loopData->getLoop());
						cout << "No of Iterations = " << noIterations << "\n";
						ConstantInt * startItConst = dyn_cast<ConstantInt>(((loopData->getLoop())->getCanonicalInductionVariable())->getIncomingValue(0));
						int start = startItConst->getSExtValue();
						cout << "Start iteration = " << start << "\n";
						bool exact = (noIterations % noThreads == 0);
						for (int i = 0; i < noThreads; i++) {
							int firstIterNo = start + i*(noIterations / noThreads);
							int lastIterNo = firstIterNo + ((noIterations / noThreads) - 1);
							if (i == noThreads - 1 && !exact) {
								lastIterNo = start + (noIterations - 1);
							}
							cout << "Alloc thread " << (i + 1) << " iterations " << firstIterNo << " to " << lastIterNo << "\n";
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
