#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include <iostream>

using namespace llvm;
using namespace std;

namespace {
	/*
		IsParallelizablePass detects loops in a function's IR and determines whether
		each is parallelizable (including already or after some transform) or not
	*/
	struct IsParallelizableLoopPass : public FunctionPass {
		//ID of the pass
		static char ID;

		//DependenceAnalysis class
		DependenceAnalysis *DA;

		//AliasAnalysis class
		AliasAnalysis *AA;

		//Constructor
		IsParallelizableLoopPass() : FunctionPass(ID) {}

		//Set LoopInfo pass to run before this one so we can access its results
		void getAnalysisUsage(AnalysisUsage &AU) const {
			AU.addRequired<LoopInfoWrapperPass>();
			AU.addRequired<DependenceAnalysis>();
			AU.addRequired<AAResultsWrapperPass>();
			//this pass is just analysis and so does not change any other analysis results
			AU.setPreservesAll();
		}

		virtual bool runOnFunction(Function &F) {
			//get data from the loopInfo analysis
			LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
			DA = &getAnalysis<DependenceAnalysis>();
			AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();

			cout << "Running parallelizable loop analysis on function " << (F.getName()).data() << "\n";

			//initialize iterators and loop counter
			LoopInfo::iterator i = LI.begin();
			LoopInfo::iterator e = LI.end();
			int LoopCounter = 1;

			//iterate through all the OUTER loops found and run anaysis to see whether they are parallelizable
			while (i != e) {
				Loop *L = *i;
				cout << "Found loop " << LoopCounter << "\n";
				//for now just dump the contents of the loop
				//L->dump();
				//call the function that will be implemented to analyse the code
				if (isParallelizable(L)) {
					cout << "this loop is parallelizable\n";
				}
				else {
					cout << "this loop is not parallelizable\n";
				}
				i++;
			}
			return false;
		}

	private:
		//runs the actual analysis
		bool isParallelizable(Loop *L) {
			//default for now
			cout << "this loop has " << (L->getSubLoops()).size() <<" subloops\n";
			//case: simple loop with no nested loops
			if ((L->getSubLoops()).size() == 0) {
				//loop through the loops basic blocks
				for (Loop::block_iterator bb = L->block_begin(); bb != L->block_end(); bb++) {
					//loop through the basic blocks instructions to check for aliasing and dependencies
					for (BasicBlock::iterator inst = ((*bb)->getIterator())->begin(); inst != ((*bb)->getIterator())->end(); inst++) {
						cout << "Dependencies for instruction: ";
						inst->dump();
						BasicBlock::iterator inst2 = inst;
						int depCounter = 0;
						for (++inst2; inst2 != ((*bb)->getIterator())->end(); inst2++) {
							//check for dependencies between instructions
							unique_ptr<Dependence> dependence = DA->depends(inst, inst2, false);
							if (dependence) {
								depCounter++;
								cout << "With " << (dependence->getDst()) << " direction " << (dependence->getDirection(0)) << " and distance " << (dependence->getDistance(0)) << " at level 0\n";
							}
							//also check for aliasing within instructions
						}
						
						/* for (Value::use_iterator i = inst->use_begin(); i != inst->use_end(); i++) {
							//for now just dump instruction that depends on the write if it is in the loop
							Value *v = i->get();
							v->dump();
						} */
					}
				}
			}
			return false;
		}
	};
}

char IsParallelizableLoopPass::ID = 0;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
