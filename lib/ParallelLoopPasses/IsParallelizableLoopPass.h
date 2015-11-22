#ifndef ISPARALLELIZABLE_H
#define ISPARALLELIZABLE_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "LoopDependencyData.h"
#include <iostream>
#include <string>
#include <set>

using namespace llvm;
using namespace std;

namespace {
	/*
	IsParallelizablePass detects loops in a function's IR and determines whether
	each is parallelizable (including already or after some transform) or not
	*/
	struct IsParallelizableLoopPass : public FunctionPass {
		//ID of the pass
		static char ID;

		//DependenceAnalysis class
		DependenceAnalysis *DA;

		//AliasAnalysis class
		AliasAnalysis *AA;

		//Map containing all loops and dependencies associated with each function
		static map<StringRef, list<LoopDependencyData *>> results;

		//Constructor
		IsParallelizableLoopPass() : FunctionPass(ID) {	}

		//Set LoopInfo pass to run before this one so we can access its results
		void getAnalysisUsage(AnalysisUsage &AU) const;

		virtual bool runOnFunction(Function &F);

		static list<LoopDependencyData *> getResultsForFunction(Function &F);

	private:
		//runs the actual analysis
		bool isParallelizable(Loop *L, Function &F);

		void getDependencies(Instruction *inst, PHINode *phi, set<Instruction *> *dependents);
	};
}

char IsParallelizableLoopPass::ID = 0;
//define the static variable member
map<StringRef, list<LoopDependencyData *>> IsParallelizableLoopPass::results;
static RegisterPass<IsParallelizableLoopPass> reg("IsParallelizableLoopPass",
	"Categorizes loops into 2 categories per function; is parallelizable and is not parallelizable");


#endif