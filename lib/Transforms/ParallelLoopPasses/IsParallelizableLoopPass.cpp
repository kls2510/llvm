#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/ParallelLoopPasses/LoopDependencyData.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/ValueSymbolTable.h"
#include <iostream>
#include "llvm/ParallelLoopPasses/IsParallelizableLoopPass.h"
#include <string>
#include <set>
#include <list>

using namespace llvm;
using namespace std;
using namespace parallelize;


INITIALIZE_PASS_BEGIN(IsParallelizableLoopPass, "parallelizable-loop-analysis",
                "Determine if a loop can be parallelized", false, true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_DEPENDENCY(DependenceAnalysis)
INITIALIZE_PASS_DEPENDENCY(AAResultsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ScalarEvolutionWrapperPass)
INITIALIZE_PASS_END(IsParallelizableLoopPass, "parallelizable-loop-analysis",
                "Determine if a loop can be parallelized", false, true)


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
	//results.clear();
	//results.insert(std::pair<Function&, list<LoopDependencyData *>>(F, l));
	//cout << "Results size = " << results.size() << "\n";
	
	if (!F.hasFnAttribute("Extracted")) {
		cerr << "Running parallelizable loop analysis on function " << (F.getName()).data() << "\n";
		//initialize iterators and loop counter
		LoopInfo::iterator i = LI.begin();
		LoopInfo::iterator e = LI.end();

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
	if (results.find(&F) != results.end()) {
		return results.find(&F)->second;
	}
	return list<LoopDependencyData *>();
}

Instruction *IsParallelizableLoopPass::findCorrespondingBranch(Value *potentialPhi, BasicBlock *backedgeBlock) {
	for (auto inst : potentialPhi->users()) {
		if (isa<CmpInst>(inst)) {
			for (auto u : inst->users()) {
				if (isa<BranchInst>(u)) {
					BranchInst *br = cast<BranchInst>(u);
					if (br->isConditional()) {
						Value *bb = br->getOperand(1);
						Value *bb2 = br->getOperand(2);
						if (bb == backedgeBlock || bb2 == backedgeBlock) {
							return cast<Instruction>(u);
						}
					}
				}
			}
		}
	}
	return nullptr;
}

pair<PHINode *, Instruction *> IsParallelizableLoopPass::inductionPhiNode(PHINode *potentialPhi, Loop *L) {
	Value *currentPhiVal = potentialPhi;
	Value *nextPhiVal = nullptr;
	int op;
	for (op = 0; op < 2; op++) {
		if (potentialPhi->getIncomingBlock(op) == L->getLoopPredecessor()){
			//initial entry edge, do nothing
		}
		else {
			nextPhiVal = potentialPhi->getIncomingValue(op);
		}
	}
	Instruction *branchInstruction = nullptr;
	branchInstruction = findCorrespondingBranch(currentPhiVal, potentialPhi->getParent());
	if (branchInstruction == nullptr) {
		branchInstruction = findCorrespondingBranch(nextPhiVal, potentialPhi->getParent());
	}
	if (branchInstruction != nullptr) {
		cerr << "found induction phi with branch instruction:\n";
		potentialPhi->dump();
		branchInstruction->dump();
		return make_pair(potentialPhi, branchInstruction);
	}
	else {
		return make_pair(nullptr, nullptr);
	}
}

//runs the actual analysis
bool IsParallelizableLoopPass::isParallelizable(Loop *L, Function &F, ScalarEvolution &SE) {
	list<Dependence *> dependencies;
	multimap<Value *, Value *> returnValues;
	map<PHINode *, unsigned int> accumulativePhiNodes;
	map<PHINode *, pair<const Value *, Value *>> otherPhiNodes;
	set<PHINode *> foundPhiNodes;
	int noOfInductionPhiNodes = 0;

	// INDUCTION PHI NODES:
	//check all phi nodes only take two operands
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (isa<PHINode>(i)) {
				if (i.getNumOperands() != 2) {
					cerr << "found phi node without 2 operands - not parallelizable\n";
					i.dump();
					cerr << "\n";
					return false;
				}
			}
		}
	}
	// get number of loops + subloops we're dealing with
	int noLoops = 1;
	bool nested = checkNestedLoops(L, noLoops);
	if (!nested) {
		// >1 subloop per loop level found
		return false;
	}
	// attempt to find phi node for each loop
	list<PHINode *> phiNodes;
	list<Instruction *> branchInstructions;
	Loop *currentLoop = L;
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (isa<PHINode>(i)) {
				pair<PHINode *, Instruction *> p = inductionPhiNode(cast<PHINode>(&i), currentLoop);
				if (p.first != nullptr) {
					phiNodes.push_back(p.first);
					branchInstructions.push_back(p.second);
					noOfInductionPhiNodes++;
					if (currentLoop->getSubLoops().size() > 0) {
						currentLoop = currentLoop->getSubLoops().front();
					}
					break;
				}
			}
		}
	}
	PHINode *outerPhi = phiNodes.front();
	Instruction *outerBranch = branchInstructions.front();
	// if phi nodes found = number of loops, continue
	if (noOfInductionPhiNodes == noLoops) {
		cerr << "no of found induction phis equals number of loops (" << noOfInductionPhiNodes << ")\n";
	}
	// else not parallelizable
	else {
		cerr << "no of found induction phis does not equal number of loops (" << noOfInductionPhiNodes << ", " << noLoops << ")\n";
		cerr << "loop not parallelizable\n";
		return false;
	}

	//check outer phi can only be integer-based - for now
	if (!outerPhi->getType()->isIntegerTy()) {
		cerr << "outer phi node not integer value - not parallelizable\n";
		return false;
	}

	Value *outerPhiStep = nullptr;
	// check the outer phi node step is constant and available using scalar evolution
	const SCEV *outerPhiScev = SE.getSCEVAtScope(outerPhi, L);
	if (SE.getLoopDisposition(outerPhiScev, L) == SE.LoopComputable) {
		cerr << "outer phi changes by a constant each loop iteration\n";
		if (isa<SCEVAddRecExpr>(outerPhiScev)) {
			const SCEVAddRecExpr *outerPhiScevExpr = cast<SCEVAddRecExpr>(outerPhiScev);
			const SCEV *stepSize = outerPhiScevExpr->getStepRecurrence(SE);
			cerr << "step size:\n";
			stepSize->dump();
			cerr << "\n";
			if (isa<SCEVConstant>(stepSize)) {
				const SCEVConstant *stepConst = cast<SCEVConstant>(stepSize);
				cerr << "outer phi has found constant step size, continuing...\n";
				outerPhiStep = stepConst->getValue();
			}
			else {
				cerr << "scev has non-constant step value - not parallelizable\n";
				return false;
			}
		}
		else {
			cerr << "outer phi node step not part of an AddRecExpr - not parallelizable";
			return false;
		}
	}
	else {
		cerr << "outer phi node step not loop computable - not parallelizable";
		return false;
	}

	//check outer phi branch condition is within a set of allowed predicates
	CmpInst *outercnd = cast<CmpInst>(outerBranch->getOperand(0));
	if (outercnd->getPredicate() == CmpInst::ICMP_SGE || outercnd->getPredicate() == CmpInst::ICMP_UGE || (outercnd->getPredicate() == CmpInst::ICMP_EQ && outerPhiStep < 0)) {
		//allowed
	}
	else if (outercnd->getPredicate() == CmpInst::ICMP_SGT || outercnd->getPredicate() == CmpInst::ICMP_UGT) {
		//allowed
	}
	else if (outercnd->getPredicate() == CmpInst::ICMP_SLE || outercnd->getPredicate() == CmpInst::ICMP_ULE || (outercnd->getPredicate() == CmpInst::ICMP_EQ && outerPhiStep > 0)) {
		//allowed
	}
	else if (outercnd->getPredicate() == CmpInst::ICMP_SLT || outercnd->getPredicate() == CmpInst::ICMP_ULT) {
		//allowed
	}
	else {
		cerr << "outer phi conditional branch predicate not allowed - not parallelizable\n";
		return false;
	}

	cerr << "set outer loop induction variable to\n";
	outerPhi->dump();
	cerr << "\n";

	//START AND END ITERATIONS
	// for outer loop phi node, obtain start and end values from the phi node and corresponding compare instruction
	int op;
	Value *startIt = nullptr;
	Value *finalIt = nullptr;
	for (op = 0; op < 2; op++) {
		if (outerPhi->getIncomingBlock(op) == L->getLoopPredecessor()){
			//initial entry edge, get start iteration value
			startIt = outerPhi->getIncomingValue(op);
			break;
		}
	}
	Instruction *outerCompare = cast<Instruction>(outerBranch->getOperand(0));
	finalIt = outerCompare->getOperand(1);

	if (startIt == nullptr || finalIt == nullptr) {
		cerr << "start/end iteration bounds could not be established - not parallelizable\n";
		return false;
	}

	//ACCUMULATOR/NON-INDUCTION PHI NODES
	//If there are any other phi nodes in the outer loop that aren't induction phis
	set<PHINode *> helperAccPhis;
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (isa<PHINode>(i) && helperAccPhis.find(cast<PHINode>(&i)) == helperAccPhis.end()) {
				PHINode *potentialAccumulator = cast<PHINode>(&i);
				if (find(phiNodes.begin(), phiNodes.end(), potentialAccumulator) != phiNodes.end()) {
					continue;
				}
				if (L->getSubLoops().size() > 0) {
					if ((*((L->getSubLoops()).begin()))->contains(potentialAccumulator))
						continue;
				}
				bool phiSatisfied = false;
				//if scalar evolution knows the phi value changes by a particular value each iteration then we can pass in the required
				//values to each thread
				cerr << "non induction phi scev found:\n";
				potentialAccumulator->dump();
				if (SE.isSCEVable(potentialAccumulator->getType())) {
					const SCEV *phiScev = SE.getSCEVAtScope(potentialAccumulator, L);
					phiScev->dump();
					phiScev->getType()->dump();
					if (SE.getLoopDisposition(phiScev, L) == SE.LoopComputable) {
						cerr << "phi changes by a constant each outer loop iteration\n";
						if (isa<SCEVAddRecExpr>(phiScev)) {
							const SCEVAddRecExpr *phiScevExpr = cast<SCEVAddRecExpr>(phiScev);
							const SCEV *firstVal = phiScevExpr->getStart();
							cerr << "first val:\n";
							firstVal->dump();
							cerr << "\n";
							const SCEV *stepSize = phiScevExpr->getStepRecurrence(SE);
							cerr << "step size:\n";
							stepSize->dump();
							cerr << "\n";
							if (isa<SCEVConstant>(stepSize)) {
								const SCEVConstant *stepConst = cast<SCEVConstant>(stepSize);
								cerr << "phi node can be split up for iterations, adding to analysis\n";
								if (isa<SCEVConstant>(firstVal)) {
									otherPhiNodes.insert(make_pair(potentialAccumulator, make_pair(cast<SCEVConstant>(firstVal)->getValue(), stepConst->getValue())));
								}
								else {
									if (isa<SCEVUnknown>(firstVal)) {
										const SCEVUnknown *scev = cast<SCEVUnknown>(firstVal);
										cerr << "start val non-const but is value:\n";
										scev->getValue()->dump();
										cerr << "\n";
										otherPhiNodes.insert(make_pair(potentialAccumulator, make_pair(scev->getValue(), stepConst->getValue())));
									}
									else {
										cerr << "Error reading SCEV values - not parallelizable\n";
										return false;
									}
								}
								phiSatisfied = true;
							}
							else {
								cerr << "scev has non-constant step value - not parallelizable\n";
								return false;
							}
						}
					}
				}

				if (!phiSatisfied) {
					cerr << "phi node doesn't match pattern to be replaced by values in threads, checking to see whether it is accumulative\n";
					//else we'll assume it is a phi node used to accumulate some value
					Value *nextValue = nullptr;
					for (op = 0; op < 2; op++) {
						if (potentialAccumulator->getIncomingBlock(op) == L->getLoopPredecessor()){
							//initial entry edge, do nothing
						}
						else {
							nextValue = potentialAccumulator->getIncomingValue(op);
						}
					}
					//if the actual phi name is used in the loop for operations other than accumualation->not parallelizable
					//(i.e.x += x * y possible and this wouldn't be correct)
					int numPhiNodeUsesInInnerPhis = 0;
					for (auto u : potentialAccumulator->users()) {
						if (isa<PHINode>(u)) {
							numPhiNodeUsesInInnerPhis++;
							helperAccPhis.insert(cast<PHINode>(u));
						}
					}
					if (potentialAccumulator->getNumUses() - numPhiNodeUsesInInnerPhis > 1) {
						cerr << "accumulative phi node has too many users: " << potentialAccumulator->getNumUses() - numPhiNodeUsesInInnerPhis << " - not parallelizable\n";
						potentialAccumulator->dump();
						for (auto u : potentialAccumulator->users()) {
							if (!isa<PHINode>(u)) {
								u->dump();
							}
						}
						return false;
					}
					//if the next value it's assigned is used elsewhere in the loop (except in an inner loop's phi node) 
					//then not parallelizable (as we change this value between threads in transform)
					for (auto u : nextValue->users()) {
						if (isa<Instruction>(u)) {
							if (L->contains(cast<Instruction>(u)) && !isa<PHINode>(u)) {
								cerr << "accumulative value used in loop (and not in inner accumulative phi) - not parallelizable\n";
								return false;
							}
						}
						else {
							//TEMPORARY
							cerr << "found use not an instruction\n";
							u->dump();
							cerr << "\n";
						}
					}
					//Check it is valid i.e. if the one place the phi's non-initial operand value is used is for a commutable operation
					int opcode;
					bool isValid = checkPhiIsAccumulative(potentialAccumulator, L, opcode);
					if (!isValid) {
						return false;
					}
					else {
						accumulativePhiNodes.insert(make_pair(potentialAccumulator, opcode));
					}
				}
			}
		}
	}

	// UNEXPECTED BRANCHING
	// Check there aren't any unexpected branches out of the loop (can't parallelize those with returns / breaks in the middle of them)
	vector<BasicBlock *> loopBBs = L->getBlocks();
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			// all branches that can go to a basic block outside the outer loops basic blocks should only be those with a corresponding phi node
			if (isa<BranchInst>(i)) {
				BranchInst *branch = cast<BranchInst>(&i);
				if (branch->isConditional()) {
					// no conditional branches to out of the loop unless associated with a phi node
					BasicBlock *t1 = nullptr;
					BasicBlock *t2 = nullptr;
					if (isa<BasicBlock>(branch->getOperand(1)) && isa<BasicBlock>(branch->getOperand(2))) {
						t1 = cast<BasicBlock>(branch->getOperand(1));
						t2 = cast<BasicBlock>(branch->getOperand(2));
					}
					else {
						cerr << "Basic block values expected in branch instruction but not found - not parallelizable\n";
						return false;
					}
					if (find(branchInstructions.begin(), branchInstructions.end(), branch) == branchInstructions.end()) {
						//the branch has no associated phi node
						if (find(loopBBs.begin(), loopBBs.end(), t1) == loopBBs.end()) {
							//can branch to outside the loop
							cerr << "unexpected branch instruction found - not parallelizable\n";
							branch->dump();
							cerr << "\n";
							return false;
						}
						if (find(loopBBs.begin(), loopBBs.end(), t2) == loopBBs.end()) {
							//can branch to outside the loop
							cerr << "unexpected branch instruction found - not parallelizable\n";
							branch->dump();
							cerr << "\n";
							return false;
						}
					}
				}
				else {
					// no unconditional branches out of the outer loop
					Value *t1 = branch->getOperand(0);
					if (find(loopBBs.begin(), loopBBs.end(), t1) == loopBBs.end()) {
						//can branch to outside the loop
						cerr << "unexpected branch instruction found - not parallelizable\n";
						branch->dump();
						cerr << "\n";
						return false;
					}
				}
			}
			if (isa<ReturnInst>(i)) {
				// no return instructions in any of the loops
				cerr << "unexpected return instruction found - not parallelizable\n";
				i.dump();
				cerr << "\n";
				return false;
			}
			if (isa<SwitchInst>(i) || isa<IndirectBrInst>(i)) {
				// no switch or indirect branch statements
				cerr << "unrecognised branch instruction found - not parallelizable\n";
				i.dump();
				cerr << "\n";
				return false;
			}
		}
	}

	//ARGUMENT VALUES
	// Find all values that must be provided to each thread of the loop
	set<Value *> argValues;
	set<Value *> privateLoopVarUses;
	set<Value *> lifetimeValues;
	set<Value *> voidCastsForLoop;
	//case: use of lifetime start/end in the IR - separate memory must be passed to each thread for this
	Function *lifetimeStart = nullptr;
	Function *lifetimeEnd = nullptr;
	ValueSymbolTable &symTab = F.getParent()->getValueSymbolTable();
	if (symTab.lookup(StringRef("llvm.lifetime.start"))) {
		lifetimeStart = cast<Function>(symTab.lookup(StringRef("llvm.lifetime.start")));
	}
	if (symTab.lookup(StringRef("llvm.lifetime.end"))) {
		lifetimeEnd = cast<Function>(symTab.lookup(StringRef("llvm.lifetime.end")));
	}
	if (lifetimeStart != nullptr) {
		for (auto bb : L->getBlocks()) {
			for (auto &i : bb->getInstList()) {
				if (isa<CallInst>(i)) {
					CallInst *call = cast<CallInst>(&i);
					if (call->getCalledFunction() == lifetimeStart) {
						//need to check lifetime end is within loop too
						cerr << "analysing a lifetime in loop\n";
						Value *lifetimeCastVal = call->getArgOperand(1);
						lifetimeCastVal->dump();
						Value *actualLifetimeVal = lifetimeCastVal->stripPointerCasts();
						actualLifetimeVal->dump();
						cerr << "\n";
						Instruction *endCall = nullptr;
						bool end = false;
						for (auto bb : L->getBlocks()) {
							for (auto &i : bb->getInstList()) {
								if (isa<CallInst>(i)) {
									CallInst *call = cast<CallInst>(&i);
									if (call->getCalledFunction() == lifetimeEnd) {
										if (call->getArgOperand(1) == lifetimeCastVal) {
											end = true;
											endCall = call;
											break;
										}
									}
								}
							}
							if (end) {
								break;
							}
						}
						if (end)   {
							//check that the null pointer cast is inside the loop before the call to lifetime begin - otherwise we'll need to move
							//it into the loop 
							if (!L->contains(cast<Instruction>(lifetimeCastVal))) {
								bool uniqueToLifetime = true;
								call->dump();
								endCall->dump();
								for (auto u : lifetimeCastVal->users()) {
									if (u != call && u != endCall) {
										cerr << "void cast is outside loop and used elsewhere other than for lifetime calls - not parallelizable\n";
										u->dump();
										uniqueToLifetime = false;
									}
								}
								if (uniqueToLifetime) {
									voidCastsForLoop.insert(lifetimeCastVal);
								}
								else {
									return false;
								}
							}
							//same goes for any values loaded from the value that we're passing uniquely to each thread
							for (auto u : actualLifetimeVal->users()) {
								Instruction *lifetimeUser = cast<Instruction>(u);
								if (!L->contains(lifetimeUser)) {
									bool uniqueToLoop = true;
									for (auto mu : lifetimeUser->users()) {
										Instruction *instructionToMoveUse = cast<Instruction>(mu);
										if (!L->contains(instructionToMoveUse)) {
											cerr << "Use of loaded lifetime val is used outside loop - not parallelizable\n";
											u->dump();
											uniqueToLoop = false;
										}
									}
									//check operands are either constant or the lifetime value
									for (auto &op : lifetimeUser->operands()) {
										if (isa<Constant>(op) || cast<Value>(&op) == actualLifetimeVal) {
											//this is OK
										}
										else {
											cerr << "Argument to instruction that needs to be moved belongs outside loop - not parallelizable\n";
											u->dump();
											return false;
										}
									}
									if (uniqueToLoop) {
										privateLoopVarUses.insert(u);
									}
									else {
										return false;
									}
								}
							}
							argValues.insert(actualLifetimeVal);
							lifetimeValues.insert(actualLifetimeVal);
							cerr << "pass in latter as argument\n";
						}
						else {
							cerr << "lifetime start in loop doesn't end until outside the loop - not parallelizable\n";
							return false;
						}
					}
				}
			}
		}
	}

	//case: find the rest - all values used in loop not defined inside it
	for (auto bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			// find all operands used in every instruction in the loop
			if (!isa<CallInst>(i)) {
				for (auto &op : i.operands()) {
					if (isa<Value>(op)) {
						if (isa<Instruction>(op)) {
							Instruction *inst = cast<Instruction>(&op);
							//if the instruction is a GEP and the memory it's accessing will definitely be local
							//i.e. is within a lifetime start/end, then we don't need to pass it in as an arg
							if (isa<GetElementPtrInst>(inst)) {
								if (lifetimeValues.find(inst->getOperand(0)) != lifetimeValues.end()) {
									cerr << "already added lifetime value, so not adding to args:\n";
									inst->dump();
									cerr << "\n";
									continue;
								}
							}
							//else if the value is an instruction declared outside the loop
							else if (!(L->contains(inst))) {
								//must pass in local value as arg so it is available
								if (argValues.find(inst) == argValues.end()) {
									argValues.insert(inst);
								}
							}
						}
						else {
							//if it is a Value declared as an argument or a global array
							//(dependency/alias analysis will catch writes and reads to global state later)
							Value *val = cast<Value>(&op);
							bool functionArg = false;
							for (auto &arg : F.getArgumentList()) {
								if (val == cast<Value>(&arg)) {
									functionArg = true;
								}
							}
							if (functionArg || isa<GlobalValue>(val)) {
								if (argValues.find(val) == argValues.end()) {
									argValues.insert(val);
								}
							}
						}
					}
					else {
						cerr << "non-value required for argument - not parallelizable\n";
						return false;
					}
				}
			}
			else {
				CallInst *call = cast<CallInst>(&i);
				int numArgs = call->getNumArgOperands();
				int i;
				for (i = 0; i < numArgs; i++) {
					Value *arg = call->getArgOperand(i);
					if (isa<Instruction>(arg)) {
						Instruction *inst = cast<Instruction>(arg);
						//if the instruction is a GEP and the memory it's accessing will definitely be local
						//i.e. is within a lifetime start/end, then we don't need to pass it in as an arg
						if (isa<GetElementPtrInst>(inst)) {
							if (lifetimeValues.find(inst->getOperand(0)) != lifetimeValues.end()) {
								cerr << "already added lifetime value, so not adding to args:\n";
								inst->dump();
								cerr << "\n";
								continue;
							}
						}
						//else if the value is an instruction declared outside the loop
						else if (!(L->contains(inst))) {
							//must pass in local value as arg so it is available
							if (argValues.find(inst) == argValues.end()) {
								argValues.insert(inst);
							}
						}
					}
					else {
						//if it is a Value declared as an argument or global array
						Value *val = arg;
						bool functionArg = false;
						for (auto &farg : F.getArgumentList()) {
							if (val == cast<Value>(&farg)) {
								functionArg = true;
							}
						}
						if (isa<GlobalValue>(val) || functionArg) {
							if (argValues.find(val) == argValues.end()) {
								argValues.insert(val);
							}
						}
					}
				}
			}
		}
	}

	//store arguments in list for extractor to use
	list<Value *> argArgs;
	for (auto v : argValues) {
		cerr << "value must be passed as an argument\n";
		v->dump();
		argArgs.push_back(v);
	}
	cerr << "\n";

	// Find all values that will need to be returned by each thread
    // for each instruction in loop, test to see whether there are uses outside the loop
	// add these to the return list along with instructions to replace it in
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			//check for users of values calculated inside the loop, outside the loop
			for (auto u : i.users()) {
				Instruction *use = cast<Instruction>(u);
				if (!L->contains(use)) {
					//we don't want duplicates
					bool first = true;
					pair <multimap<Value *, Value *>::iterator, multimap<Value *, Value *>::iterator> range = returnValues.equal_range(&i);
					for (multimap<Value *, Value *>::iterator it = range.first; it != range.second; ++it) {
						if (it->second == use) {
							first = false;
							break;
						}
					}
					if (first) {
						cerr << "use of value in loop that is also used after:\n";
						i.dump();
						use->dump();
						returnValues.insert(make_pair(&i, use));
					}
				}
			}
		}
	}

	//DEPENDENCY ANALYSIS
	set<Instruction *> dependentInstructions;
    // get all read / write instructions in the loop and check that all write indexes depend on the outer phi value
	bool allWriteDependentOnPhi = getDependencies(L, outerPhi, dependentInstructions, lifetimeValues);
	if (!allWriteDependentOnPhi) {
		//writes to the same location across threads will cause error
		cerr << "not parallelizable as write to array found not dependent on i\n";
		return false;
	}
	// get dependency information for all found read / write instructions - any non - zero ->not parallelizable
	dependencies = findDistanceVectors(dependentInstructions, DA);
	if (dependencies.size() > 0) {
		for (auto d : dependencies) {
			if (isa<StoreInst>(d->getSrc()) || isa<StoreInst>(d->getDst())) {
				cerr << "Has read-write/write-read/write-write dependency so not parallelizable\n";
				return false;
			}
			//else the dependency is just between to reads so OK
		}
	}

	//ALIAS ANALYSIS
	//store all arrays accessed in the loop to check for aliasing
	set<Value *> allarrays;
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (isa<LoadInst>(i) || isa<StoreInst>(i)) {
				//corresponding GEP
				Value *memAddress = nullptr;
				//and array
				Value *arrayVal = nullptr;
				if (isa<LoadInst>(i)) {
					memAddress = i.getOperand(0);
					if (isa<Instruction>(memAddress)) {
						arrayVal = cast<Instruction>(memAddress)->getOperand(0);
					}
					else {
						cerr << "Unexpected value in load instruction - not parallelizable\n";
						i.dump();
						cerr << "\n";
						return false;
					}
				}
				if (isa<StoreInst>(i)) {
					memAddress = i.getOperand(1);
					if (isa<Instruction>(memAddress)) {
						arrayVal = cast<Instruction>(memAddress)->getOperand(0);
					}
					else {
						cerr << "Unexpected value in store instruction - not parallelizable\n";
						i.dump();
						cerr << "\n";
						return false;
					}
				}
				allarrays.insert(arrayVal);
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

	// FUNCTION CALLS
	// If there are any call instructions that may have side - effects -> not parallelizable
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			if (isa<CallInst>(i)) {
				CallInst *call = cast<CallInst>(&i);
				Function *callee = call->getCalledFunction();
				if (callee == lifetimeStart || callee == lifetimeEnd) {
					call->dump();
					cerr << "lifetime start and end are safe functions\n";
				}
				else if (callee == symTab.lookup(StringRef("erand48"))) {
					cerr << "assume erand is OK for now\n";
				}
				else if (callee->onlyReadsMemory()) {
					cerr << "call to function found but it doesn't write to memory\n";
				}
				else {
					cerr << "call to function that may write to memory found\n";
					//check if memory altered is only in arguments and it's different for each loop
					if (callee->onlyAccessesArgMemory()) {
						cerr << "But only accesses arg values, args are:\n";
						for (auto &arg : call->operands()) {
							bool par = true;
							arg->dump();
							if (isa<Instruction>(arg)) {
								Instruction *a = cast<Instruction>(arg);
								if (a->getType()->isPointerTy()) {
									if (isDependentOnInductionVariable(a, outerPhi, false) || lifetimeValues.find(a) != lifetimeValues.end()) {
										//it should be a different location each iteration
										cerr << "This pointer argument is dependent on the outer loop induction variable so could be parallelizable\n";
									}
									else {
										cerr << "This pointer argument is perhaps not unique to each thread\n";
										par = false;
									}
								}
							}
							else {
								cerr << "This argument is perhaps not unique to each thread\n";
								par = false;
							}
							if (!par) {
								cerr << "call to function that may write to same memory across iterations found - not parallelizable\n";
								callee->dump();
								cerr << "\n";
								return false;
							}
						}
					}
					else {
						cerr << "call to function that may write to same memory across iterations found - not parallelizable\n";
						callee->dump();
						cerr << "\n";
						return false;
					}
				}
			}
		}
	}

	//attempt to find trip count
	unsigned int tripCount = SE.getSmallConstantTripCount(L);
	cerr << "trip count is: " << tripCount << "\n";

	//final checks for expected loop form
	if (L->getExitingBlock() == nullptr) {
		cerr << "loop has no exiting block - not parallelizable";
		return false;
	}
	if (L->getExitBlock() == nullptr) {
		cerr << "loop has no exit block - not parallelizable";
		return false;
	}

	//store results of analysis
	bool parallelizable = true;
	LoopDependencyData *data = new LoopDependencyData(outerPhi, argArgs, cast<Instruction>(outerBranch->getOperand(0)), L, dependencies, noOfInductionPhiNodes, 
														startIt, finalIt, tripCount, parallelizable, returnValues, accumulativePhiNodes, otherPhiNodes, lifetimeValues,
														voidCastsForLoop, privateLoopVarUses, outerPhiStep);
	
	if (results.find(&F) == results.end()) {
		results.insert(make_pair(&F, list<LoopDependencyData *>()));
	}
	results.find(&F)->second.push_back(data);
	
	return true;
}

