#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/ParallelLoopPasses/LoopDependencyData.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include <iostream>
#include "llvm/ParallelLoopPasses/IsParallelizableLoopPass.h"
#include <string>
#include <set>
#include <list>

using namespace llvm;
using namespace std;
using namespace parallelize;


//Set LoopInfo pass to run before this one so we can access its results
void IsParallelizableLoopPass::getAnalysisUsage(AnalysisUsage &AU) const {
	AU.addRequired<LoopInfoWrapperPass>();
	AU.addRequired<ScalarEvolutionWrapperPass>();
	AU.addRequired<DependenceAnalysis>();
	AU.addRequired<AAResultsWrapperPass>();
	AU.addRequired<ScalarEvolutionWrapperPass>();
	//this pass is just analysis and so does not change any other analysis results
	AU.setPreservesAll();
}

bool IsParallelizableLoopPass::runOnFunction(Function &F) {
	//get data from the loopInfo analysis
	LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
	ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>().getSE();
	DA = &getAnalysis<DependenceAnalysis>();
	AA = &getAnalysis<AAResultsWrapperPass>().getAAResults();
	//list<LoopDependencyData *> l;
	results.clear();
	//results.insert(std::pair<Function&, list<LoopDependencyData *>>(F, l));
	//cout << "Results size = " << results.size() << "\n";
	
	if (!F.hasFnAttribute("Extracted")) {
		cerr << "Running parallelizable loop analysis on function " << (F.getName()).data() << "\n";
		//initialize iterators and loop counter
		LoopInfo::iterator i = LI.begin();
		LoopInfo::iterator e = LI.end();
		int LoopCounter = 1;

		//iterate through all the OUTER loops found and run anaysis to see whether they are parallelizable
		while (i != e) {
			Loop *L = *i;
			//cerr << "Found loop " << LoopCounter << "\n";
			//call the function that will be implemented to analyse the code
			if (isParallelizable(L, F, SE)) {
				cerr << "this loop is parallelizable\n";
			}
			else {
				cerr << "this loop is not parallelizable\n";
			}
			i++;
		}
	}
	return false;
}

list<LoopDependencyData *> IsParallelizableLoopPass::getResultsForFunction(Function &F) {
	return results;
}

PHINode *inductionPhiNode(Instruction &i) {
	if (isa<PHINode>(i)) {
		PHINode *potentialPhi = cast<PHINode>(&i);
		BasicBlock *phiBB = potentialPhi->getParent();
		//check for a back edge with the phi node variable
		for (auto inst : potentialPhi->users()) {
			if (isa<CmpInst>(inst)) {
				for (auto u : inst->users()) {
					if (isa<BranchInst>(u)) {
						BranchInst *br = cast<BranchInst>(u);
						Value *bb = br->getOperand(1);
						Value *bb2 = br->getOperand(2);
						if (bb == phiBB || bb2 == phiBB) {
							//we have found the phi node
							cerr << "found a loop induction variable\n";
							i.dump();
							cerr << "\n";
							return cast<PHINode>(potentialPhi);
						}
					}
				}
			}
		}
		//If one doesn't exist, check for a back edge with the next phi node variable
		Value *nextVal = potentialPhi->getOperand(1);
		for (auto inst : nextVal->users()) {
			if (isa<CmpInst>(inst)) {
				for (auto u : inst->users()) {
					if (isa<BranchInst>(u)) {
						BranchInst *br = cast<BranchInst>(u);
						Value *bb = br->getOperand(1);
						Value *bb2 = br->getOperand(2);
						if (bb == phiBB || bb2 == phiBB) {
							//we have found the phi node
							cerr << "found a loop induction variable\n";
							i.dump();
							cerr << "\n";
							return cast<PHINode>(potentialPhi);
						}
					}
				}
			}
		}
	}
	return nullptr;
}

