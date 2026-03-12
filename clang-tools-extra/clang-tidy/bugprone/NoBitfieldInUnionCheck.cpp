//===--- NoBitfieldInUnionCheck.cpp - clang-tidy --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NoBitfieldInUnionCheck.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"

using namespace clang::ast_matchers;

namespace clang::tidy::bugprone {

void NoBitfieldInUnionCheck::registerMatchers(MatchFinder *Finder) {
  const auto Matcher = fieldDecl(isBitField(), hasParent(tagDecl(isUnion()))).bind("errorprone_bitfield");
  Finder->addMatcher(Matcher, this);
}

void NoBitfieldInUnionCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *MatchedDecl = Result.Nodes.getNodeAs<FieldDecl>("errorprone_bitfield");
  diag(MatchedDecl->getLocation(), "bit field %0 is declared as a member of union, which is forbidden by MISRA rule 6.3")
      << MatchedDecl;
}

} // namespace clang::tidy::bugprone
