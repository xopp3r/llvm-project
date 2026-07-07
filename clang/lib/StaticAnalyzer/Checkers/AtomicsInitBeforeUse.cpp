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

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState_Fwd.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/Support/raw_ostream.h"
#include <utility>

using namespace clang;
using namespace ento;

namespace {

struct AtomicState {
private:
  enum Kind { Uninitialized, Initialized, Escaped } K;

  const Stmt *S;

  AtomicState(Kind k, const Stmt *s) : K(k), S(s) {}

public:
  bool isInitialized() const { return K == Initialized; }
  bool isUninitialized() const { return K == Uninitialized; }
  bool isEscaped() const { return K == Escaped; }

  static AtomicState getInitialized(const Stmt *s) {
    return AtomicState(Initialized, s);
  }
  static AtomicState getUninitialized(const Stmt *s) {
    return AtomicState(Uninitialized, s);
  }
  static AtomicState getEscaped(const Stmt *s) {
    return AtomicState(Escaped, s);
  }

  bool operator==(const AtomicState &X) const { return K == X.K and S == X.S; }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
    ID.AddPointer(S);
  }
};

} // end anonymous namespace

REGISTER_MAP_WITH_PROGRAMSTATE(AtomicRegionMap, const MemRegion *, AtomicState)

namespace {

class AtomicsInitBeforeUseChecker
    : public Checker<check::PreCall, check::PostCall,
                     check::PointerEscape, // check::Bind,
                     check::Location, check::PreStmt<ReturnStmt>,
                     check::PreStmt<DeclStmt>> {

  const CallDescription AtomicInitFn{CDM::SimpleFunc, {"atomic_init"}, 2};
  const CallDescription MallocFn{CDM::SimpleFunc, {"malloc"}, 1};
  const CallDescription CallocFn{CDM::SimpleFunc, {"calloc"}, 2};
  const CallDescription ReallocFn{CDM::SimpleFunc, {"realloc"}, 2};
  const CallDescription FreeFn{CDM::SimpleFunc, {"free"}, 1};

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

  bool isAtomicRegion(const MemRegion *R) const {
    if (not R)
      return false;
    const TypedValueRegion *TVR = R->getAs<TypedValueRegion>();
    if (not TVR)
      return false;

    QualType T = TVR->getValueType();
    if (T.isNull())
      return false;
    return T->isAtomicType();
  }

  bool hasStaticStorage(const MemRegion *R) const {
    if (const auto *VR = R->getBaseRegion()->getAs<VarRegion>())
      return VR->getDecl()->hasGlobalStorage();
    return false;
  }

  const AtomicState *getStateForRegion(const MemRegion *R,
                                       ProgramStateRef State) const {
    const AtomicState *AS = State->get<AtomicRegionMap>(R);

    while (not AS and R) {
      if (auto *ER = R->getAs<ElementRegion>()) {
        R = ER->getSuperRegion();
      } else if (auto *FR = R->getAs<FieldRegion>()) {
        R = FR->getSuperRegion();
      } else {
        break;
      }
      AS = State->get<AtomicRegionMap>(R);
    }

    return AS;
  }

  static QualType getDeepPointeeType(QualType T) {
  QualType Result = T, PointeeType = T->getPointeeType();
  while (!PointeeType.isNull()) {
    Result = PointeeType;
    PointeeType = PointeeType->getPointeeType();
  }
  return Result;
}

public:
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {
    if (AtomicInitFn.matches(Call)) {

        const SVal Arg = Call.getArgSVal(0);
        if (Arg.isUnknownOrUndef()) return;
        
        const QualType Type = Arg.getType(C.getASTContext());
        if (not getDeepPointeeType(Type)->isAtomicType()) 
          return;
        
        const MemRegion *R = Arg.getAsRegion();
        if (not R) return;

        ProgramStateRef State = C.getState();
        const AtomicState *AS = getStateForRegion(R, State);
        if (AS and AS->isInitialized()) {
          reportDoubleInit(R, Call, C);
        } else {
          ProgramStateRef State = C.getState();
          State = State->set<AtomicRegionMap>(
              R, AtomicState::getInitialized(Call.getOriginExpr()));
          C.addTransition(State);
        }

    } 
  }

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {
    if (FreeFn.matches(Call)) return; // TODO add other

    if (MallocFn.matches(Call) or CallocFn.matches(Call) or
        ReallocFn.matches(Call)) { // TODO add other mem alloc functions
          
      SVal RetVal = Call.getReturnValue();
      if (RetVal.isUnknownOrUndef()) return;
      
      const MemRegion *R = RetVal.getAsRegion();
      if (not R)
        return;

      ProgramStateRef State = C.getState();
      State = State->set<AtomicRegionMap>(
          R, AtomicState::getUninitialized(Call.getOriginExpr()));
      C.addTransition(State);

    } else {

       for (unsigned i = 0; i < Call.getNumArgs(); ++i) {

        const SVal Arg = Call.getArgSVal(i);
        if (Arg.isUnknownOrUndef()) continue;

        const QualType Type = Arg.getType(C.getASTContext());
        if (Type.isNull()) continue;

        if (not Type->isAtomicType() and not getDeepPointeeType(Type)->isAtomicType()) // TODO need to go only 1 level deep into 
          continue;

        const MemRegion *R = Arg.getAsRegion();
        if (not R)
          continue;

        const AtomicState *AS = getStateForRegion(R, C.getState());
        if (AS and AS->isEscaped()) 
          reportUninitializedEscape(R, Call.getArgExpr(i), C);
        
        bool analyzible = false;
        if (const auto *b = Call.getDecl()) analyzible = b->hasBody();

        if (not analyzible) 
          if (AS and not AS->isInitialized()) 
            reportUninitializedEscape(R, Call.getOriginExpr(), C);
          
        
      }
    }
  }

  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &C) const {
    const MemRegion *R = Loc.getAsRegion();
    if (not R)
      return;

    if (not isAtomicRegion(R))
      return;

    ProgramStateRef State = C.getState();
    const AtomicState *AS = getStateForRegion(R, State);

    if (not AS) {
      if (hasStaticStorage(R)) {
        State = State->set<AtomicRegionMap>(R, AtomicState::getInitialized(S));
        C.addTransition(State);
        return;
      }
      reportUninitializedAccess(R, S, C);
      return;
    }

    if (AS->isEscaped()) {
      reportUninitializedEscape(R, S, C);
    } else if (not AS->isInitialized()) {
      reportUninitializedAccess(R, S, C);
    }
  }

  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const {

    if (Kind == PSK_DirectEscapeOnCall and Call and
        AtomicInitFn.matches(*Call)) {
      return State;
    }
    MemRegionManager &MRM = State->getStateManager().getRegionManager();
    for (SymbolRef Sym : Escaped) {
      const SymbolicRegion *SR = MRM.getSymbolicRegion(Sym);

      if (not SR)
        continue;

      if (const AtomicState *AS = getStateForRegion(SR, State)) {
        if (not AS->isInitialized()) {
          State = State->set<AtomicRegionMap>(
              SR,
              AtomicState::getEscaped(Call ? Call->getOriginExpr() : nullptr));
        }
      }
    }
    return State;
  }

