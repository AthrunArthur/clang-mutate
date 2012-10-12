// Copyright (C) 2012 Eric Schulte
#include "ASTMutate.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Rewrite/Rewriters.h"
#include "clang/Rewrite/Rewriter.h"
#include "llvm/Module.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Timer.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#define VISIT(func) \
  bool func { VisitRange(element->getSourceRange()); return true; }

using namespace clang;

namespace {
  class ASTMutator : public ASTConsumer,
                     public RecursiveASTVisitor<ASTMutator> {
    typedef RecursiveASTVisitor<ASTMutator> base;

  public:
    ASTMutator(raw_ostream *Out = NULL, bool Dump = false,
               ACTION Action = NUMBER,
               int Stmt1 = -1, int Stmt2 = -1)
      : Out(Out ? *Out : llvm::outs()), Dump(Dump), Action(Action),
        Stmt1(Stmt1), Stmt2(Stmt2) {}

    virtual void HandleTranslationUnit(ASTContext &Context) {
      TranslationUnitDecl *D = Context.getTranslationUnitDecl();
      // Setup
      Counter=0;
      mainFileID=Context.getSourceManager().getMainFileID();
      Rewrite.setSourceMgr(Context.getSourceManager(),
                           Context.getLangOpts());
      // Run Recursive AST Visitor
      TraverseDecl(D);
      // Finish Up
      switch(Action){
      case INSERT:
        Rewritten2 = Rewrite.getRewrittenText(Range2);
        Rewrite.InsertText(Range1.getBegin(), (Rewritten2+" "), true);
        break;
      case SWAP:
        Rewritten1 = Rewrite.getRewrittenText(Range1);
        Rewritten2 = Rewrite.getRewrittenText(Range2);
        Rewrite.ReplaceText(Range1, Rewritten2);
        Rewrite.ReplaceText(Range2, Rewritten1);
        break;
      default: break;
      }
      OutputRewritten(Context);
    };
    
    Rewriter Rewrite;

    bool SelectRange(SourceRange r)
    {
      FullSourceLoc loc = FullSourceLoc(r.getEnd(), Rewrite.getSourceMgr());
      return (loc.getFileID() == mainFileID);
    }

    void NumberRange(SourceRange r)
    {
      char label[24];
      unsigned EndOff;
      SourceLocation END = r.getEnd();

      sprintf(label, "/* %d[ */", Counter);
      Rewrite.InsertText(r.getBegin(), label, false);
      sprintf(label, "/* ]%d */", Counter);
  
      // Adjust the end offset to the end of the last token, instead
      // of being the start of the last token.
      EndOff = Lexer::MeasureTokenLength(END,
                                         Rewrite.getSourceMgr(),
                                         Rewrite.getLangOpts());

      Rewrite.InsertText(END.getLocWithOffset(EndOff), label, true);
    }

    void DeleteRange(SourceRange r)
    {
      char label[24];
      if(Counter == Stmt1) {
        sprintf(label, "/* deleted:%d */", Counter);
        Rewrite.ReplaceText(r, label);
      }
    }

    void SaveRange(SourceRange r)
    {
      if (Counter == Stmt1) Range1 = r;
      if (Counter == Stmt2) Range2 = r;
    }

    void VisitRange(SourceRange r){
      if (SelectRange(r)) {
        switch(Action) {
        case NUMBER: NumberRange(r); break;
        case DELETE: DeleteRange(r); break;
        case INSERT:
        case SWAP:     SaveRange(r); break;
        case IDS: break;
        }
        Counter++;
      }
    }

    //// from AST/EvaluatedExprVisitor.h
    // VISIT(VisitDeclRefExpr(DeclRefExpr *element));
    // VISIT(VisitOffsetOfExpr(OffsetOfExpr *element));
    // VISIT(VisitUnaryExprOrTypeTraitExpr(UnaryExprOrTypeTraitExpr *element));
    // VISIT(VisitExpressionTraitExpr(ExpressionTraitExpr *element));
    // VISIT(VisitBlockExpr(BlockExpr *element));
    // VISIT(VisitCXXUuidofExpr(CXXUuidofExpr *element));
    // VISIT(VisitCXXNoexceptExpr(CXXNoexceptExpr *element));
    // VISIT(VisitMemberExpr(MemberExpr *element));
    // VISIT(VisitChooseExpr(ChooseExpr *element));
    // VISIT(VisitDesignatedInitExpr(DesignatedInitExpr *element));
    // VISIT(VisitCXXTypeidExpr(CXXTypeidExpr *element));
    VISIT(VisitStmt(Stmt *element));

    //// from Analysis/Visitors/CFGRecStmtDeclVisitor.h
    // VISIT(VisitDeclRefExpr(DeclRefExpr *element)); // <- duplicate above
    // VISIT(VisitDeclStmt(DeclStmt *element));
    // VISIT(VisitDecl(Decl *element)); // <- throws assertion error
    // VISIT(VisitCXXRecordDecl(CXXRecordDecl *element));
    // VISIT(VisitChildren(Stmt *element));
    // VISIT(VisitStmt(Stmt *element)); // <- duplicate above
    // VISIT(VisitCompoundStmt(CompoundStmt *element));
    // VISIT(VisitConditionVariableInit(Stmt *element));

    void OutputRewritten(ASTContext &Context) {
      // output rewritten source code or ID count
      if(Action == IDS){
        Out << Counter << "\n";
      } else {
        const RewriteBuffer *RewriteBuf = 
          Rewrite.getRewriteBufferFor(Context.getSourceManager().getMainFileID());
        Out << std::string(RewriteBuf->begin(), RewriteBuf->end());
      }
    }
    
  private:
    raw_ostream &Out;
    bool Dump;
    ACTION Action;
    int Stmt1, Stmt2;
    unsigned int Counter;
    FileID mainFileID;
    SourceRange Range1, Range2;
    std::string Rewritten1, Rewritten2;
  };
}

ASTConsumer *clang::CreateASTNumberer(){
  return new ASTMutator(0, /*Dump=*/ true, NUMBER, -1, -1);
}

ASTConsumer *clang::CreateASTIDS(){
  return new ASTMutator(0, /*Dump=*/ true, IDS, -1, -1);
}

ASTConsumer *clang::CreateASTDeleter(int Stmt){
  return new ASTMutator(0, /*Dump=*/ true, DELETE, Stmt, -1);
}

ASTConsumer *clang::CreateASTInserter(int Stmt1, int Stmt2){
  return new ASTMutator(0, /*Dump=*/ true, INSERT, Stmt1, Stmt2);
}

ASTConsumer *clang::CreateASTSwapper(int Stmt1, int Stmt2){
  return new ASTMutator(0, /*Dump=*/ true, SWAP, Stmt1, Stmt2);
}
