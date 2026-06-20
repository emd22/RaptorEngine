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

bool FoxAstFunctionCall::HasReturnType() const
{
    if (!pFunction || !pFunction->pDeclaration) {
        return false;
    }

    return pFunction->pDeclaration->ReturnType != eFoxType::NONETYPE;
}

#define PRINT_NESTED(node_) Print(static_cast<FoxAstNode*>(node_), depth + 1);

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
            PRINT_NESTED(child);
        }
        return;
    }
    else if (node->NodeType == FX_AST_PROCDECL) {
        FoxAstFunctionDecl* functiondecl = static_cast<FoxAstFunctionDecl*>(node);

        printf("[PROCDECL] ");
        functiondecl->pNameToken->PrintBasic();

        for (FoxAstNode* param : functiondecl->pParams->Statements) {
            PRINT_NESTED(param);
        }

        PRINT_NESTED(functiondecl->pBlock);
    }
    else if (node->NodeType == FX_AST_MODULECALL) {
        FoxAstModuleCall* module_call = static_cast<FoxAstModuleCall*>(node);
        printf("[MODULECALL] ");

        if (module_call->pModuleLoad) {
            module_call->pModuleLoad->pAlias->Print();
        }

        PRINT_NESTED(module_call->pFunctionCall);
    }
    else if (node->NodeType == FX_AST_VARDECL) {
        FoxAstVarDecl* vardecl = static_cast<FoxAstVarDecl*>(node);

        printf("[VARDECL] ");
        vardecl->pNameToken->PrintBasic();

        PRINT_NESTED(vardecl->pAssignment);
    }
    else if (node->NodeType == FX_AST_ASSIGN) {
        FoxAstAssign* assign = static_cast<FoxAstAssign*>(node);

        printf("[ASSIGN] ");

        assign->pLhs->pName->PrintBasic();
        PRINT_NESTED(assign->pRhs);
    }
    else if (node->NodeType == FX_AST_PROCCALL) {
        FoxAstFunctionCall* functioncall = static_cast<FoxAstFunctionCall*>(node);

        printf("[PROCCALL] ");
        if (functioncall->pFunction == nullptr) {
            printf("{defined externally}");
        }
        else {
            functioncall->pFunction->Name->PrintBasic(true);
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
        binop->OpToken->PrintBasic();

        PRINT_NESTED(binop->pLeft);
        PRINT_NESTED(binop->pRight);
    }
    else if (node->NodeType == FX_AST_RETURN) {
        FoxAstReturn* return_node = static_cast<FoxAstReturn*>(node);

        puts("[RETURN]");

        if (return_node->pRhs) {
            PRINT_NESTED(return_node->pRhs);
        }
    }
    else if (node->NodeType == FX_AST_IF) {
        FoxAstIf* if_block = static_cast<FoxAstIf*>(node);

        puts("[IF]");

        PRINT_NESTED(if_block->pCondition);
        PRINT_NESTED(if_block->pBlock);

        if (if_block->pElseBlock) {
            PRINT_NESTED(if_block->pElseBlock);
        }
    }

    else if (node->NodeType == FX_AST_MODULELOAD) {
        FoxAstModuleLoad* module_load = static_cast<FoxAstModuleLoad*>(node);

        printf("[MODLOAD] ");

        if (module_load->pAlias) {
            module_load->pAlias->PrintBasic(true);
        }
        else {
            printf("[none]");
        }
        printf(" at ");

        if (module_load->pModulePath) {
            module_load->pModulePath->PrintBasic();
        }
        else {
            printf("[none]\n");
        }
    }
    else {
        puts("[UNKNOWN]");
    }
    // else if (node->NodeType == FX_AST_)
}

} // namespace fx::script
