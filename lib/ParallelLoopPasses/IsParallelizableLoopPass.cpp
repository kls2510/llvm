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
	//list<LoopDependencyData *> l;
	results.clear();
	//results.insert(std::pair<Function&, list<LoopDependencyData *>>(F, l));
	//cout << "Results size = " << results.size() << "\n";
	
	//cerr << "Running parallelizable loop analysis on function " << (F.getName()).data() << "\n";
	//initialize iterators and loop counter
	LoopInfo::iterator i = LI.begin();
	LoopInfo::iterator e = LI.end();
	int LoopCounter = 1;

	//iterate through all the OUTER loops found and run anaysis to see whether they are parallelizable
	while (i != e) {
		Loop *L = *i;
		//cerr << "Found loop " << LoopCounter << "\n";
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
	//return (results.find(F))->second;
	return results;
}

//runs the actual analysis
bool IsParallelizableLoopPass::isParallelizable(Loop *L, Function &F) {
	list<Dependence *> dependencies;
	int noOfPhiNodes = 0;
	bool parallelizable = true;

	//parallelize just outer loops for now
	PHINode *phi = L->getCanonicalInductionVariable();
	vector<Loop*> subloops = L->getSubLoops();
	vector<PHINode*> PhiNodes;
	if (phi == nullptr) {
		//loop has no cannonical induction variable but may have a normal phi node
		for (auto bb : L->getBlocks()) {
			for (auto &i : bb->getInstList()) {
				if (isa<PHINode>(i)) {
					phi = cast<PHINode>(&i);
					break;
				}
			}
			if (phi != nullptr) {
				break;
			}
		}
		if (phi == nullptr) {
			cerr << "no induction variable exists\n";
			return false;
		}
	}
	PhiNodes.push_back(phi);
	noOfPhiNodes++;

	//TODO: see if I can use a pass to cannonicalise more loops

	Instruction *inst = phi->getNextNode();
	//loop through all instructions to check for only one phi node per inner loop
	while (L->contains(inst)) {
		if (isa<PHINode>(inst)) {
			noOfPhiNodes++;
			//finding Phi node that isn't a cannonical induction variable means the loop is not directly parallelizable
		}
		inst = inst->getNextNode();
	}

	if (noOfPhiNodes > (L->getSubLoops().size() + 1)) {
		parallelizable = false;
		cerr << "Too many phi nodes\n";
	}

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

	//find distance vectors for dependent instructions
	for (set<Instruction *>::iterator si = dependentInstructions->begin(); si != dependentInstructions->end(); si++) {
		Instruction *i1 = (*si);
		for (set<Instruction *>::iterator si2 = dependentInstructions->begin(); si2 != dependentInstructions->end(); si2++) {
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
	if (!parallelizable) {
		cerr << "Has dependencies so not parallelizable\n";
	}
	delete dependentInstructions;
	
	//TODO: check if load/store operands alias
	vector<Value *> readwriteinstructions;
	for (auto bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (i.mayReadOrWriteMemory()) {
				for (auto &op : i.operands()) {
					if (isa<PointerType>(op->getType())){
						for (auto other : readwriteinstructions) {
							if (!(other == op)) {
								cerr << "looking for alias between:\n";
								op->dump();
								other->dump();
								//found an alias so can't parallelize
								if (!(AA->isNoAlias(op, other))) {
									parallelizable = false;
									cerr << "alias found between:\n";
									op->dump();
									other->dump();
								}
							}
						}
						readwriteinstructions.push_back(op);
					}
				}
			}
		}
	}

	//find loop boundaries
	bool endFound = false;
	Value *startIt = phi->getOperand(0);
	Value *finalIt;
	//find start and end iteration values
	for (auto bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (!endFound) {
				if (strcmp((i.getName()).data(), "exitcond") == 0 || strcmp((i.getName()).data(), "cmp") == 0) {
					finalIt = (i.getOperand(1));
					endFound = true;
				}
			}
		}
	}

	//not parallelizable if proper boundaries can't be found
	if (!endFound) {
		cerr << "boundaries can't be established\n";
		parallelizable = false;
	}

	//store results of analysis
	LoopDependencyData *data = new LoopDependencyData(L, dependencies, noOfPhiNodes, startIt, finalIt, parallelizable);
	results.push_back(data);
	
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
				for (Instruction::user_iterator ui = inst->user_begin(); ui != inst->user_end(); ui++) {
					getDependencies(dyn_cast<Instruction>(*ui), phi, dependents);
				}
			}
		}
	}
	return;
}

char IsParallelizableLoopPass::ID = 0;
//define the static variable member
//map<Function&, list<LoopDependencyData *>> IsParallelizableLoopPass::results;
list<LoopDependencyData *> IsParallelizableLoopPass::results;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
