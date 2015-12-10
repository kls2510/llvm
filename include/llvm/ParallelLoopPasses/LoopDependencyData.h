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
	Loop *loop;
	list<Dependence *> dependencies;
	int noOfPhiNodes;
	bool parallelizable;

public:
	LoopDependencyData(Loop *L, list<Dependence *> d, int phi, Value *startIt, Value *finalIt, bool parallelizable) {
		loop = L, dependencies = d, noOfPhiNodes = phi, this->parallelizable = parallelizable, this->startIt = startIt, this->finalIt = finalIt;
	}

	Loop *getLoop();

	list<Dependence *> getDependencies();

	int getNoOfPhiNodes();

	int getDistance(Dependence *d);

	void print();

	bool isParallelizable();

	Value *getStartIt();

	Value *getFinalIt();

};

#endif