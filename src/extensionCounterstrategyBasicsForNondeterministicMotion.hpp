#ifndef __EXTENSION_COUNTERSTRATEGY_BASICS_NONDETERMINISTIC_HPP
#define __EXTENSION_COUNTERSTRATEGY_BASICS_NONDETERMINISTIC_HPP

#include "gr1context.hpp"
#include <string>

/**
 * A class that computes an explicit state counterstrategy for an unrealizable specification
 */
template<class T> class XCounterStrategyBasicsNondeterministicMotion : public T {
protected:
    // Inherited stuff used
    using T::mgr;
    using T::lineNumberCurrentlyRead;
    using T::addVariable;
    using T::parseBooleanFormula;
    using T::strategyDumpingData;
    using T::livenessGuarantees;
    using T::livenessAssumptions;
    using T::varVectorPre;
    using T::varVectorPost;
    using T::varCubePostOutput;
    using T::varCubePostInput;
    using T::varCubePreOutput;
    using T::varCubePreInput;
    using T::varCubePre;
    using T::varCubePost;
    using T::safetyEnv;
    using T::safetySys;
    using T::winningPositions;
    using T::initEnv;
    using T::initSys;
    using T::preVars;
    using T::postVars;
    using T::variableTypes;
    using T::variables;
    using T::variableNames;
    using T::realizable;
    using T::determinize;
    using T::postInputVars;
    using T::postOutputVars;
    using T::doesVariableInheritType;
    using T::robotBDD;

    // Own variables local to this plugin
    SlugsVectorOfVarBFs preMotionStateVars{PreMotionState,this};
    SlugsVectorOfVarBFs postMotionStateVars{PostMotionState,this};
    SlugsVectorOfVarBFs postControllerOutputVars{PostMotionControlOutput,this};
    SlugsVarCube varCubePostMotionState{PostMotionState,this};
    SlugsVarCube varCubePostControllerOutput{PostMotionControlOutput,this};
    SlugsVarCube varCubePreMotionState{PreMotionState,this};
    SlugsVarCube varCubePreControllerOutput{PreMotionControlOutput,this};

    BF newLivenessAssumption;
    BF failingPreAndPostConditions = mgr.constantFalse();

public:

    XCounterStrategyBasicsNondeterministicMotion<T>(std::list<std::string> &filenames) : T(filenames) {}

/**
 * @brief Compute and print out (to stdout) an explicit-state counter strategy that is winning for
 *        the environment. The output is compatible with the old JTLV output of LTLMoP.
 *        This function requires that the unrealizability of the specification has already been
 *        detected and that the variables "strategyDumpingData" and
 *        "winningPositions" have been filled by the synthesis algorithm with meaningful data.
 * @param outputStream - Where the strategy shall be printed to.
 */
void extractAutomaticallyGeneratedLivenessAssumption() {
    
    // Create a new liveness assumption that says that always eventually, if an action/pre-state
    // combination may lead to a different position, then it is taken
    BF prePostMotionStatesDifferent = mgr.constantFalse();
    for (uint i=0;i<preMotionStateVars.size();i++) {
        prePostMotionStatesDifferent |= (preMotionStateVars[i] ^ postMotionStateVars[i]);
    }
    BF preMotionInputCombinationsThatCanChangeState = (prePostMotionStatesDifferent & robotBDD).ExistAbstract(varCubePostMotionState);
    newLivenessAssumption = (!preMotionInputCombinationsThatCanChangeState) | prePostMotionStatesDifferent;
    livenessAssumptions.push_back(newLivenessAssumption);
    if (!(newLivenessAssumption.isTrue())) {
        std::cerr << "Note: Automatically-generated liveness assumption also considered in the extraction step.\n";
    }
}
    
void computeAndPrintExplicitStateStrategy(std::ostream &outputStream) {

    // We don't want any reordering from this point onwards, as
    // the BDD manipulations from this point onwards are 'kind of simple'.
    mgr.setAutomaticOptimisation(false);

    // List of states in existance so far. The first map
    // maps from a BF node pointer (for the pre variable valuation) and a goal
    // to a state number. The vector then contains the concrete valuation.
    std::map<std::pair<size_t, std::pair<unsigned int, unsigned int> >, unsigned int > lookupTableForPastStates;
    std::vector<BF> bfsUsedInTheLookupTable;
    std::list<std::pair<size_t, std::pair<unsigned int, unsigned int> > > todoList;

    
    // Prepare positional strategies for the individual goals
    std::vector<std::vector<BF> > positionalStrategiesForTheIndividualGoals(livenessAssumptions.size());
    for (unsigned int i=0;i<livenessAssumptions.size();i++) {
        BF casesCovered = mgr.constantFalse();
        std::vector<BF> strategy(livenessGuarantees.size()+1);
        std::vector<BF> strategyAllPost(livenessGuarantees.size()+1);
        for (unsigned int j=0;j<livenessGuarantees.size()+1;j++) {
            strategy[j] = mgr.constantFalse();
            strategyAllPost[j] = mgr.constantFalse();
        }
        for (auto it = strategyDumpingData.begin();it!=strategyDumpingData.end();it++) {
            if (boost::get<0>(*it) == i) {
                //Have to cover each guarantee (since the winning strategy depends on which guarantee is being pursued)
                //Essentially, the choice of which guarantee to pursue can be thought of as a system "move".
                //The environment always to chooses that prevent the appropriate guarantee.
                strategy[boost::get<1>(*it)] |= boost::get<2>(*it).UnivAbstract(varCubePostOutput) & !(strategy[boost::get<1>(*it)].ExistAbstract(varCubePost));
                BF newCases = boost::get<3>(*it).ExistAbstract(varCubePostMotionState) & !casesCovered;
                strategyAllPost[boost::get<1>(*it)] |= newCases & boost::get<3>(*it);
                casesCovered |= newCases;
                // BF_newDumpDot(*this,strategy[boost::get<1>(*it)],NULL,"/tmp/strategy.dot"); 
                // BF_newDumpDot(*this,strategyAllPost[boost::get<1>(*it)],NULL,"/tmp/strategyAllPost.dot"); 
            }
        }
        positionalStrategiesForTheIndividualGoals[i] = strategy;//strategyAllPost;
    }
    
    // Prepare initial to-do list from the allowed initial states. Select a single initial input valuation.

    // TODO: Support for non-special-robotics semantics
    BF todoInit = (winningPositions & initEnv & initSys);
    BF_newDumpDot(*this,initSys,NULL,"/tmp/initSys.dot"); 
    BF_newDumpDot(*this,winningPositions & initEnv & initSys,NULL,"/tmp/winningAndInit.dot"); 
    while (!(todoInit.isFalse())) {
        BF concreteState = determinize(todoInit,preVars);
        
        //find which liveness guarantee is being prevented (finds the first liveness in order specified)
        // Note by Ruediger here: Removed "!livenessGuarantees[j]" as condition as it is non-positional
        unsigned int found_j_index = 0;
        for (unsigned int j=0;j<livenessGuarantees.size();j++) {
            if (!(concreteState & positionalStrategiesForTheIndividualGoals[0][j]).isFalse()) {
                found_j_index = j;
                break;
            }
        }
            
        std::pair<size_t, std::pair<unsigned int, unsigned int> > lookup = std::pair<size_t, std::pair<unsigned int, unsigned int> >(concreteState.getHashCode(),std::pair<unsigned int, unsigned int>(0,found_j_index));
        lookupTableForPastStates[lookup] = bfsUsedInTheLookupTable.size();
        bfsUsedInTheLookupTable.push_back(concreteState);
        //from now on use the same initial input valuation (but consider all other initial output valuations)
        todoInit &= !concreteState;
        todoList.push_back(lookup);
    }

    // Extract strategy
    while (todoList.size()>0) {
        std::pair<size_t, std::pair<unsigned int, unsigned int> > current = todoList.front();
        todoList.pop_front();
        unsigned int stateNum = lookupTableForPastStates[current];
        BF currentPossibilities = bfsUsedInTheLookupTable[stateNum];

        // BF robotAllowedMoves = robotBDD.ExistAbstract(varCubePre).SwapVariables(varVectorPre,varVectorPost);
        BF robotAllowedMoves = mgr.constantTrue();
        // if (!(currentPossibilities & robotAllowedMoves).isFalse()){ 
        // Print state information
        outputStream << "State " << stateNum << " with rank (" << current.second.first << "," << current.second.second << ") -> <";
        bool first = true;
        for (unsigned int i=0;i<variables.size();i++) {
            if (doesVariableInheritType(i,Pre)) {
                if (first) {
                    first = false;
                } else {
                    outputStream << ", ";
                }
                outputStream << variableNames[i] << ":";
                outputStream << (((currentPossibilities & variables[i]).isFalse())?"0":"1");
            }
        }

        outputStream << ">\n\tWith successors : ";
        first = true;

        // Can we enforce a deadlock?
        BF possibilitiesAndRobotBDD = safetyEnv & currentPossibilities & robotBDD;
        BF_newDumpDot(*this,currentPossibilities,NULL,"/tmp/AcurrentPossibilitiesUsedInDeadlockCheck.dot");
        // BF_newDumpDot(*this,!safetySys,NULL,"/tmp/AnegatedSafetySys.dot");
        // BF_newDumpDot(*this,currentPossibilities & robotBDD,NULL,"/tmp/AcurrentPossibilitiesAndRobotBDD.dot");
        BF deadlockInput = mgr.constantTrue();
        while (!possibilitiesAndRobotBDD.isFalse()) {
            BF possibilitiesForThisControl = determinize(possibilitiesAndRobotBDD, postControllerOutputVars);
            possibilitiesAndRobotBDD &= !possibilitiesForThisControl;
            deadlockInput &= (possibilitiesForThisControl & safetyEnv & !safetySys).ExistAbstract(varCubePostMotionState).ExistAbstract(varCubePostControllerOutput);
            
            // BF_newDumpDot(*this,possibilitiesForThisControl,NULL,"/tmp/ApossibilitiesForThisControl.dot");
            // BF_newDumpDot(*this,(possibilitiesForThisControl & !safetySys),NULL,"/tmp/ApossibilitiesThatNegateSys.dot");
            // BF_newDumpDot(*this,(possibilitiesForThisControl & !safetySys).ExistAbstract(varCubePostMotionState).ExistAbstract(varCubePostMotionState),NULL,"/tmp/AexistAbstractedPossibilitiesThatNegateSys.dot");
            // BF_newDumpDot(*this,deadlockInput,NULL,"/tmp/AdeadlockInput.dot");
        }
        if (deadlockInput!=mgr.constantFalse()) {
            addDeadlocked(deadlockInput, current, bfsUsedInTheLookupTable,  lookupTableForPastStates, outputStream);
        } else {

            // No deadlock in sight -> Jump to a different liveness guarantee if necessary.
            while ((currentPossibilities & positionalStrategiesForTheIndividualGoals[current.second.first][current.second.second])==mgr.constantFalse()) current.second.second = (current.second.second + 1) % livenessGuarantees.size();
            currentPossibilities &= positionalStrategiesForTheIndividualGoals[current.second.first][current.second.second];
            assert(currentPossibilities != mgr.constantFalse());
            BF remainingTransitions = currentPossibilities;

            if (!(remainingTransitions.ExistAbstract(varCubePostMotionState).ExistAbstract(varCubePostControllerOutput).ExistAbstract(varCubePre)).isTrue()){
                BF_newDumpDot(*this,remainingTransitions,NULL,"/tmp/weHaveTrueEvents.dot");
            }

            // Choose one next input and stick to it!
            // BF_newDumpDot(*this,remainingTransitions,NULL,"/tmp/remainingTransitionsBefore.dot");
            remainingTransitions = determinize(safetyEnv & remainingTransitions & robotAllowedMoves,postInputVars);
            // BF_newDumpDot(*this,remainingTransitions,NULL,"/tmp/remainingTransitionsAfter.dot");

            // Switching goals
            while (!(remainingTransitions & robotBDD & safetySys).isFalse()) {

                BF safeTransition = remainingTransitions & robotBDD & safetySys;
                BF possibleNextControlsOverTheModel = determinize(safeTransition, postControllerOutputVars);

                //Mark which controller output has been captured by this case. Use the same control for other successors
                BF controlCaptured = possibleNextControlsOverTheModel.ExistAbstract(varCubePostMotionState).ExistAbstract(varCubePostInput);
                remainingTransitions &= !controlCaptured;
                // BF_newDumpDot(*this,controlCaptured,NULL,"/tmp/controlCaptured.dot");
                // BF_newDumpDot(*this,remainingTransitions,NULL,"/tmp/remainingTransitionsAfterRemoval.dot");

                // BF_newDumpDot(*this,remainingTransitions,NULL,"/tmp/remainingTransitions.dot");
                // BF_newDumpDot(*this,safeTransition,NULL,"/tmp/safeTransition.dot");
                // BF_newDumpDot(*this,robotAllowedMoves,"PreMotionControlOutput PreMotionState","/tmp/robotAllowedMoves.dot");
                // BF_newDumpDot(*this,safetySys,NULL,"/tmp/safetySys.dot");

                // pick a concrete postMotionState
                // BF_newDumpDot(*this,possibleNextControlsOverTheModel,NULL,"/tmp/possibleNextControlsOverTheModel.dot"); 
                // BF_newDumpDot(*this,safetyEnv & possibleNextControlsOverTheModel,NULL,"/tmp/winningPostMotionStatesForThisControl.dot");
                
                //TODO: the special liveness requirement really belongs in the game solving insead of re-including it here
                BF newCombination = determinize(newLivenessAssumption & safetyEnv & possibleNextControlsOverTheModel, postMotionStateVars);

                // BF_newDumpDot(*this,newLivenessAssumption & safetyEnv & possibleNextControlsOverTheModel,NULL,"/tmp/postMotionStateOptions.dot");
                // BF_newDumpDot(*this,newCombination,NULL,"/tmp/newCombination.dot");

                // Jump as much forward  in the liveness assumption list as possible ("stuttering avoidance")
                unsigned int nextLivenessAssumption = current.second.first;
                bool firstTry = true;
                while (((nextLivenessAssumption != current.second.first) | firstTry) && !((livenessAssumptions[nextLivenessAssumption] & newCombination).isFalse())) {
                    nextLivenessAssumption  = (nextLivenessAssumption + 1) % livenessAssumptions.size();
                    firstTry = false;
                }
                
                // We don't need the pre information from the point onwards anymore.
                newCombination = newCombination.ExistAbstract(varCubePre).SwapVariables(varVectorPre,varVectorPost);

                unsigned int tn;

                std::pair<size_t, std::pair<unsigned int, unsigned int> > target;

                target = std::pair<size_t, std::pair<unsigned int, unsigned int> >(newCombination.getHashCode(),std::pair<unsigned int, unsigned int>(nextLivenessAssumption, current.second.second));

                if (lookupTableForPastStates.count(target)==0) {
                    tn = lookupTableForPastStates[target] = bfsUsedInTheLookupTable.size();
                    bfsUsedInTheLookupTable.push_back(newCombination);
                    todoList.push_back(target);
                } else {
                    tn = lookupTableForPastStates[target];
                }

                // Print
                if (first) {
                    first = false;
                } else {
                    outputStream << ", ";
                }
                outputStream << tn;

            }
        }

        outputStream << "\n";
    }
    }


    //This function adds a new successor-less "state" that captures the deadlock-causing input values
    //The outputvalues are omitted (indeed, no valuation exists that satisfies the system safeties)
    //Format compatible with JTLV counterstrategy

    void addDeadlocked(BF targetPositionCandidateSet, std::pair<size_t, std::pair<unsigned int, unsigned int> > current, std::vector<BF> &bfsUsedInTheLookupTable, std::map<std::pair<size_t, std::pair<unsigned int, unsigned int> >, unsigned int > &lookupTableForPastStates, std::ostream &outputStream) {
    
    // find the deadlock state's predecessor and store it..
    unsigned int stateNum = lookupTableForPastStates[current];

    // BF_newDumpDot(*this,targetPositionCandidateSet,NULL,"/tmp/targetPositionCandidateSet.dot");
    BF newCombination = determinize(targetPositionCandidateSet, postVars) ;
    BF_newDumpDot(*this,newCombination,NULL,"/tmp/newCombinationAddDeadocked.dot");
    newCombination = newCombination.ExistAbstract(varCubePreControllerOutput).SwapVariables(varVectorPre,varVectorPost);
    
    
    std::pair<size_t, std::pair<unsigned int, unsigned int> > target = std::pair<size_t, std::pair<unsigned int, unsigned int> >(newCombination.getHashCode(),std::pair<unsigned int, unsigned int>(current.second.first, current.second.second));
    unsigned int tn;

    BF failingPostInputs = newCombination.ExistAbstract(varCubePreControllerOutput).ExistAbstract(varCubePreMotionState).ExistAbstract(varCubePost).SwapVariables(varVectorPre,varVectorPost);
    BF failingPreCommandsAndMotionStates = bfsUsedInTheLookupTable[stateNum].ExistAbstract(varCubePost).ExistAbstract(varCubePreInput);
    failingPreAndPostConditions |= failingPostInputs & failingPreCommandsAndMotionStates;

    if (lookupTableForPastStates.count(target)==0) {
        tn = lookupTableForPastStates[target] = bfsUsedInTheLookupTable.size();
        bfsUsedInTheLookupTable.push_back(newCombination);   
        outputStream << tn << "\n";
        
        //Note that since we are printing here, we usually end up with the states being printed out of order.
        //TOTO: print in order
        outputStream << "State " << tn << " with rank (" << current.second.first << "," << current.second.second << ") -> <";
        bool first = true;
        for (unsigned int i=0;i<variables.size();i++) {
            if (doesVariableInheritType(i,PreInput)) {
                if (first) {
                    first = false;
                } else {
                    outputStream << ", ";
                }
                outputStream << variableNames[i] << ":";
                outputStream << (((newCombination & variables[i]).isFalse())?"0":"1");
            }
        }
        outputStream << ">\n\tWith no successors.";
    
    } else {
        tn = lookupTableForPastStates[target];
        outputStream << tn;
    }
    
}


    static GR1Context* makeInstance(std::list<std::string> &filenames) {
        return new XCounterStrategyBasicsNondeterministicMotion<T>(filenames);
    }
};

#endif
