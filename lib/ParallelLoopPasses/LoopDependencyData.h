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

class LoopDependencyData {
private:
	Loop *loop;
	list<Dependence *> dependencies;
	int noOfPhiNodes;

public:
	LoopDependencyData(Loop *L, list<Dependence *> d, int phi) {
		loop = L, dependencies = d, noOfPhiNodes = phi;
	}

	Loop *getLoop();

	list<Dependence *> getDependencies();

	int getNoOfPhiNodes();

	int getDistance(Dependence *d);

	void print();

};