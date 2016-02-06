#ifndef LOOP_DEPENDENCY_DATA_H
#define LOOP_DEPENDENCY_DATA_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include <list>

using namespace llvm;
using namespace std;

class LoopDependencyData {
private:
	Value *startIt;
	Value *finalIt;
	int tripCount;
	Loop *loop;
	list<Dependence *> dependencies;
	int noOfPhiNodes;
	bool parallelizable;
	multimap<Value *, Value *> returnValues;
	map<PHINode *, unsigned int> accumulativePhiNodes;
	Instruction *phi;
	Instruction *end;
	list<Value *> argValues;
	map<PHINode *, pair < const Value *, Value * >> otherPhiNodes;
	set<Value *> lifetimeValues;

public:
	LoopDependencyData(Instruction *IndPhi, list<Value *> argValues, Instruction *end, Loop *L, list<Dependence *> d, int phi, Value *startIt, Value *finalIt, int tripCount,
		bool parallelizable, multimap<Value *, Value *> returnValues, map<PHINode *, unsigned int> accumulativePhiNodes, map<PHINode *, pair<const Value *, Value *>> otherPhiNodes, set<Value *> lifetimeValues) {
		loop = L, dependencies = d, noOfPhiNodes = phi, this->parallelizable = parallelizable, this->startIt = startIt,
			this->finalIt = finalIt, this->tripCount = tripCount, this->returnValues = returnValues, this->accumulativePhiNodes = accumulativePhiNodes,
			this->phi = IndPhi, this->end = end, this->argValues = argValues, this->otherPhiNodes = otherPhiNodes, this->lifetimeValues = lifetimeValues;
	}

	Loop *getLoop();

	list<Dependence *> getDependencies();

	int getNoOfPhiNodes();

	int getDistance(Dependence *d);

	void print();

	bool isParallelizable();

	Value *getStartIt();

	Value *getFinalIt();

	int getTripCount();

	list<Value *> getReturnValues();

	list<Value *> getReplaceReturnValueIn(Value *returnValue);

	list<PHINode *> getOuterLoopNonInductionPHIs();

	unsigned int getPhiNodeOpCode(PHINode *phi);

	Instruction *getInductionPhi();

	Instruction *getExitCondNode();

	list<Value *> getArgumentArgValues();

	map<PHINode *, pair <const Value *, Value * >> getOtherPhiNodes();

	set<Value *> getLifetimeValues();
};

#endif