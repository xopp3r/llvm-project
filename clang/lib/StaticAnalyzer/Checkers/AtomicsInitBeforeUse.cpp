//===-- AtomicsInitBeforeUseChecker.cpp -----------------------------------*-
// C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MISRA Rule 9.7 Atomic objects shall be appropriately initialized before being
// accessed
//
// Amplification
// Initialization of atomic objects shall be completed before accessing them:
// For objects that do not have static storage duration, initialization shall be
// included in their declaration using the assignment operator = or using the
// Standard Library function atomic_init(), before any other access. For objects
// of static storage duration, the default initialization is sufficient.
//
// Rationale
// An atomic object is to be initialized before it is accessed. Concurrent
// access to the object being initialized, even via an atomic operation,
// constitutes a data race. The atomicinit() function initializes atomic
// objects, including any additional state that the implementation might need to
// carry for the atomic object. However, it does not avoid data races. Because
// of the potential initialization of the implementation state, atomic_init()
// cannot be replaced by other access functions, e.g. atomic_store().
// Initialization of atomic objects inside of threads would impose constraints
// on thread ordering which are hard to ensure or verify. An explicit
// protection, e.g. by use of a mutex, would make atomicity unnecessary.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

using namespace clang;
using namespace ento;

namespace {

struct AtomicState {
private:
  bool Init;
  AtomicState(bool Initialized) : Init(Initialized) {}

public:
  bool isInitialized() const { return Init; }

  static AtomicState getInitialized() { return AtomicState(true); }
  static AtomicState getUninitialized() { return AtomicState(false); }

  bool operator==(const AtomicState &X) const { return Init == X.Init; }

  void Profile(llvm::FoldingSetNodeID &ID) const { ID.AddBoolean(Init); }
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AtomicRegionMap, const MemRegion *, AtomicState)

namespace {

class AtomicsInitBeforeUseChecker
    : public Checker<check::PreCall, check::PointerEscape, check::Bind,
                     check::Location> {

  const CallDescription AtomicInitFn{CDM::SimpleFunc, {"atomic_init"}, 2};

  const BugType UninitializedAccess{this, "Uninitialized atomic access",
                                    "MISRA rule 9.7"};

  const BugType DoubleInit{this, "Double initialization of atomic object",
                           "MISRA rule 9.7"};

  const BugType UninitializedEscape{this, "Uninitialized atomic object escapes",
                                    "MISRA rule 9.7"};

  void reportUninitializedAccess(const MemRegion *R, const Stmt *S,
                                 CheckerContext &C) const;
  void reportDoubleInit(const MemRegion *R, const CallEvent &Call,
                        CheckerContext &C) const;
  void reportUninitializedEscape(const MemRegion *R, const Stmt *S,
                                 CheckerContext &C) const;

  bool isAtomicType(QualType T) const { return T->isAtomicType(); }

  const MemRegion *getAtomicRegion(SVal Loc) const {
    const MemRegion *R = Loc.getAsRegion();
    if (not R)
      return nullptr;

    const TypedValueRegion *TVR = R->getAs<TypedValueRegion>();
    if (not TVR)
      return nullptr;

    if (not isAtomicType(TVR->getValueType()))
      return nullptr;

    return R;
  }

  std::pair<ProgramStateRef, const AtomicState *>
  getOrCreateState(ProgramStateRef State, const MemRegion *R) const {
    const AtomicState *AS = State->get<AtomicRegionMap>(R);
    if (AS)
      return {State, AS};

    AtomicState NewState = AtomicState::getUninitialized();
    if (auto *VR = dyn_cast<VarRegion>(R->getBaseRegion())) {
      if (VR->getDecl()->hasGlobalStorage()) {
        // static storage duration
        NewState = AtomicState::getInitialized();
      } else {
        // automatic
        NewState = AtomicState::getUninitialized();
      }
    } else {
      // other: heap, ...
      NewState = AtomicState::getUninitialized();
    }

    State = State->set<AtomicRegionMap>(R, NewState);
    return {State, State->get<AtomicRegionMap>(R)};
  }

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    if (not AtomicInitFn.matches(Call))
      return;

    const SVal Arg = Call.getArgSVal(0);
    const MemRegion *R = getAtomicRegion(Arg);
    if (not R)
      return;

    ProgramStateRef State = C.getState();
    auto [NewState, AS] = getOrCreateState(State, R);
    if (AS->isInitialized()) {
      reportDoubleInit(R, Call, C);
    }

    NewState = NewState->set<AtomicRegionMap>(R, AtomicState::getInitialized());
    C.addTransition(NewState);
  }

  void checkBind(SVal Loc, SVal Val, const Stmt *S,
                 /*bool AtDeclInit,*/ CheckerContext &C) const {

    if (not isa<DeclStmt>(S)) // not AtDeclInit in newer versions
      return;

    const MemRegion *R = getAtomicRegion(Loc);
    if (not R)
      return;

    ProgramStateRef State = C.getState();
    State = State->set<AtomicRegionMap>(R, AtomicState::getInitialized());
    C.addTransition(State);
  }

  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const {

    const MemRegion *R = getAtomicRegion(Loc);
    if (not R)
      return;

    ProgramStateRef State = C.getState();
    auto [NewState, AS] = getOrCreateState(State, R);
    if (not AS->isInitialized()) {
      reportUninitializedAccess(R, S, C);
    }

    C.addTransition(NewState);
  }

  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const {

    if (Kind == PSK_DirectEscapeOnCall and Call and
        AtomicInitFn.matches(*Call)) {
      return State;
    }

    for (const auto *Sym : Escaped) {
      // const MemRegion *R = getAtomicRegion(Sym);
      const MemRegion *R =
          Sym->getOriginRegion(); // хз как получить MemoryRegion, на который
                                  // указывает убежавший указатель
      if (not R)
        continue;

      auto [NewState, AS] = getOrCreateState(State, R);
      State = NewState;

      if (not AS->isInitialized()) {
        llvm::errs() << "POINTER ESCAPE\n";
      }
    }
    return State;
  }
};

} // end anonymous namespace

void ento::registerAtomicsInitBeforeUse(CheckerManager &mgr) {
  mgr.registerChecker<AtomicsInitBeforeUseChecker>();
}

bool ento::shouldRegisterAtomicsInitBeforeUse(const CheckerManager &mgr) {
  return true;
}

void AtomicsInitBeforeUseChecker::reportUninitializedAccess(
    const MemRegion *R, const Stmt *S, CheckerContext &C) const {
  ExplodedNode *ErrNode = C.generateNonFatalErrorNode();
  if (not ErrNode)
    return;

  auto Report = std::make_unique<PathSensitiveBugReport>(
      UninitializedAccess, "Access to uninitialized atomic object", ErrNode);
  Report->markInteresting(R);
  Report->addRange(S->getSourceRange());

  C.emitReport(std::move(Report));
}

void AtomicsInitBeforeUseChecker::reportDoubleInit(const MemRegion *R,
                                                   const CallEvent &Call,
                                                   CheckerContext &C) const {
  ExplodedNode *ErrNode = C.generateNonFatalErrorNode();
  if (not ErrNode)
    return;

  auto Report = std::make_unique<PathSensitiveBugReport>(
      DoubleInit,
      "Atomic object already initialized; double initialization is undefined",
      ErrNode);
  Report->markInteresting(R);
  if (const Stmt *S = Call.getOriginExpr())
    Report->addRange(S->getSourceRange());

  C.emitReport(std::move(Report));
}
