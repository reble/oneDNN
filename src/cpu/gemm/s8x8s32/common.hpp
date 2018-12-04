/*******************************************************************************
* Copyright 2018 Intel Corporation
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

#define GEMM_CODE_SIZE          (4096L * 32)

#define AVX512_UNROLL_M                   48
#define AVX512_UNROLL_N                    8
#define AVX512_UNROLL_K                    1
#define AVX512_BM                       9984
#define AVX512_BN                        384
#define AVX512_BK                        768
#define AVX512_BK_VNNI                  1536
#define AVX512_BK_TRADITIONAL            384
#define AVX512_BLOCKING_SMALL_K           48
#define AVX512_BN_SMALL_K                 24

#define COPY_AN_AVX512          "kernel/igemm_copy_an_avx512.inc"
#define COPY_AT_AVX512          "kernel/igemm_copy_at_avx512.inc"
#define COPY_BN_AVX512          "kernel/igemm_copy_bn_avx512.inc"
#define COPY_BT_AVX512          "kernel/igemm_copy_bt_avx512.inc"

#define COPY_SUM_AN_AVX512      "kernel/igemm_copy_sum_an_avx512.inc"
#define COPY_SUM_AT_AVX512      "kernel/igemm_copy_sum_at_avx512.inc"
#define COPY_SUM_BN_AVX512      "kernel/igemm_copy_sum_bn_avx512.inc"
#define COPY_SUM_BT_AVX512      "kernel/igemm_copy_sum_bt_avx512.inc"

#define KERNEL_AVX512           "kernel/igemm_kernel_avx512.inc"
#define KERNEL_AVX512_VNNI      "kernel/igemm_kernel_avx512_vnni.inc"

#define KERNEL_B0_AVX512        "kernel/igemm_kernel_b0_avx512.inc"
#define KERNEL_B0_AVX512_VNNI   "kernel/igemm_kernel_b0_avx512_vnni.inc"

#define KERNEL_B_AVX512         "kernel/igemm_kernel_b_avx512.inc"
#define KERNEL_B_AVX512_VNNI    "kernel/igemm_kernel_b_avx512_vnni.inc"

#define KERNEL_B0_B_AVX512      "kernel/igemm_kernel_b0_b_avx512.inc"
#define KERNEL_B0_B_AVX512_VNNI "kernel/igemm_kernel_b0_b_avx512_vnni.inc"

#define KERNEL_R_AVX512         "kernel/igemm_kernel_r_avx512.inc"
#define KERNEL_R_AVX512_VNNI    "kernel/igemm_kernel_r_avx512_vnni.inc"

#define KERNEL_B0_R_AVX512      "kernel/igemm_kernel_b0_r_avx512.inc"
#define KERNEL_B0_R_AVX512_VNNI "kernel/igemm_kernel_b0_r_avx512_vnni.inc"

#define KERNEL_C_AVX512         "kernel/igemm_kernel_c_avx512.inc"
#define KERNEL_C_AVX512_VNNI    "kernel/igemm_kernel_c_avx512_vnni.inc"

#define KERNEL_B0_C_AVX512      "kernel/igemm_kernel_b0_c_avx512.inc"
#define KERNEL_B0_C_AVX512_VNNI "kernel/igemm_kernel_b0_c_avx512_vnni.inc"

#include "jit_generator.hpp"

namespace mkldnn {
namespace impl {
namespace cpu {

class jit_avx512_core_u8_copy_an_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_an_kern);

    public:
        jit_avx512_core_u8_copy_an_kern();
};

class jit_avx512_core_u8_copy_at_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_at_kern);

    public:
        jit_avx512_core_u8_copy_at_kern();
};

class jit_avx512_core_u8_copy_bn_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_bn_kern);

    public:
        jit_avx512_core_u8_copy_bn_kern();
};

class jit_avx512_core_u8_copy_bt_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_bt_kern);

    public:
        jit_avx512_core_u8_copy_bt_kern();
};

class jit_avx512_core_u8_copy_sum_an_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_sum_an_kern);

    public:
        jit_avx512_core_u8_copy_sum_an_kern();
};

class jit_avx512_core_u8_copy_sum_at_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_sum_at_kern);

    public:
        jit_avx512_core_u8_copy_sum_at_kern();
};

class jit_avx512_core_u8_copy_sum_bn_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_sum_bn_kern);

    public:
        jit_avx512_core_u8_copy_sum_bn_kern();
};

class jit_avx512_core_u8_copy_sum_bt_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_u8_copy_sum_bt_kern);

    public:
        jit_avx512_core_u8_copy_sum_bt_kern();
};

class jit_avx512_core_kernel_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_b_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_b_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_b_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_r_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_r_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_r_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_c_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_c_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_c_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_b0_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_b0_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_b0_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_b0_b_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_b0_b_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_b0_b_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_b0_r_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_b0_r_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_b0_r_gemm_s8u8s32_kern();
};

class jit_avx512_core_kernel_b0_c_gemm_s8u8s32_kern : public jit_generator {
    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_kernel_b0_c_gemm_s8u8s32_kern);

    public:
        jit_avx512_core_kernel_b0_c_gemm_s8u8s32_kern();
};

}
}
}
#endif
