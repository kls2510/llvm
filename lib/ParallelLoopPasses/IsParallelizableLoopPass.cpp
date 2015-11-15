#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include <iostream>
#include <string>
#include <set>

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
			AU.addRequired<ScalarEvolutionWrapperPass>();
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
			bool parallelizable = true;
			//default for now
			cout << "this loop has " << (L->getSubLoops()).size() << " subloops\n";
			//case: simple loop with no nested loops
			if ((L->getSubLoops()).size() == 0) {
				//look as the phi node and carry out analysis from there
				PHINode *phi = L->getCanonicalInductionVariable();
				//loop through instructions dependendent on the induction variable and check to see whether
				//there are interloop dependencies
				set<Instruction *> *dependentInstructions = new set<Instruction *>();
				for (Instruction::user_iterator pui = phi->user_begin(); pui != phi->user_end(); pui++) {
					Instruction *dependency = dyn_cast<Instruction>(*pui);
					for (Instruction::user_iterator ui = dependency->user_begin(); ui != dependency->user_end(); ui++) {
						Instruction *dependency2 = dyn_cast<Instruction>(*ui);
						getDependencies(dependency2, phi, dependentInstructions);
					}
				}
				for (set<Instruction *>::iterator si = dependentInstructions->begin(); si != dependentInstructions->end(); si++) {
					Instruction *i1 = (*si);
					for (set<Instruction *>::iterator si2 = dependentInstructions->begin(); si2 != dependentInstructions->end(); si2++) {
						Instruction *i2 = (*si2);
						unique_ptr<Dependence> d = DA->depends(i1,i2,true);
						cout << "dependency between\n";
						i1->dump();
						i2->dump();
						if (d != nullptr) {
							/*  direction:
							    NONE = 0,
							    LT = 1,
							    EQ = 2,
							    LE = 3,
							    GT = 4,
							    NE = 5,
							    GE = 6,
							    ALL = 7 */
							const SCEV *scev = (d->getDistance(1));
							int direction = d->getDirection(1);
							int distance;
							if (scev != nullptr && isa<SCEVConstant>(scev)) {
								const SCEVConstant *scevConst = cast<SCEVConstant>(scev);
								distance = *(int *)(scevConst->getValue()->getValue()).getRawData();
							}
							else {
								distance = 0;
							}
							cout << "obtained distance = " << distance << " and direction =  " << direction << " and consistent?: " << d->isConsistent() << "\n";

							//decide whether this dependency makes the loop not parallelizable
							if (distance != 0) {
								if (d->isConsistent()) {
									cout << "This is a loop dependency, but they are consistent each loop so might be transformed\n";
									parallelizable = false;
								}
								else {
									cout << "Loop dependency differs each iteration, this is not transfomable\n";
									parallelizable = false;
								}
							}
						}
					}
				}
				delete dependentInstructions;
			}
			
			return parallelizable;
		}

		void getDependencies(Instruction *inst, PHINode *phi, set<Instruction *> *dependents) {
			if (inst == phi) {
			}
			else {
				if (inst->mayReadFromMemory()) {
					dependents->insert(inst);
				}
				else if (inst->mayWriteToMemory()) {
					dependents->insert(inst);
				}
				else {
					if (inst->getNumUses() > 0) {
						for (Instruction::user_iterator ui = inst->user_begin(); ui != inst->user_end(); ui++) {
							getDependencies(dyn_cast<Instruction>(*ui), phi, dependents);
						}
					}
				}
			}
			return;
		}
	};
}

char IsParallelizableLoopPass::ID = 0;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
