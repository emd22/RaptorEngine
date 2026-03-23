#pragma once

/*
 * Heavily modified version of the TLSF memory allocator v3.1 written by Matthew Conte.
 */

/*
** Two Level Segregated Fit memory allocator, version 3.1.
** Written by Matthew Conte
**	http://tlsf.baisoku.org
**
** Based on the original documentation by Miguel Masmano:
**	http://www.gii.upv.es/tlsf/main/docs
**
** This implementation was written to the specification
** of the document, therefore no GPL restrictions apply.
**
** Copyright (c) 2006-2016, Matthew Conte
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the copyright holder nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL MATTHEW CONTE BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stddef.h>

#include <Core/FxTypes.hpp>

/* tlsf_t: a TLSF structure. Can contain 1 to N pools. */
/* pool_t: a block of memory that TLSF can manage. */
typedef void* tlsf_t;
typedef void* pool_t;

class ControlBlock;

class FxMemPool2
{
public:
    using WalkerFunc = void (*)(void* ptr, size_t size, int32 used, void* user);

public:
    using Status = int32;
    static constexpr Status scPoolOk = 0;

    void Create(uint64 size);

    tlsf_t CreateFromPtr(void* allocated_buffer);
    tlsf_t CreateWithPool(void* mem, size_t bytes);

    pool_t AddPool(void* mem, size_t bytes);
    void RemovePool(pool_t pool);
    pool_t GetPool();

    /* malloc/memalign/realloc/free replacements. */
    void* Alloc(size_t bytes);
    void* AlignedAlloc(size_t align, size_t bytes);
    void* Realloc(void* ptr, size_t size);
    void Free(void* ptr);

    /* Debugging. */
    void WalkPool(pool_t pool, WalkerFunc walker, void* user);
    Status CheckIntegrity();
    Status CheckPool(pool_t pool);

private:
    ControlBlock* GetControlBlock();

public:
    void* pMemory;
};