  void checkPreStmt(const DeclStmt *S, CheckerContext &C) const {
    if (not S)
      return;

    for (const Decl *D : S->decls()) {
      if (const VarDecl *VD = dyn_cast<VarDecl>(D)) {
        ProgramStateRef State = C.getState();
        const VarRegion *VR = State->getRegion(VD, C.getLocationContext());
        if (not VR)
          continue;

        if (VD->hasInit() or VD->hasConstantInitialization()) {
          State =
              State->set<AtomicRegionMap>(VR, AtomicState::getInitialized(S));
        } else {
          if (VD->hasGlobalStorage()) {
            State =
                State->set<AtomicRegionMap>(VR, AtomicState::getInitialized(S));
          } else {
            State = State->set<AtomicRegionMap>(
                VR, AtomicState::getUninitialized(S));
          }
        }

        C.addTransition(State);
      }
    }
  }

  void checkPreStmt(const ReturnStmt *S, CheckerContext &C) const {
    if (not S)
      return;

    const Expr *E = S->getRetValue();
    if (not E)
      return;

    const MemRegion *R = C.getSVal(E).getAsRegion();
    if (not R)
      return;

    ProgramStateRef State = C.getState();
    const AtomicState *AS = getStateForRegion(R, State);

    if (AS and AS->isEscaped()) {
      reportUninitializedEscape(R, S, C); // TODO verify, that it catches uninit escape
    }
  }
};

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

void AtomicsInitBeforeUseChecker::reportUninitializedEscape(
    const MemRegion *R, const Stmt *S, CheckerContext &C) const {
  ExplodedNode *ErrNode = C.generateNonFatalErrorNode();
  if (not ErrNode)
    return;

  auto Report = std::make_unique<PathSensitiveBugReport>(
      UninitializedEscape, "Uninitialized atomic object escapes", ErrNode);
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

} // end anonymous namespace

void ento::registerAtomicsInitBeforeUse(CheckerManager &mgr) {
  mgr.registerChecker<AtomicsInitBeforeUseChecker>();
}

bool ento::shouldRegisterAtomicsInitBeforeUse(const CheckerManager &mgr) {
  return true;
}