//runs the actual analysis
bool IsParallelizableLoopPass::isParallelizable(Loop *L, Function &F, ScalarEvolution &SE) {
	list<Dependence *> dependencies;
	multimap<Instruction *, Instruction *> returnValues;
	list<PHINode *> accumulativePhiNodes;
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
					phi = inductionPhiNode(i);
				}
				if (phi != nullptr) {
					cerr << "first found phi node is the outer loop induction variable, continuing...\n";
					break;
				}
				else {
					cerr << "first found phi node is not the outer loop induction variable; not parallelizable...\n";
					parallelizable = false;
					return false;
				}
			}
			if (phi != nullptr) {
				break;
			}
		}
		if (phi == nullptr) {
			cerr << "no induction variable exists\n";
			parallelizable = false;
			return false;
		}
	}
	PhiNodes.push_back(phi);
	noOfPhiNodes++;
	cerr << "set loop induction variable to\n";
	phi->dump();
	cerr << "\n";

	int branchNo = 0;
	//loop through all instructions to check for only one induction phi node per inner loop, calls to instructions and variables live outside the loop (so must be returned)
	for (auto &bb : L->getBlocks()) {
		for (auto &inst : bb->getInstList()) {
			PHINode *foundPhi = inductionPhiNode(inst);
			if (foundPhi != nullptr) {
				//found another loop induction phi node
				if (!(foundPhi == phi)) {
					noOfPhiNodes++;
				}
			}
			else if (isa<PHINode>(inst)) {
				bool inOuterLoop = true;
				//we have found a non-loop-induction phi node variable
				for (Loop *inner : L->getSubLoops()) {
					if (inner->contains(&inst)) {
						inOuterLoop = false;
						break;
					}
				}
				if (inOuterLoop) {
					//if it's in the outer loop we are parallelizing we must accumulate
					cerr << "non-induction phi-node found in outer loop\n";
					inst.dump();
					cerr << "\n";
					accumulativePhiNodes.push_back(cast<PHINode>(&inst));
				}
			}
			//also, say the function is not parallelizable if it calls a function (will be an overestimation)
			else if (isa<CallInst>(inst)) {
				CallInst *call = cast<CallInst>(&inst);
				cerr << "call found in loop so not parallelizable\n";
				parallelizable = false;
				//TODO: IMPROVE
				return false;
			}
			//check for uses outside the loop
			for (auto u : inst.users()) {
				Instruction *use = cast<Instruction>(u);
				if (!L->contains(use)) {
					//we don't want duplicates
					bool first = true;
					pair <multimap<Instruction *, Instruction *>::iterator, multimap<Instruction *, Instruction *>::iterator> range = returnValues.equal_range(&inst);
					for (multimap<Instruction *, Instruction *>::iterator it = range.first; it != range.second; ++it) {
						if (it->second == use) {
							first = false;
							break;
						}
					}
					if (first) {
						cerr << "use of value in loop that is also used after:\n";
						inst.dump();
						use->dump();
						returnValues.insert(make_pair(&inst, use));
					}					
				}
			}
			if (isa<SelectInst>(inst)) {
				if (L->getSubLoops().size() == 0) {
					cerr << "Outer loop contains a conditional value select, not parallelizable:\n";
					inst.dump();
					cerr << "\n";
					parallelizable = false;
					return;
				}
				else if (!L->getSubLoops().front()->contains(&inst)) {
					cerr << "Outer loop contains a conditional value select, not parallelizable:\n";
					inst.dump();
					cerr << "\n";
					parallelizable = false;
					return;
				}
			}
		}
	}


	
	cerr << "total number of loops nested = " << noOfPhiNodes << "\n";
	//note: getSubLoops only obtains the immediate subloop number
	//say we'll only parallelize if the outerloop only contains one immediate inner loop
	if ((L->getSubLoops().size()) > 1) {
		parallelizable = false;
		cerr << "Too many loops immediately within the outer loop\n";
		return false;
	}

	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			//don't want to parallelize if multiple conditional branch instructions exist in the outer loop (i.e. if's) - to be safe
			//these also come in the form of select
			if (isa<BranchInst>(i)) {
				BranchInst *br = cast<BranchInst>(&i);
				if (br->isConditional()) {
					if (L->getSubLoops().size() == 0) {
						cerr << "Outer loop contains a conditional branch:\n";
						br->dump();
						cerr << "\n";
						branchNo++;
					}
					else if (!L->getSubLoops().front()->contains(br)) {
						cerr << "Outer loop contains a conditional branch:\n";
						br->dump();
						cerr << "\n";
						branchNo++;
					}
				}
			}
		}
	}
	if (branchNo > 1) {
		cerr << "Too many conditional branches found\n";
		parallelizable = false;
		return false;
	}

	//loop through instructions dependendent on the induction variable and check to see whether
	//there are interloop dependencies
	set<Instruction *> *dependentInstructions = new set<Instruction *>();
	parallelizable = getDependencies(L, phi, dependentInstructions);

	bool dependent = false;
	//find distance vectors for loop induction dependent read/write instructions
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
					dependent = true;
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
	if (dependent) {
		cerr << "Has dependencies so not parallelizable\n";
	}
	delete dependentInstructions;
	
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
	Instruction *exitCond;
	Value *startIt = phi->getOperand(0);
	Value *finalIt;
	for (auto bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (strncmp((i.getName()).data(), "exitcond", 8) == 0 || strncmp((i.getName()).data(), "cmp", 3) == 0) {
				finalIt = (i.getOperand(1));
				exitCond = &i;
				endFound = true;
			}
		}
	}

	//attempt to find trip count
	unsigned int tripCount = SE.getSmallConstantTripCount(L);
	cerr << "trip count is: " << tripCount << "\n";

	//not parallelizable if proper boundaries can't be found
	if (!endFound) {
		cerr << "boundaries can't be established\n";
		parallelizable = false;
	}

	//store results of analysis
	LoopDependencyData *data = new LoopDependencyData(phi, exitCond, L, dependencies, noOfPhiNodes, startIt, finalIt, tripCount, parallelizable, returnValues, accumulativePhiNodes);
	results.push_back(data);
	
	return parallelizable;
}

