#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
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
				set<Instruction *> *dependentInstructions = new set<Instruction *>();
				for (Instruction::user_iterator pui = phi->user_begin(); pui != phi->user_end(); pui++) {
					Instruction *dependency = dyn_cast<Instruction>(*pui);
					cout << "found instruction dependent on induction variable at:\n";
					dependency->dump();
					//check to see whether the instruction manipulates the value of the IV in any way
					//if ((dependency->getOperand(0)->getName() == inductionVariable)
						//&& (string(dependency->getOpcodeName()).compare("trunc") != 0) && (string(dependency->getOpcodeName()).compare("zext") != 0)) {
						//cout << "and this instruction passes a manipulated version to...\n";
						//if so, look for instructions dependent on that instruction's value
					for (Instruction::user_iterator ui = dependency->user_begin(); ui != dependency->user_end(); ui++) {
						Instruction *dependency2 = dyn_cast<Instruction>(*ui);
						getDependencies(dependency2, phi, dependentInstructions);
					}
				}
				cout << "found potential dependent instructions within the loop:\n";
				for (set<Instruction *>::iterator si = dependentInstructions->begin(); si != dependentInstructions->end(); si++) {
					Instruction *i1 = (*si);
					for (set<Instruction *>::iterator si2 = dependentInstructions->begin(); si2 != dependentInstructions->end(); si2++) {
						Instruction *i2 = (*si2);
						unique_ptr<Dependence> d = DA->depends(i1,i2,true);
						cout << "dependency between\n";
						i1->dump();
						i2->dump();
						cout << "is\n";
						if (d != nullptr) {
							cout << d->getDirection(0) << " " << d->getDistance(0) << "\n";
						}
						else {
							cout << "no dependency\n";
						}
					}
				}

				for (set<Instruction *>::iterator si = dependentInstructions->begin(); si != dependentInstructions->end(); si++) {
					(*si)->dump();
				}
				delete dependentInstructions;
			}
			
			return false;
		}

		void getDependencies(Instruction *inst, PHINode *phi, set<Instruction *> *dependents) {
			cout << "For\n";
			inst->dump();
			cout << "we find it...\n";
			if (inst == phi) {
				cout << "is the phi node so this is OK\n\n";
			}
			else {
				if (inst->mayReadFromMemory()) {
					cout << "is a read memory instruction so this could be bad\n\n";
					dependents->insert(inst);
				}
				else if (inst->mayWriteToMemory()) {
					cout << "is a write memory instruction so this could be bad\n\n";
					dependents->insert(inst);
				}
				else {
					if (inst->getNumUses() > 0) {
						cout << "could still pass the iterator to a read/write instruction, recursing on..\n";
						for (Instruction::user_iterator ui = inst->user_begin(); ui != inst->user_end(); ui++) {
							getDependencies(dyn_cast<Instruction>(*ui), phi, dependents);
						}
					}
					else {
						cout << "not used in a read/write so doesn't cause loop dependencies\n";
					}
				}
			}
		}
	};
}

char IsParallelizableLoopPass::ID = 0;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
