#include "LoopDependencyData.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include <iostream>
#include <list>

using namespace llvm;
using namespace std;

Loop *LoopDependencyData::getLoop() {
	return loop;
}

list<Dependence *> LoopDependencyData::getDependencies() {
	return dependencies;
}

int LoopDependencyData::getNoOfPhiNodes() {
	return noOfPhiNodes;
}

int LoopDependencyData::getDistance(Dependence *d) {
	const SCEV *scev = (d->getDistance(1));
	int distance;
	if (scev != nullptr && isa<SCEVConstant>(scev)) {
		const SCEVConstant *scevConst = cast<SCEVConstant>(scev);
		distance = *(int *)(scevConst->getValue()->getValue()).getRawData();
	}
	else {
		distance = 0;
	}
	return distance;
}

void LoopDependencyData::print() {
	cout << "For loop\n";
	loop->dump();
	cout << "Number of PHI nodes = " << noOfPhiNodes << "\n";
	cout << "and dependencies:\n";
	if (dependencies.size() > 0) {
		for (list<Dependence *>::iterator i = dependencies.begin(); i != dependencies.end(); i++) {
			int distance = getDistance(*i);
			Instruction *inst1 = (*i)->getSrc();
			Instruction *inst2 = (*i)->getDst();
			cout << "Dependency between\n";
			inst1->dump();
			inst2->dump();
			cout << "with distance = " << distance << "\n";
		}
	}
	else {
		cout << "NONE\n";
	}
}

