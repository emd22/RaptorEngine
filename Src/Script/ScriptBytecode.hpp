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

    BcBase_Variable,
};

enum BcSpecPush : uint8
{
    BcSpecPush_Int32 = 1, // PUSH32  [imm]
    BcSpecPush_Float32,   // PUSH32r [%r32]
    BcSpecPush_Var,       // VPUSH [%var]

    BcSpecPush_StackAlloc,
};

enum BrSpecPop : uint8
{
    BcSpecPop_Int32 = 1, // PIr32 [%r32]
    BcSpecPop_Variable_Int32,
    BcSpecPop_Variable_Float32,
};

enum BrSpecLoad : uint8
{
    BcSpecLoad_Int32 = 1, // LOAD32 [offset] [%r32]
    BcSpecLoad_AbsoluteInt32,
};

enum BcSpecArith : uint8
{
    BcSpecArith_Add = 1,     // ADD
    BcSpecArith_Add_Float32, // ADDF
};

enum BcSpecSave : uint8
{
    BcSpecSave_Int32 = 1,
    BcSpecSave_Reg32,
    BcSpecSave_AbsoluteInt32,
    BcSpecSave_AbsoluteReg32
};

enum BcSpecJump : uint8
{
    BcSpecJump_Relative = 1,
    BcSpecJump_Absolute,
    BcSpecJump_AbsoluteReg32,

    BcSpecJump_CallAbsolute,

    BcSpecJump_ReturnToCaller,
    BcSpecJump_ReturnToCaller_Value,

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

enum BcSpecMove : uint8
{
    BcSpecMove_Int32 = 1,
    BcSpecMove_Reg32,
};

enum BcSpecMarker : uint8
{
    // Function frame ops
    BcSpecMarker_FrameBegin = 1,
    BcSpecMarker_FrameEnd,

    BcSpecMarker_ParamsBegin,
    BcSpecMarker_FunctionName,

    BcSpecMarker_ExtFn,
    BcSpecMarker_Proc,
    BcSpecMarker_ExternalProc,
    BcSpecMarker_ProcEnd,

};

enum BcSpecVariable : uint8
{
    BcSpecVariable_Set_Int32 = 1,
    BcSpecVariable_Set_Float32,

    BcSpecVariable_Set_Var,

    BcSpecVariable_Get_Int32,
    BcSpecVariable_Get_Float32,

    BcSpecVariable_Define,
    BcSpecVariable_Define_Float32,

    BcSpecVariable_Cast_Int32,
    BcSpecVariable_Cast_Float32,
};

} // namespace fx::script
