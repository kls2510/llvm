#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/ParallelLoopPasses/LoopDependencyData.h"
#include <iostream>
#include "llvm/ParallelLoopPasses/IsParallelizableLoopPass.h"
#include <string>
#include <set>

using namespace llvm;
using namespace std;
using namespace parallelize;


//Set LoopInfo pass to run before this one so we can access its results
void IsParallelizableLoopPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<LoopInfoWrapperPass>();
	AU.addRequired<DependenceAnalysis>();
	AU.addRequired<AAResultsWrapperPass>();
	AU.addRequired<ScalarEvolutionWrapperPass>();
	//this pass is just analysis and so does not change any other analysis results
	AU.setPreservesAll();
}

bool IsParallelizableLoopPass::runOnFunction(Function &F) {
	//get data from the loopInfo analysis
	LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
	DA = &getAnalysis<DependenceAnalysis>();
	AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
	list<LoopDependencyData *> l;
	results.insert(std::pair<StringRef, list<LoopDependencyData *>>(F.getName(), l));
	//cout << "Results size = " << results.size() << "\n";
	
	//cout << "Running parallelizable loop analysis on function " << (F.getName()).data() << "\n";
	//initialize iterators and loop counter
	LoopInfo::iterator i = LI.begin();
	LoopInfo::iterator e = LI.end();
	int LoopCounter = 1;

	//iterate through all the OUTER loops found and run anaysis to see whether they are parallelizable
	while (i != e) {
		Loop *L = *i;
		//cout << "Found loop " << LoopCounter << "\n";
		//call the function that will be implemented to analyse the code
		if (isParallelizable(L, F)) {
			//cout << "this loop is parallelizable\n";
		}
		else {
			//cout << "this loop is not parallelizable\n";
		}
		i++;
	}
	return false;
}

list<LoopDependencyData *> IsParallelizableLoopPass::getResultsForFunction(Function &F) {
	StringRef name = F.getName();
	//cout << "Found request for " << F.getName().data() << "\n";
	//cout << "Map size = " << results.size() << "\n";
	//cout << "Dependency list size = " << ((results.find(name))->second).size() << "\n";
	return (results.find(name))->second;
}

//runs the actual analysis
bool IsParallelizableLoopPass::isParallelizable(Loop *L, Function &F) {
	list<Dependence *> dependencies;
	int noOfPhiNodes = 0;
	bool parallelizable = true;

	//case: simple loop with no nested loops
	if ((L->getSubLoops()).size() == 0) {
		//look as the primary phi node and carry out analysis from there
		PHINode *phi = L->getCanonicalInductionVariable();
		if (phi == nullptr) {
			//cannot parallelize loops with no cannonical induction variable
			return false;
		}
		noOfPhiNodes++;
		Instruction *inst = phi->getNextNode();
		//loop through all instructions to check for only one phi node
		while (L->contains(inst)) {
			if (isa<PHINode>(inst)) {
				//finding a second Phi node means the loop is not directly parallelizable
				noOfPhiNodes++;
				parallelizable = false;
			}
			inst = inst->getNextNode();
		}
		
		set<Instruction *> *dependentInstructions = new set<Instruction *>();

		//loop through instructions dependent on the induction variable and check to see whether
		//there are interloop dependencies
		for (auto &pui : phi->user_begin) {
			Instruction *dependency = dyn_cast<Instruction>(*pui);
			for (auto &ui : dependency->user_begin) {
				Instruction *dependency2 = dyn_cast<Instruction>(*ui);
				getDependencies(dependency2, phi, dependentInstructions);
			}
		}

		//TODO: run alias analysis on the found instructions here and add to data

		//Find distance vectors for the found dependent instructions
		for (auto &si : dependentInstructions->begin) {
			Instruction *i1 = (*si);
			for (auto &si2 : dependentInstructions->begin) {
				Instruction *i2 = (*si2);
				unique_ptr<Dependence> d = DA->depends(i1, i2, true);
				
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
					
					//decide whether this dependency makes the loop not parallelizable
					if (distance != 0) {
						if (d->isConsistent()) {
							parallelizable = false;
						}
						else {
							parallelizable = false;
						}
					}
					dependencies.push_back(d.release());
				}
			}
		}
		delete dependentInstructions;
		
		//store results of analysis
		LoopDependencyData *data = new LoopDependencyData(L, dependencies, noOfPhiNodes);
		StringRef funName = F.getName();
		map<StringRef, list<LoopDependencyData *>>::iterator it = (results.find(funName));
		(it->second).push_back(data);
	}
	return parallelizable;
}


void IsParallelizableLoopPass::getDependencies(Instruction *inst, PHINode *phi, set<Instruction *> *dependents) {
	if (inst == phi) {}
	else {
		if (inst->mayReadFromMemory()) {
			dependents->insert(inst);
		}
		else if (inst->mayWriteToMemory()) {
			dependents->insert(inst);
		}
		else {
			if (inst->getNumUses() > 0) {
				for (auto &ui : inst->user_begin) {
					getDependencies(dyn_cast<Instruction>(*ui), phi, dependents);
				}
			}
		}
	}
	return;
}

char IsParallelizableLoopPass::ID = 0;
//define the static variable member
map<StringRef, list<LoopDependencyData *>> IsParallelizableLoopPass::results;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
