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
			cout << "this loop has " << (L->getSubLoops()).size() << " subloops\n";
			//case: simple loop with no nested loops
			if ((L->getSubLoops()).size() == 0) {
				//look as the phi node and carry out analysis from there
				PHINode *phi = L->getCanonicalInductionVariable();
				StringRef inductionVariable = phi->getName();
				//loop through instructions dependendent on the induction variable and check to see whether
				//there are interloop dependencies
				for (Instruction::user_iterator pui = phi->user_begin(); pui != phi->user_end(); pui++) {
					Instruction *dependency = dyn_cast<Instruction>(*pui);
					cout << "found instruction dependent on induction variable at:\n";
					dependency->dump();
					//check to see whether the instruction manipulates the value of the IV in any way
					if (dependency->getOperand(1)->getName() == inductionVariable) {
						cout << "and this instruction may pass a manipulated version somewhere else\n";
					}
					//if so, look for instructions dependent on that instruction's value
					
				}
			}
			return false;
		}


				/* for (Loop::block_iterator bb = L->block_begin(); bb != L->block_end(); bb++) {
					//loop through the basic blocks instructions to check for aliasing and dependencies
					for (BasicBlock::iterator inst = ((*bb)->getIterator())->begin(); inst != ((*bb)->getIterator())->end(); inst++) {
						cout << "Dependencies for instruction: \n";
						inst->dump();
						int depCount = 1;
						
						for (Instruction::user_iterator ui = inst->user_begin(); ui != inst->user_end(); ui++) {
							Instruction *dependency = dyn_cast<Instruction>(*ui);
							if (L->contains(dependency)) {
								cout << "dependency " << depCount << ":\n";
								dependency->dump();
								depCount++;
								//check for dependencies between instructions
								for (User::op_iterator op = dependency->op_begin(); op != dependency->op_end(); op++) {
									
								//also check for aliasing within instructions
							}
							cout << "\n";
						}
					}
				}
			}*/
	};
}

char IsParallelizableLoopPass::ID = 0;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
