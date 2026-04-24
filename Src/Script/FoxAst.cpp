#include "FoxAst.hpp"

#include "ScriptVar.hpp"

namespace fx::script {

const FoxValue FoxValue::scNone = FoxValue(0);


eFoxType FoxAstFunctionCall::GetReturnType() const
{
    if (!pFunction || !pFunction->pDeclaration) {
        return eFoxType::NONETYPE;
    }

    return pFunction->pDeclaration->ReturnType;
}

void FoxAstPrinter::Print(FoxAstNode* node, int depth)
{
    if (node == nullptr) {
        return;
    }

    for (int i = 0; i < depth; i++) {
        putchar(' ');
        putchar(' ');
    }

    if (node->NodeType == FX_AST_BLOCK) {
        puts("[BLOCK]");

        FoxAstBlock* block = reinterpret_cast<FoxAstBlock*>(node);
        for (FoxAstNode* child : block->Statements) {
            Print(child, depth + 1);
        }
        return;
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        FoxAstFunctionDecl* functiondecl = reinterpret_cast<FoxAstFunctionDecl*>(node);
        printf("[PROCDECL] ");
        functiondecl->pNameToken->Print();

        for (FoxAstNode* param : functiondecl->pParams->Statements) {
            Print(param, depth + 1);
        }

        Print(functiondecl->pBlock, depth + 1);
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = reinterpret_cast<FoxAstVarDecl*>(node);

        printf("[VARDECL] ");
        vardecl->pNameToken->Print();

        Print(vardecl->pAssignment, depth + 1);
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = reinterpret_cast<FoxAstAssign*>(node);

        printf("[ASSIGN] ");

        assign->Var->pName->Print();
        Print(assign->Rhs, depth + 1);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* functioncall = reinterpret_cast<FoxAstFunctionCall*>(node);

        printf("[PROCCALL] ");
        if (functioncall->pFunction == nullptr) {
            printf("{defined externally}");
        }
        else {
            functioncall->pFunction->Name->Print(true);
        }

        printf(" (%zu params)\n", functioncall->Params.size());
    }
    else if (node->NodeType == FX_AST_LITERAL) {
        FoxAstLiteral* literal = reinterpret_cast<FoxAstLiteral*>(node);

        printf("[LITERAL] ");
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = reinterpret_cast<FoxAstBinop*>(node);

        printf("[BINOP] ");
        binop->OpToken->Print();

        Print(binop->pLeft, depth + 1);
        Print(binop->pRight, depth + 1);
    }
    else if (node->NodeType == FX_AST_COMMANDMODE) {
        FoxAstCommandMode* command_mode = reinterpret_cast<FoxAstCommandMode*>(node);
        printf("[COMMANDMODE]\n");

        Print(command_mode->Node, depth + 1);
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = reinterpret_cast<FoxAstReturn*>(node);

        puts("[RETURN]");

        if (return_node->pRhs) {
            Print(return_node->pRhs, depth + 1);
        }
    }
    else {
        puts("[UNKNOWN]");
    }
    // else if (node->NodeType == FX_AST_)
}

} // namespace fx::script
