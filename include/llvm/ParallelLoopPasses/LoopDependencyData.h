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
	Loop *loop;
	list<unique_ptr<Dependence>> dependencies;
	int noOfPhiNodes;

public:
	LoopDependencyData(Loop *L, list<unique_ptr<Dependence>> d, int phi) {
		loop = L, dependencies = d, noOfPhiNodes = phi;
	}

	Loop *getLoop();

	list<unique_ptr<Dependence>> getDependencies();

	int getNoOfPhiNodes();

	int getDistance(unique_ptr<Dependence> d);

	void print();

};

#endif