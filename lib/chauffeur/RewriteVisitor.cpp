//
// Copyright (c) 2014 Pantazis Deligiannis (p.deligiannis@imperial.ac.uk)
// This file is distributed under the MIT License. See LICENSE for details.
//

#include "chauffeur/Chauffeur.h"
#include "chauffeur/RewriteVisitor.h"

namespace chauffeur
{
  using namespace std;

  bool RewriteVisitor::VisitFunctionDecl(FunctionDecl* FD)
  {
    string fdFileWithExt = Context->getSourceManager().getFilename(FD->getLocation());
    string fdFile = fdFileWithExt.substr(0, fdFileWithExt.find_last_of("."));

    InlineFunctions(FD, fdFile);
    InstrumentEntryPoints(FD, fdFile);
    InstrumentInitWithEntryPointCalls(FD, fdFile);

    return true;
  }

  void RewriteVisitor::InlineFunctions(FunctionDecl* FD, string fdFile)
  {
    if (DI->getInstance().ExistsEntryPointWithName(FD->getNameInfo().getName().getAsString()))
      return;

    if ((fdFile.size() > 0) && (fdFile.find(FileName) != string::npos))
    {
      if (FD->getStorageClass() == SC_Static)
        RW.ReplaceText(FD->getInnerLocStart(), 6, "static inline");
    }
  }

  void RewriteVisitor::InstrumentEntryPoints(FunctionDecl* FD, string fdFile)
  {
    if (!(DI->getInstance().ExistsEntryPointWithName(FD->getNameInfo().getName().getAsString())))
      return;

    if (FD->getStorageClass() == SC_Static)
      RW.RemoveText(FD->getInnerLocStart(), 7);

    if (DI->getInstance().GetInitFunction() == FD->getNameInfo().getName().getAsString())
      return;

    if (FD->getParamDecl(0)->getOriginalType().getAsString() != "struct device *" &&
        FD->getParamDecl(0)->getOriginalType().getAsString() != "struct pci_dev *")
      return;

    SourceRange sr = FD->getParamDecl(0)->getSourceRange();

    RW.InsertTextBefore(sr.getBegin(), "struct net_device *dev, ");

    Stmt *body = FD->getBody();
    list<DeclStmt*> stmtsToRewrite;

    for (auto i = body->child_begin(), e = body->child_end(); i != e; ++i)
    {
      if (!isa<DeclStmt>(*i))
        continue;

      DeclStmt *declStmt = cast<DeclStmt>(*i);
      if (!declStmt->isSingleDecl() && !isa<VarDecl>(declStmt->getSingleDecl()))
        continue;

      VarDecl *var = cast<VarDecl>(declStmt->getSingleDecl());
      if (!var->hasInit())
        continue;

      Expr *expr = var->getInit();
      if (!isa<ImplicitCastExpr>(expr))
        continue;

      ImplicitCastExpr *implicit = cast<ImplicitCastExpr>(expr);
      if (!isa<CallExpr>(implicit->getSubExpr()))
       continue;

      CallExpr *call = cast<CallExpr>(implicit->getSubExpr());
      DeclRefExpr *callee = cast<DeclRefExpr>(cast<ImplicitCastExpr>(call->getCallee())->getSubExpr());

      if (callee->getNameInfo().getName().getAsString() == "to_pci_dev" ||
          callee->getNameInfo().getName().getAsString() == "pci_get_drvdata")
      {
        stmtsToRewrite.push_back(declStmt);
      }
    }

    while (!stmtsToRewrite.empty())
    {
      DeclStmt *stmt = stmtsToRewrite.back();
      RW.RemoveText(stmt->getSourceRange());
      stmtsToRewrite.pop_back();
    }
  }

