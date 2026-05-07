#include "FoxAst.hpp"

#include "FoxVariable.hpp"

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

        FoxAstBlock* block = static_cast<FoxAstBlock*>(node);
        for (FoxAstNode* child : block->Statements) {
            Print(child, depth + 1);
        }
        return;
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        FoxAstFunctionDecl* functiondecl = static_cast<FoxAstFunctionDecl*>(node);
        printf("[PROCDECL] ");
        functiondecl->pNameToken->Print();

        for (FoxAstNode* param : functiondecl->pParams->Statements) {
            Print(param, depth + 1);
        }

        Print(functiondecl->pBlock, depth + 1);
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = static_cast<FoxAstVarDecl*>(node);

        printf("[VARDECL] ");
        vardecl->pNameToken->Print();

        Print(vardecl->pAssignment, depth + 1);
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = static_cast<FoxAstAssign*>(node);

        printf("[ASSIGN] ");

        assign->Var->pName->Print();
        Print(assign->Rhs, depth + 1);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* functioncall = static_cast<FoxAstFunctionCall*>(node);

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
        FoxAstLiteral* literal = static_cast<FoxAstLiteral*>(node);

        printf("[LITERAL] ");
        literal->Value.Print();
    }
    else if (node->NodeType == FX_AST_BINOP) {
        FoxAstBinop* binop = static_cast<FoxAstBinop*>(node);

        printf("[BINOP] ");
        binop->OpToken->Print();

        Print(binop->pLeft, depth + 1);
        Print(binop->pRight, depth + 1);
    }
    else if (node->NodeType == FX_AST_COMMANDMODE) {
        FoxAstCommandMode* command_mode = static_cast<FoxAstCommandMode*>(node);
        printf("[COMMANDMODE]\n");

        Print(command_mode->Node, depth + 1);
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = static_cast<FoxAstReturn*>(node);

        puts("[RETURN]");

        if (return_node->pRhs) {
            Print(return_node->pRhs, depth + 1);
        }
    }
    else if (node->NodeType == FX_AST_IF) {
        FoxAstIf* if_block = static_cast<FoxAstIf*>(node);

        puts("[IF]");

        Print(if_block->pCondition, depth + 1);
        Print(if_block->pBlock, depth + 1);

        if (if_block->pElseBlock) {
            Print(if_block->pElseBlock, depth + 1);
        }
    }
    else {
        puts("[UNKNOWN]");
    }
    // else if (node->NodeType == FX_AST_)
}

} // namespace fx::script
