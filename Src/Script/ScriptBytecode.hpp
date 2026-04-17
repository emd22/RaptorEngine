#pragma once

#include <Core/Types.hpp>

/*
[BASE] [SPEC]
*/

enum eOpBase : uint8
{
    OpBase_Push = 1,
    OpBase_Pop,
    OpBase_Load,
    OpBase_Arith,
    OpBase_Save,
    OpBase_Jump,
    OpBase_Data,
    OpBase_Type,
    OpBase_Move,
};

enum eOpSpecPush : uint8
{
    OpSpecPush_Int32 = 1, // PUSH32  [imm]
    OpSpecPush_Reg32,     // PUSH32r [%r32]
};

enum eOpSpecPop : uint8
{
    OpSpecPop_Int32 = 1, // POP32 [%r32]
};

enum eOpSpecLoad : uint8
{
    OpSpecLoad_Int32 = 1, // LOAD32 [offset] [%r32]
    OpSpecLoad_AbsoluteInt32,
};

enum eOpSpecArith : uint8
{
    OpSpecArith_Add = 1 // ADD [%r32] [%r32]
};

enum eOpSpecSave : uint8
{
    OpSpecSave_Int32 = 1,
    OpSpecSave_Reg32,
    OpSpecSave_AbsoluteInt32,
    OpSpecSave_AbsoluteReg32
};

enum eOpSpecJump : uint8
{
    OpSpecJump_Relative = 1,
    OpSpecJump_Absolute,
    OpSpecJump_AbsoluteReg32,

    OpSpecJump_CallAbsolute,
    OpSpecJump_ReturnToCaller,

    OpSpecJump_CallExternal,
};

enum eOpSpecData : uint8
{
    OpSpecData_String = 1,
    OpSpecData_ParamsStart,
};

enum eOpSpecType : uint8
{
    OpSpecType_Int = 1,
    OpSpecType_String,
};

enum eOpSpecMove : uint8
{
    OpSpecMove_Int32 = 1,
};