bool isDependentOnInductionVariable(Instruction *ptr, Instruction *phi, bool read) {
	set<Instruction *> opsToCheck;
	for (auto &op : ptr->operands()) {
		if (isa<Instruction>(op)) {
			opsToCheck.insert(cast<Instruction>(op));
		}
	}
	while (!opsToCheck.empty()) {
		Instruction *op = *opsToCheck.begin();
		if (op == phi) {
			if (read) {
				cerr << "read dependent on i found\n";
			}
			else {
				cerr << "write dependent on i found\n";
			}
			return true;
		}
		else if (isa<PHINode>(op)) {
			//stop searching on this branch
			opsToCheck.erase(op);
		}
		else {
			for (auto &newop : op->operands()) {
				if (isa<Instruction>(newop)) {
					opsToCheck.insert(cast<Instruction>(newop));
				}
			}
			opsToCheck.erase(op);
		}
	}
	return false;
}

//function puts all instructions whose memory access depends on the induction variable in the set. And returns false if
//there is write instruction not dependent on the induction variable (i.e. can't parallelize)
bool IsParallelizableLoopPass::getDependencies(Loop *L, PHINode *phi, set<Instruction *> *dependents) {
	bool parallelizable = true;
	bool dependent;
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			dependent = false;
			if (isa<LoadInst>(&i)) {
				//case : load
				//get the memory location pointer
				Instruction *ptr = cast<Instruction>(i.getOperand(0));
				dependent = isDependentOnInductionVariable(ptr, phi, true);
				if (dependent) {
					i.dump();
					cerr << "\n";
				}
			}
			else if (isa<StoreInst>(&i)) {
				//case : write
				Instruction *ptr = cast<Instruction>(i.getOperand(1));
				dependent = isDependentOnInductionVariable(ptr, phi, false);
				if (dependent) {
					i.dump();
					cerr << "\n";
				}
				else {
					i.dump();
					cerr << "Write instruction found but not dependent on i, not parallelizable\n\n";
					parallelizable = false;
				}
			}
			if (dependent) {
				dependents->insert(&i);
			}
		}
	}
	return parallelizable;
}

char IsParallelizableLoopPass::ID = 0;
list<LoopDependencyData *> IsParallelizableLoopPass::results;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