  void RewriteVisitor::InstrumentInitWithEntryPointCalls(FunctionDecl* FD, string fdFile)
  {
    if (FD->getNameInfo().getName().getAsString() != DI->getInstance().GetInitFunction())
      return;

    string net_device_str;
    Stmt *body = FD->getBody();

    for (auto i = body->child_begin(), e = body->child_end(); i != e; ++i)
    {
      if (!isa<DeclStmt>(*i))
        continue;

      DeclStmt *declStmt = cast<DeclStmt>(*i);
      if (!declStmt->isSingleDecl() && !isa<VarDecl>(declStmt->getSingleDecl()))
        continue;

      VarDecl *var = cast<VarDecl>(declStmt->getSingleDecl());
      if (!isa<ValueDecl>(var))
        continue;

      ValueDecl *value = cast<ValueDecl>(var);

      if (value->getType().getAsString(Context->getPrintingPolicy()) != "struct net_device *")
        continue;
      if (!isa<NamedDecl>(var))
        continue;

      NamedDecl *named = cast<NamedDecl>(var);
      net_device_str = named->getNameAsString();
      break;
    }

    if (net_device_str.empty())
      return;

    map<string, string> func_params;
    // list<ParmVarDecl*> func_params;
    for (auto i = FD->param_begin(), e = FD->param_end(); i != e; ++i)
    {
      ValueDecl *paramVal = cast<ValueDecl>(*i);
      NamedDecl *paramNam = cast<NamedDecl>(*i);

      func_params[paramVal->getType().getAsString(Context->getPrintingPolicy())] = paramNam->getNameAsString();
    }

    for (auto i = body->child_begin(), e = body->child_end(); i != e; ++i)
    {
      if (!isa<LabelStmt>(*i))
        continue;

      LabelStmt *labelStmt = cast<LabelStmt>(*i);

      RW.InsertTextBefore(labelStmt->getIdentLoc(), "\n");

      auto entry_points = DI->getInstance().GetEntryPoints();
      for(auto i = entry_points.begin(); i != entry_points.end(); i++)
      {
        string entry_point_call = "	" + i->first + "(";
        if (find(i->second.begin(), i->second.end(), "struct net_device *") == i->second.end())
          entry_point_call += net_device_str + ", ";

        for(auto j = i->second.begin(); j != i->second.end(); j++)
        {
          if (*j == "struct net_device *")
            entry_point_call += net_device_str + ", ";
          else if (*j == "struct pci_dev *")
            entry_point_call += func_params["struct pci_dev *"] + ", ";
          else if (*j == "struct device *")
            entry_point_call += "&" + func_params["struct pci_dev *"] + "->dev, ";
          else if (*j == "void *")
            entry_point_call += "NULL, ";
          else if (*j == "u64 *")
            entry_point_call += "NULL, ";
          else if (*j == "u8 *")
            entry_point_call += "NULL, ";
          else if (*j == "struct sk_buff *")
            entry_point_call += "whoop_skb, ";
          else if (*j == "struct ethtool_wolinfo *")
            entry_point_call += "whoop_wolinfo, ";
          else if (*j == "struct ethtool_cmd *")
            entry_point_call += "whoop_ecmd, ";
          else if (*j == "struct ifreq *")
            entry_point_call += "whoop_ifreq, ";
          else if (*j == "struct rtnl_link_stats64 *")
            entry_point_call += "whoop_rtnlsts64, ";
          else if (*j == "struct ethtool_regs *")
            entry_point_call += "whoop_ethtoolregs, ";
          else if (*j == "struct ethtool_stats *")
            entry_point_call += "whoop_ethtoolsts, ";
          else if (*j == "struct ethtool_drvinfo *")
            entry_point_call += "whoop_ethtooldrvinfo, ";
          else if (*j == "netdev_features_t")
            entry_point_call += "whoop_netdevfeat, ";
          else if (*j == "int")
            entry_point_call += "0, ";
          else if (*j == "u32")
            entry_point_call += "0, ";
          else
            entry_point_call += *j + ", ";
        }

        entry_point_call.resize(entry_point_call.size() - 2);
        RW.InsertTextBefore(labelStmt->getIdentLoc(), entry_point_call + ");\n");
      }

      RW.InsertTextBefore(labelStmt->getIdentLoc(), "\n");

      list<string> instrDone;
      for(auto i = entry_points.begin(); i != entry_points.end(); i++)
      {
        for(auto j = i->second.begin(); j != i->second.end(); j++)
        {
          if (find(instrDone.begin(), instrDone.end(), *j) != instrDone.end())
            continue;

          if (*j == "struct sk_buff *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_skb;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct ethtool_wolinfo *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_wolinfo;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct ethtool_cmd *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_ecmd;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct ifreq *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_ifreq;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct rtnl_link_stats64 *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_rtnlsts64;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct ethtool_regs *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_ethtoolregs;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct ethtool_stats *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_ethtoolsts;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "struct ethtool_drvinfo *")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_ethtooldrvinfo;\n");
            instrDone.push_back(*j);
          }
          else if (*j == "netdev_features_t")
          {
            RW.InsertTextBefore(labelStmt->getIdentLoc(), "	" + *j + " whoop_netdevfeat = NETIF_F_RXCSUM;\n");
            instrDone.push_back(*j);
          }
        }
      }

      break;
    }
  }

  void RewriteVisitor::Finalise()
  {
    string file = FileName;
    file.append(".re.c");

    string error_msg;
    llvm::raw_fd_ostream *fos = new llvm::raw_fd_ostream(file.c_str(), error_msg, llvm::sys::fs::F_None);
    if (!error_msg.empty())
    {
      if (llvm::errs().has_colors()) llvm::errs().changeColor(llvm::raw_ostream::RED);
      llvm::errs() << "error: " << error_msg << "\n";
      if (llvm::errs().has_colors()) llvm::errs().resetColor();
      exit(1);
    }

    fos->SetUnbuffered();
    fos->SetUseAtomicWrites(true);

    raw_ostream *ros = fos;

    RW.getEditBuffer(RW.getSourceMgr().getMainFileID()).write(*ros);
  }
}