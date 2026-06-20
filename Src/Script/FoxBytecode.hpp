#pragma once

#include <Core/Types.hpp>

namespace fx::script {

enum FoxBytecodeBase : uint8
{
    BcBase_Push = 1,
    BcBase_Pop,
    BcBase_Load,
    BcBase_Arith,
    BcBase_Save,
    BcBase_Jump,
    BcBase_Data,
    BcBase_Type,
    BcBase_Move,
    BcBase_Marker,
    BcBase_Compare,

    BcBase_Variable,
};

enum BcSpecCompare : uint8
{
    BcSpecCompare_Default,
    BcSpecCompare_NotZero,
};

enum BcSpecPush : uint8
{
    BcSpecPush_Int32 = 1,  // PUSH  [int32]
    BcSpecPush_Float32,    // PUSHF [float32]
    BcSpecPush_String,     // PUSHS [str]
    BcSpecPush_Var,        // VPUSH [%var]
    BcSpecPush_ReturnAddr, // PUSHRA
    BcSpecPush_VarPtr,     // VPUSHPTR [%var]
    BcSpecPush_ReadPtr,    // VREADPTR [&ptr]

    BcSpecPush_StackAlloc,
};

enum BcSpecPop : uint8
{
    BcSpecPop_Variable_Int32,   // VPOP
    BcSpecPop_Variable_Float32, // VPOPF
    BcSpecPop_Discard,          // POP
    BcSpecPop_ReturnAddr,       // POPRA
};

enum BcSpecArith : uint8
{
    BcSpecArith_Add_Int32 = 1, // ADD
    BcSpecArith_Add_Float32,   // ADDF
    BcSpecArith_Multiply_Int32,
    BcSpecArith_Multiply_Float32,
};

enum BcSpecSave : uint8
{
    BcSpecSave_Int32 = 1,
    BcSpecSave_Reg32,
    BcSpecSave_AbsoluteInt32,
};

enum BcSpecJump : uint8
{
    BcSpecJump_Relative = 1,

    BcSpecJump_Equal,
    BcSpecJump_NotEqual,
    BcSpecJump_Greater,
    BcSpecJump_Less,
    BcSpecJump_LessEqual,
    BcSpecJump_GreaterEqual,

    BcSpecJump_Absolute,
    BcSpecJump_AbsoluteReg32,

    BcSpecJump_CallAbsolute,
    BcSpecJump_CallModuleFunction,

    BcSpecJump_ReturnToCaller,
    BcSpecJump_ReturnToCaller_Int32,
    BcSpecJump_ReturnToCaller_Float32,
    BcSpecJump_ReturnToCaller_String,

    BcSpecJump_Pause,

    BcSpecJump_CallExternal,
};

enum BcSpecData : uint8
{
    BcSpecData_String = 1,
};

enum BcSpecType : uint8
{
    BcSpecType_Int = 1,
    BcSpecType_Float,
    BcSpecType_String,
};

enum BcSpecMarker : uint8
{
    // Function frame ops
    BcSpecMarker_FrameBegin = 1,
    BcSpecMarker_FrameEnd,

    BcSpecMarker_ParamsBegin,
    BcSpecMarker_FunctionName,

    BcSpecMarker_ExtFn,

    BcSpecMarker_Proc, // PROC [name hash]
    BcSpecMarker_ProcEnd,

    BcSpecMarker_StringsBegin,
    BcSpecMarker_StringsEnd,


};

enum BcSpecVariable : uint8
{
    BcSpecVariable_Set_Int32 = 1,
    BcSpecVariable_Set_Float32,
    BcSpecVariable_Set_String,
    BcSpecVariable_Set_Var,

    BcSpecVariable_Get_Int32,
    BcSpecVariable_Get_Float32,

    BcSpecVariable_Define_Int32,
    BcSpecVariable_Define_Float32,
    BcSpecVariable_Define_String,

    BcSpecVariable_DefineGlobal_Int32,
    BcSpecVariable_DefineGlobal_Float32,
    BcSpecVariable_DefineGlobal_String,

    BcSpecVariable_DefineFetchParam_Int32,
    BcSpecVariable_DefineFetchParam_Float32,
    BcSpecVariable_DefineFetchParam_String,

    BcSpecVariable_Cast_Int32,
    BcSpecVariable_Cast_Float32,

    BcSpecVariable_SetPtr_Int32,
    BcSpecVariable_SetPtr_Float32,
    BcSpecVariable_SetPtr_String,
    BcSpecVariable_SetPtr_Var,
};

} // namespace fx::script
