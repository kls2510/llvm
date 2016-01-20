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

PHINode *inductionPhiNode(Instruction &i, Loop *L) {
	if (isa<PHINode>(i)) {
		PHINode *potentialPhi = cast<PHINode>(&i);
		//check for only 2 operands
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
		int op;
		BasicBlock *initialEntry = L->getLoopPredecessor();
		for (op = 0; op < 2; op++) {
			if (potentialPhi->getIncomingBlock(op) == initialEntry){
				//initial entry edge, do nothing
			}
			else {
				Value *nextVal = potentialPhi->getOperand(op);
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
		}
	}
	return nullptr;
}

//runs the actual analysis
bool IsParallelizableLoopPass::isParallelizable(Loop *L, Function &F, ScalarEvolution &SE) {
	list<Dependence *> dependencies;
	multimap<Value *, Value *> returnValues;
	map<PHINode *, unsigned int> accumulativePhiNodes;
	set<PHINode *> foundPhiNodes;
	int noOfPhiNodes = 0;

	//parallelize just outer loops for now
	PHINode *phi = findOuterLoopInductionPhi(L);
	if (phi == nullptr) {
		cerr << "no induction variable exists\n";
		return false;
	}
	foundPhiNodes.insert(phi);

	noOfPhiNodes++;
	cerr << "set loop induction variable to\n";
	phi->dump();
	cerr << "\n";

	//check only max one nested loop within each loop and count how many there are
	int noLoops = 1;
	bool nested = checkNestedLoops(L, noLoops);
	if (!nested) {
		return false;
	}

	//find all inductive phi nodes in the loop and its subloops
	vector<Loop *> currentSubloops = L->getSubLoops();
	while (currentSubloops.size() != 0) {
		//we know there is only one subloop in each loop; find their phi nodes
		Loop *subloop = *currentSubloops.begin();
		PHINode *inductionPhi;
		for (auto &bb : subloop->getBlocks()) {
			for (auto &i : bb->getInstList()) {
				inductionPhi = inductionPhiNode(i, subloop);
				if (inductionPhi != nullptr) {
					foundPhiNodes.insert(inductionPhi);
					noOfPhiNodes++;
					break;
				}
			}
			if (inductionPhi != nullptr) {
				break;
			}
		}
		currentSubloops = subloop->getSubLoops();
	}

	int branchNo = 0;
	Instruction *exitCnd = nullptr;
	//loop through all instructions to:
	// - ensure all outer loop phi nodes only use 2 operands
	// - find non-induction phi nodes in the outer loop
	// - find calls to non thread-safe functions
	// - find variables live outside the loop (and so will need to be returned)
	// - doesn't contain any select statements
	// - find the exit branch condition for the outer loop
	for (auto &bb : L->getBlocks()) {
		for (auto &inst : bb->getInstList()) {
			bool inOuterLoop = true;
			currentSubloops = L->getSubLoops();
			while (currentSubloops.size() != 0) {
				Loop *subloop = *currentSubloops.begin();
				if (subloop->contains(&inst)) {
					inOuterLoop = false;
					break;
				}
				currentSubloops = subloop->getSubLoops();
			}

			if (isa<PHINode>(inst)) {
				//we are only interested in phi nodes in the outermost loop

				if (inOuterLoop) {
					if (cast<PHINode>(&inst)->getNumOperands() != 2) {
						//We assume there are only 2 operands, so the loop is not parallelizable if not
						cerr << "Phi node with too many operands found, not parallelizable\n";
						return false;
					}

					if (foundPhiNodes.find(cast<PHINode>(&inst)) == foundPhiNodes.end()) {
						//we have found a non-loop-induction phi node variable
						int opcode;
						bool phiIsValid = checkAccumulativePhiIsValid(inst, L, opcode);
						if (phiIsValid) {
							accumulativePhiNodes.insert(make_pair(cast<PHINode>(&inst), opcode));
						}
						else {
							return false;
						}
					}
				}
			}

			//also, say the function is not parallelizable if it calls a function that accesses memory and causes side effects (an overestimation)
			else if (isa<CallInst>(inst)) {
				CallInst *call = cast<CallInst>(&inst);
				if (call->mayHaveSideEffects()) {
					cerr << "call found in loop that may write to memory, so not parallelizable\n";
					return false;
				}
			}

			//Dont parallelize if the outer loop contains a select statement
			else if (isa<SelectInst>(inst)) {
				if (inOuterLoop) {
					cerr << "Outer loop contains a conditional value select, not parallelizable:\n";
					inst.dump();
					cerr << "\n";
					return false;
				}
			}

			//don't want to parallelize if branch instructions that aren't phi node branches exist in the outer loop or to the next BB in the loop (i.e. if's) - to be safe
			else if (isa<BranchInst>(inst)) {
				BranchInst *br = cast<BranchInst>(&inst);
				if (br->isConditional()) {
					BasicBlock *phiBB = phi->getParent();
					Value *bb = br->getOperand(1);
					Value *bb2 = br->getOperand(2);
					if (bb == phiBB || bb2 == phiBB) {
						//we have found the branch to exit the outer loop
						exitCnd = cast<Instruction>(inst.getOperand(0));
					}
					cerr << "loop contains a conditional branch:\n";
					br->dump();
					cerr << "\n";
					branchNo++;
					if (branchNo > noLoops) {
						cerr << "Too many conditional branches found\n";
						return false;
					}
				}
			}

			//check for users of values calculated inside the loop, outside the loop
			for (auto u : inst.users()) {
				Instruction *use = cast<Instruction>(u);
				if (!L->contains(use)) {
					//we don't want duplicates
					bool first = true;
					pair <multimap<Value *, Value *>::iterator, multimap<Value *, Value *>::iterator> range = returnValues.equal_range(&inst);
					for (multimap<Value *, Value *>::iterator it = range.first; it != range.second; ++it) {
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
		}
	}

	Value *finalIt;
	//if exit condition wasn't found, not parallelizable
	if (exitCnd == nullptr) {
		cerr << "exit condition can't be established, not parallelizable\n";
		return false;
	}
	else {
		cerr << "found exit condition instruction :\n";
		exitCnd->dump();
		finalIt = exitCnd->getOperand(1);
	}

	cerr << "total number of induction phi nodes = " << noOfPhiNodes << "\n";
	cerr << "total number of loops = " << noLoops << "\n";
	//only parallelizable if the number of discovered induction phi nodes match the number of loops found
	if (noOfPhiNodes != noLoops) {
		cerr << "No of phi nodes don't match no of loops - not parallelizable\n";
		return false;
	}

	//loop through instructions to find all read/write instructions dependent on the outer phi node
	set<Instruction *> *dependentInstructions = new set<Instruction *>();
	bool allWriteDependentOnPhi = getDependencies(L, phi, dependentInstructions);
	if (!allWriteDependentOnPhi) {
		//writes to the same location across threads will cause error
		cerr << "not parallelizable as write to array found not dependent on i\n";
		return false;
	}

	//find inter-loop dependencies between the reads and writes found above
	dependencies = findDistanceVectors(dependentInstructions, DA);
	if (dependencies.size() > 0) {
		cerr << "Has dependencies so not parallelizable\n";
		delete dependentInstructions;
		return false;
	}
	delete dependentInstructions;
	
	//store all arrays accessed in the loop to check for aliasing
	set<Value *> allarrays;
	//store all values passed to the function that are used in the loop
	set<Value *> argValues;
	//check function arguments for use in the loop - if they are they must be loaded first before being passed to the thread function
	for (auto &arg : F.getArgumentList()) {
		for (auto &bb : L->getBlocks()) {
			if (arg.isUsedInBasicBlock(bb)) {
				if (isa<PointerType>(arg.getType())) {
					//array is passed to function as argument and used in loop
					allarrays.insert(&arg);
				}
				//value is passed to function as argument and used in loop
				argValues.insert(&arg);
				break;
			}
		}
	}

	//set to store all local array and value declarations within the function used in the loop (will need to be passed to thread function as argument)
	set<Value *> localvalues;
	//loop through all instructions and find all arrays and variables used that aren't defined in the loop
	for (auto bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			for (auto &op : i.operands()) {
				if (isa<ArrayType>(op->getType()) || isa<ArrayType>(op->getType()->getPointerElementType())){
					Value *arr = cast<Value>(&op);
					if (argValues.find(arr) == argValues.end() && !isa<GlobalValue>(arr)) {
						if (isa<Instruction>(arr)) {
							if (!L->contains(cast<Instruction>(arr))) {
								localvalues.insert(arr);
							}
						}
						else {
							localvalues.insert(arr);
						}
					}
					allarrays.insert(arr);
				}
				else {
					//If the variable is global then loop is not parallelizable
					if (isa<GlobalValue>(&op)) {
						//POTENTIAL TODO: must pass copy of value in so if it is changed we can store it back
						cerr << "accesses to global variable made in loop, not parallelizable\n";
						op->dump();
						op->getType()->dump();
						cerr << "%n";
						return false;
					}
					//also get local values that are required to be known in the loop
					else if (isa<Instruction>(op)) {
						Instruction *inst = cast<Instruction>(&op);
						//global values accessed and written to in loop must be returned and stored back
						if (!L->contains(inst)) {
							//must pass in local value as arg so it is available
							if (argValues.find(inst) == argValues.end()) {
								localvalues.insert(cast<Value>(inst));
							}
						}
					}
				}
			}
		}
	}


	//check for aliasing between any two arrays in the loop
	for (auto arr1 : allarrays) {
		for (auto arr2 : allarrays) {
			if (arr1 != arr2) {
				cerr << "looking for alias between:\n";
				arr1->dump();
				arr2->dump();
				//found an alias so can't parallelize
				if (!(AA->isNoAlias(arr1, arr2))) {
					cerr << "potential alias found, can't parallelize\n";
					return false;
				}
			}
		}
	}

	//store local args in list for extractor to use
	list<Value *> localArgs;
	for (auto v : localvalues) {
		cerr << "value must be passed as an argument\n";
		v->dump();
		localArgs.push_back(v);
	}

	//store argument args in list for extractor to use
	list<Value *> argArgs;
	for (auto v : argValues) {
		cerr << "value must be passed as an argument\n";
		v->dump();
		argArgs.push_back(v);
	}

	//find loop start iteration value
	Value *startIt = nullptr;
	int op;
	BasicBlock *initialEntry = L->getLoopPredecessor();
	for (op = 0; op < 2; op++) {
		if (phi->getIncomingBlock(op) == initialEntry){
			//initial entry edge, replace here
			startIt = phi->getOperand(op);
			break;
		}
	}
	if (startIt == nullptr) {
		cerr << "start iteration could not be found\n";
		return false;
	}

	//attempt to find trip count
	unsigned int tripCount = SE.getSmallConstantTripCount(L);
	cerr << "trip count is: " << tripCount << "\n";

	//store results of analysis
	bool parallelizable = true;
	LoopDependencyData *data = new LoopDependencyData(phi, localArgs, argArgs, exitCnd, L, dependencies, noOfPhiNodes, startIt, finalIt, tripCount, parallelizable, returnValues, accumulativePhiNodes);
	results.push_back(data);
	
	return true;
}

bool IsParallelizableLoopPass::isDependentOnInductionVariable(Instruction *ptr, Instruction *phi, bool read) {
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

PHINode *IsParallelizableLoopPass::findOuterLoopInductionPhi(Loop *L) {
	PHINode *phi = L->getCanonicalInductionVariable();

	if (phi == nullptr) {
		//loop has no cannonical induction variable but may have a normal phi node
		for (auto bb : L->getBlocks()) {
			for (auto &i : bb->getInstList()) {
				if (isa<PHINode>(i)) {
					phi = inductionPhiNode(i, L);
				}
				if (phi != nullptr) {
					cerr << "found phi node is the outer loop induction variable, continuing...\n";
					break;
				}
				/* else {
				cerr << "first found phi node is not the outer loop induction variable; not parallelizable...\n";
				parallelizable = false;
				return false;
				} */
			}
			if (phi != nullptr) {
				break;
			}
		}
	}
	return phi;
}

bool IsParallelizableLoopPass::checkNestedLoops(Loop *L, int &noLoops) {
	vector<Loop *> currentSubloops = L->getSubLoops();
	while (currentSubloops.size() != 0) {
		if (currentSubloops.size() == 1) {
			Loop *next = *currentSubloops.begin();
			currentSubloops = next->getSubLoops();
			noLoops++;
		}
		else {
			//multiple subloops in one loop so don't parallelize
			cerr << "too many inner loops, not parallelizable\n";
			return false;
		}
	}
	return true;
}

bool IsParallelizableLoopPass::checkAccumulativePhiIsValid(Instruction &inst, Loop *L, int &opcode){
	//if it's in the outer loop we are parallelizing we must accumulate
	cerr << "non-induction phi-node found in outer loop\n";
	inst.dump();
	cerr << "\n";
	PHINode *phi = cast<PHINode>(&inst);
	//find what the repeated operation is - only accept commutative operations
	//i.e. case Add, case FAdd, case Mul, case FMul, case And, case Or, case Xor
	int op;
	BasicBlock *initialEntry = L->getLoopPredecessor();

	for (op = 0; op < 2; op++) {
		if (phi->getIncomingBlock(op) == initialEntry){
			//initial entry edge, do nothing
		}
		else {
			//find where incoming value is defined - this op will be what needs to be used to accumulate
			Value *incomingNewValue = phi->getIncomingValue(op);
			if (isa<Instruction>(incomingNewValue)) {
				Instruction *incomingInstruction = cast<Instruction>(incomingNewValue);
				cerr << "found instruction to accumulate\n";
				incomingInstruction->dump();
				cerr << "\n";
				opcode = incomingInstruction->getOpcode();
				if (Instruction::isCommutative(opcode)) {
					//check it doesn't use it's own value in the evaluation e.g. k += X*k
					bool isValid = true;
					set<Value *> usedInstructions;
					User::op_iterator operand = incomingInstruction->op_begin();
					while (operand != incomingInstruction->op_end()) {
						if (isa<Instruction>(operand)) {
							usedInstructions.insert(*operand);
						}
						operand++;
					}
					while (!usedInstructions.empty()) {
						Value *op = *usedInstructions.begin();
						if (op == incomingInstruction) {
							isValid = false;
							cerr << "Accumulation dependent on itself, not parallelizable\n";
							break;
						}
						else if (isa<PHINode>(op)) {
							//stop searching on this branch
							usedInstructions.erase(op);
						}
						else {
							for (auto &newop : cast<Instruction>(op)->operands()) {
								if (isa<Instruction>(newop)) {
									usedInstructions.insert(newop);
								}
							}
							usedInstructions.erase(op);
						}
					}
					if (isValid) {
						return true;
					}
					else {
						return false;
					}
				}
				else {
					cerr << "phi variable op not commutative so can't be parallelized\n";
					return false;
				}
			}
			else {
				return false;
				//TODO: Not parallelizable?
			}
		}
	}
	return false;
}

list<Dependence *> IsParallelizableLoopPass::findDistanceVectors(set<Instruction *> *dependentInstructions, DependenceAnalysis *DA) {
	//find distance vectors for loop induction dependent read/write instructions
	list<Dependence *> dependencies;
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
					dependencies.push_back(d.release());
				}
			}
		}
	}
	return dependencies;
}


char IsParallelizableLoopPass::ID = 0;
list<LoopDependencyData *> IsParallelizableLoopPass::results;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");
