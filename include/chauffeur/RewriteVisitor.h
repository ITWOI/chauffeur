//
// Copyright (c) 2014 Pantazis Deligiannis (p.deligiannis@imperial.ac.uk)
// This file is distributed under the MIT License. See LICENSE for details.
//

#ifndef REWRITEVISITOR_H
#define REWRITEVISITOR_H

#include "chauffeur/DriverInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Core/Rewriter.h"


namespace chauffeur
{
  using namespace clang;

	class RewriteVisitor : public RecursiveASTVisitor<RewriteVisitor>
	{
	private:
		ASTContext *Context;
		Rewriter RW;
		DriverInfo *DI;

    void InlineFunctions(FunctionDecl* FD, string fdFile);
    void InstrumentEntryPoints(FunctionDecl* FD, string fdFile);
    void InstrumentInitWithEntryPointCalls(FunctionDecl* FD, string fdFile);

	public:
	  explicit RewriteVisitor(CompilerInstance *CI)
      : Context(&(CI->getASTContext()))
    {
      RW.setSourceMgr(Context->getSourceManager(), Context->getLangOpts());
    }

		virtual bool VisitFunctionDecl(FunctionDecl* FD);

		void Finalise();
	};
}

#endif // REWRITEVISITOR_H