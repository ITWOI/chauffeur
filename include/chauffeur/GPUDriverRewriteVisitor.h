//
// Copyright (c) 2014-2015 Pantazis Deligiannis (p.deligiannis@imperial.ac.uk)
// This file is distributed under the MIT License. See LICENSE for details.
//

#ifndef GPUDRIVERREWRITEVISITOR_H
#define GPUDRIVERREWRITEVISITOR_H

#include "chauffeur/DriverInfo.h"
#include "chauffeur/AbstractDriverRewriteVisitor.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"


namespace chauffeur
{
  using namespace clang;

	class GPUDriverRewriteVisitor : public AbstractDriverRewriteVisitor
	{
	protected:
    virtual void InstrumentEntryPoints(FunctionDecl* FD, string fdFile);
    virtual void CreateCheckerFunction(FunctionDecl* FD, string fdFile);

    virtual string GetSharedStructStr(CallExpr *callExpr);

	public:
	  explicit GPUDriverRewriteVisitor(CompilerInstance *CI, bool doInline)
      : AbstractDriverRewriteVisitor(CI, doInline)
    {

    }

    virtual ~GPUDriverRewriteVisitor() {}
	};
}

#endif // GPUDRIVERREWRITEVISITOR_H