//Follow operands back from a pointer instruction to find out whether a GEP index depends on the phi node
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
//there is write instruction not dependent on the induction variable (i.e. can't parallelize
// for a write, in each index position of the corresponding GEP instruction, there must be at least one that depends on the outer loop induction phi node,
// or the memory must be unique for each thread (i.e. be in lifetime values)
// (otherwise many threads could write to the same place at the same time - not parallelizable)
bool IsParallelizableLoopPass::getDependencies(Loop *L, PHINode *phi, set<Instruction *> &dependents, set<Value *> lifetimeValues) {
	bool parallelizable = true;
	bool dependent, lifetimeVal;
	for (auto &bb : L->getBlocks()) {
		for (auto &i : bb->getInstList()) {
			dependent = false;
			lifetimeVal = false;
			if (isa<LoadInst>(&i)) {
				//case : load
				//get the memory location pointer
				if (isa<Instruction>(i.getOperand(0))) {
					Instruction *ptr = cast<Instruction>(i.getOperand(0));
					if (lifetimeValues.find(ptr->getOperand(0)) == lifetimeValues.end()) {
						dependent = isDependentOnInductionVariable(ptr, phi, true);
					}
					else {
						lifetimeVal = true;
						dependent = true;
					}
					if (dependent) {
						i.dump();
						cerr << "\n";
					}
				}
				else {
					//the place we're reading from isn't a GEP instruction so not dependent on i
				}
			}
			else if (isa<StoreInst>(&i)) {
				//case : write
				if (isa<Instruction>(i.getOperand(1))) {
					Instruction *ptr = cast<Instruction>(i.getOperand(1));
					if (lifetimeValues.find(ptr->getOperand(0)) == lifetimeValues.end()) {
						dependent = isDependentOnInductionVariable(ptr, phi, false);
					}
					else {
						lifetimeVal = true;
						dependent = true;
					}
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
				else {
					i.dump();
					cerr << "Write instruction found location isn't a GEP instruction, not parallelizable\n\n";
					parallelizable = false;
				}
			}
			if (dependent && !lifetimeVal) {
				dependents.insert(&i);
			}
		}
	}
	return parallelizable;
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

bool IsParallelizableLoopPass::checkPhiIsAccumulative(PHINode *inst, Loop *L, int &opcode){
	cerr << "non-induction phi-node found in outer loop\n";
	inst->dump();
	cerr << "\n";

	//find what the repeated operation is - only accept commutative operations
	//i.e. case Add, case FAdd, case Mul, case FMul, case And, case Or, case Xor

	for (auto u : inst->users()) {
		if (!isa<PHINode>(u)) {
			cerr << "new accumulative phi value:\n";
			u->dump();

			if (isa<Instruction>(u)) {
				Instruction *incomingInstruction = cast<Instruction>(u);
				opcode = incomingInstruction->getOpcode();
				if (Instruction::isCommutative(opcode)) {
					return true;
				}
				else {
					cerr << "phi variable op not commutative so can't be parallelized\n";
					return false;
				}
			}
			else {
				//TEMPORARY
				cerr << "next phi node value that's not an instruction found\n";
				u->dump();
				cerr << "\n";
				return false;
			}
			break;
		}
	}
	return false;
}

list<Dependence *> IsParallelizableLoopPass::findDistanceVectors(set<Instruction *> &dependentInstructions, DependenceAnalysis *DA) {
	//find distance vectors for loop induction dependent read/write instructions
	list<Dependence *> dependencies;
	if (dependentInstructions.size() > 1) {
		//(*(dependentInstructions.begin()))->dump();
		//(*(dependentInstructions.end()--))->dump();
		for (auto si : dependentInstructions) {
			//set<Instruction *>::iterator si = dependentInstructions.begin(); si != dependentInstructions.end()--; si++) {
			Instruction *i1 = si;
			//auto si2 = dependentInstructions.find(si)++;
			auto si2 = dependentInstructions.find(si);
			while (si2 != dependentInstructions.end()) {
				Instruction *i2 = (*si2);
				cerr << "trying to find dependence between";
				i1->dump();
				i2->dump();
				unique_ptr<Dependence> d = DA->depends(i1, i2, true);

				if (d != nullptr) {
					// direction: NONE = 0, LT = 1, EQ = 2, LE = 3, GT = 4, NE = 5, GE = 6, ALL = 7
					const SCEV *scev = (d->getDistance(1));
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
						cerr << "dependency found between:\n";
						i1->dump();
						i2->dump();
						cerr << "\n";
						dependencies.push_back(d.release());
					}
				}
				si2++;
			}
		}
	}
	return dependencies;
}


char IsParallelizableLoopPass::ID = 0;
map<Function *, list<LoopDependencyData *>> IsParallelizableLoopPass::results;

/* static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable"); */

FunctionPass *parallelize::createAnalysisParallelizationPass() {
	return new IsParallelizableLoopPass();
}
