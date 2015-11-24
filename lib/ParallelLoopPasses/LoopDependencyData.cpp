#include "LoopDependencyData.h"
#include <iostream>

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

int LoopDependencyData::getDistance(unique_ptr<Dependence> d) {
	cout << "calculating distance\n";
	const SCEV *scev = (d->getDistance(1));
	int distance;
	if (scev != nullptr && isa<SCEVConstant>(scev)) {
		const SCEVConstant *scevConst = cast<SCEVConstant>(scev);
		distance = *(int *)(scevConst->getValue()->getValue()).getRawData();
	}
	else {
		distance = 0;
	}
	cout << "distance calculated\n";
	return distance;
}

void LoopDependencyData::print() {
	cout << "For loop\n";
	loop->dump();
	cout << "Number of PHI nodes = " << noOfPhiNodes << "\n";
	cout << "and dependencies:\n";
	if (dependencies.size() > 0) {
		cout << "there are " << dependencies.size() << "\n";
		for (list<unique_ptr<Dependence>>::iterator i = dependencies.begin(); i != dependencies.end(); i++) {
			//int distance = getDistance(*i);
			Instruction *inst1 = (*i)->getSrc();
			Instruction *inst2 = (*i)->getDst();
			cout << "Dependency between\n";
			inst1->dump();
			inst2->dump();
			//cout << "with distance = " << distance << "\n";
		}
	}
	else {
		cout << "NONE\n";
	}
}

