#ifndef IS_PARALLELIZABLE_LOOP_PASS_H
#define IS_PARALLELIZABLE_LOOP_PASS_H

#include "llvm/IR/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/DependenceAnalysis.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/ParallelLoopPasses/LoopDependencyData.h"
#include "llvm/Transforms/Scalar.h"
#include <iostream>
#include <string>
#include <set>

using namespace llvm;
using namespace std;

namespace parallelize {
	/*
	IsParallelizablePass detects loops in a function's IR and determines whether
	each is parallelizable (including already or after some transform) or not
	*/
	class IsParallelizableLoopPass : public FunctionPass {
	public:
		//ID of the pass
		static char ID;

		//DependenceAnalysis class
		DependenceAnalysis *DA;

		//AliasAnalysis class
		AliasAnalysis *AA;

		//Map containing all loops and dependencies associated with each function
		//static map<Function&, list<LoopDependencyData *>> results;
		static list<LoopDependencyData *> results;

		//Constructor
		IsParallelizableLoopPass() : FunctionPass(ID) {	}

		//Set LoopInfo pass to run before this one so we can access its results
		void getAnalysisUsage(AnalysisUsage &AU) const;

		virtual bool runOnFunction(Function &F);

		static list<LoopDependencyData *> getResultsForFunction(Function &F);

	private:
		//runs the actual analysis
		bool isParallelizable(Loop *L, Function &F, ScalarEvolution &SE);

		bool getDependencies(Loop *L, PHINode *phi, set<Instruction *> *dependents);
	};
}

#endif