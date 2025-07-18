/*******************************************************************************
* Copyright 2023-2025 Intel Corporation
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

#include "gpu/intel/jit/conv/problem.hpp"
#include "common/convolution_pd.hpp"
#include "gpu/intel/jit/ir/block_2d_utils.hpp"
#include "gpu/intel/jit/ir/fma.hpp"
#include "gpu/intel/jit/ir/hw.hpp"
#include "gpu/intel/jit/ir/tensor_config.hpp"

namespace dnnl {
namespace impl {
namespace gpu {
namespace intel {
namespace jit {

const std::vector<pvar_t> &conv_dims() {
    static std::vector<pvar_t> _conv_dims = []() {
        std::vector<pvar_t> ret;
        for (auto &d : conv_index_dims(prop_kind::forward)) {
            ret.push_back(d);
        }
        ret.push_back(pvars::id);
        ret.push_back(pvars::ih);
        ret.push_back(pvars::iw);
        for (auto &d : conv_stride_dims())
            ret.push_back(d);
        for (auto &d : conv_dilation_dims())
            ret.push_back(d);
        for (auto &d : conv_padding_dims())
            ret.push_back(d);
        return ret;
    }();
    return _conv_dims;
}

pvar_t prb_stride(const pvar_t &dim, tensor_kind_t tensor_kind) {

    auto dims = conv_layout_dims(tensor_kind, true);
    for (auto &d : dims) {
        if (d == dim) {
            auto str = to_string(tensor_kind) + "_";
            return pvar_t(str + dim.str() + "_stride");
        }
    }
    return pvar_t();
}

const std::vector<pvar_t> &conv_index_dims(prop_kind_t prop) {
    auto get_dims = [&](prop_kind_t prop) {
        std::vector<pvar_t> ret;
        ret.push_back(pvars::mb);
        ret.push_back(pvars::g);
        ret.push_back(pvars::oc);
        ret.push_back(pvars::ic);
        ret.push_back(pvars::kd);
        ret.push_back(pvars::kh);
        ret.push_back(pvars::kw);
        if (prop != prop_kind::backward_data) {
            ret.push_back(pvars::od);
            ret.push_back(pvars::oh);
            ret.push_back(pvars::ow);
        } else {
            ret.push_back(pvars::id);
            ret.push_back(pvars::ih);
            ret.push_back(pvars::iw);
        }
        return ret;
    };
    static std::vector<pvar_t> fwd_dims = get_dims(prop_kind::forward);
    static std::vector<pvar_t> bwd_d_dims = get_dims(prop_kind::backward_data);
    static std::vector<pvar_t> bwd_w_dims
            = get_dims(prop_kind::backward_weights);
    switch (prop) {
        case prop_kind::forward: return fwd_dims;
        case prop_kind::backward_data: return bwd_d_dims;
        case prop_kind::backward_weights: return bwd_w_dims;
        default: gpu_error_not_expected(); return fwd_dims;
    }
}

bool is_conv_index(const pvar_t &dim) {
    for (auto prop : {prop_kind::forward, prop_kind::backward_data,
                 prop_kind::backward_weights})
        if (is_conv_index(dim, prop)) return true;
    return false;
}

bool is_conv_index(const pvar_t &dim, prop_kind_t prop) {
    for (auto &d : conv_index_dims(prop))
        if (d == dim) return true;
    return false;
}

const std::vector<pvar_t> &conv_layout_dims(
        tensor_kind_t tensor_kind, bool src_dst_with_group) {
    static const std::vector<pvar_t> src_dims(
            {pvars::mb, pvars::ic, pvars::id, pvars::ih, pvars::iw});
    static const std::vector<pvar_t> src_g_dims(
            {pvars::mb, pvars::g, pvars::ic, pvars::id, pvars::ih, pvars::iw});
    static const std::vector<pvar_t> wei_dims(
            {pvars::g, pvars::oc, pvars::ic, pvars::kd, pvars::kh, pvars::kw});
    static const std::vector<pvar_t> dst_dims(
            {pvars::mb, pvars::oc, pvars::od, pvars::oh, pvars::ow});
    static const std::vector<pvar_t> dst_g_dims(
            {pvars::mb, pvars::g, pvars::oc, pvars::od, pvars::oh, pvars::ow});
    static const std::vector<pvar_t> bia_g_dims({pvars::g, pvars::oc});
    static const std::vector<pvar_t> bia_dims({pvars::oc});
    switch (tensor_kind) {
        case tensor_kind_t::src:
            return src_dst_with_group ? src_g_dims : src_dims;
        case tensor_kind_t::wei: return wei_dims;
        case tensor_kind_t::dst:
            return src_dst_with_group ? dst_g_dims : dst_dims;
        case tensor_kind_t::bias:
            return src_dst_with_group ? bia_g_dims : bia_dims;
        default: gpu_error_not_expected();
    }
    return src_dims;
}

tensor_kind_t to_abc(prop_kind_t prop, tensor_kind_t tensor) {
    bool is_bwd_d = (prop == prop_kind::backward_data);
    bool is_bwd_w = (prop == prop_kind::backward_weights);
    tensor_kind_t kinds[3]
            = {tensor_kind_t::a, tensor_kind_t::b, tensor_kind_t::c};
    if (is_bwd_d) std::swap(kinds[0], kinds[2]);
    if (is_bwd_w) std::swap(kinds[1], kinds[2]);
    switch (tensor) {
        case tensor_kind_t::src: return kinds[0];
        case tensor_kind_t::wei: return kinds[1];
        case tensor_kind_t::dst: return kinds[2];
        case tensor_kind_t::a:
        case tensor_kind_t::b:
        case tensor_kind_t::c: return tensor;
        default: gpu_error_not_expected();
    }
    return kinds[0];
}

tensor_kind_t from_abc(prop_kind_t prop, tensor_kind_t abc) {
    for (auto t :
            {tensor_kind_t::src, tensor_kind_t::wei, tensor_kind_t::dst}) {
        if (to_abc(prop, t) == abc) return t;
    }
    return tensor_kind_t::undef;
}

const std::vector<pvar_t> &conv_stride_dims() {
    static std::vector<pvar_t> _stride_dims = [&]() {
        std::vector<pvar_t> ret;
        ret.push_back(pvars::sd);
        ret.push_back(pvars::sh);
        ret.push_back(pvars::sw);
        return ret;
    }();
    return _stride_dims;
}

const std::vector<pvar_t> &conv_dilation_dims() {
    static std::vector<pvar_t> _dilation_dims = [&]() {
        std::vector<pvar_t> ret;
        ret.push_back(pvars::dd);
        ret.push_back(pvars::dh);
        ret.push_back(pvars::dw);
        return ret;
    }();
    return _dilation_dims;
}

const std::vector<pvar_t> &conv_padding_dims() {
    static std::vector<pvar_t> _padding_dims = [&]() {
        std::vector<pvar_t> ret;
        ret.push_back(pvars::pd);
        ret.push_back(pvars::ph);
        ret.push_back(pvars::pw);
        return ret;
    }();
    return _padding_dims;
}

bool can_reduce_to_1d(const memory_desc_t &md, const post_ops_t &post_ops) {
    int ndims = md.ndims;
    int sp_ndims = ndims - 2;
    int non_one_sp_ndims = 0;
    auto &strides = md.format_desc.blocking.strides;
    dim_t sp_size = strides[ndims - 1];
    bool sp_dense = true;
    for (int i = ndims - 1; i >= ndims - sp_ndims; i--) {
        if (md.dims[i] != 1) non_one_sp_ndims++;
        if (strides[i] != sp_size) sp_dense = false;
        sp_size *= md.dims[i];
    }
    if (non_one_sp_ndims == 1) return true;
    memory_desc_wrapper mdw(md);
    bool strided = mdw.is_plain() && !sp_dense;
    if (strided) return false;
    for (int i = 0; i < post_ops.len(); i++) {
        auto &po = post_ops.entry_[i];
        int mask = 0;
        if (po.is_prelu()) {
            mask = po.prelu.mask;
        } else if (po.is_binary()) {
            mask = utils::get_dims_mask(
                    md.dims, po.binary.src1_desc.dims, ndims);
        }
        // If the post-op is applied per D/H/W dimension then it cannot be
        // transformed to 1D.
        for (int i = ndims - sp_ndims; i < ndims; i++) {
            if ((mask & (1 << i)) != 0) return false;
        }
    }
    return true;
}

void conv_problem_t::normalize_shape() {
    normalize_conv_shape(id, od, kd, sd, dd, pd, ih, oh, kh, sh, dh, ph, iw, ow,
            kw, sw, dw, pw,
            can_reduce_to_1d(c_md(), conv_pd->attr()->post_ops_)
                    && can_reduce_to_1d(a_md(), post_ops_t())
                    && can_reduce_to_1d(b_md(), post_ops_t()),
            dhw_map);
}

const memory_desc_t &conv_problem_t::a_md() const {
    return *pick_a(conv_pd->invariant_src_md(), conv_pd->invariant_wei_md(),
            conv_pd->invariant_dst_md());
}

const memory_desc_t &conv_problem_t::b_md() const {
    return *pick_b(conv_pd->invariant_src_md(), conv_pd->invariant_wei_md(),
            conv_pd->invariant_dst_md());
}

const memory_desc_t &conv_problem_t::c_md() const {
    return *pick_c(conv_pd->invariant_src_md(), conv_pd->invariant_wei_md(),
            conv_pd->invariant_dst_md());
}

status_t conv_problem_t::init_abc_data_types(const hw_t &hw) {
    a_data_type = pick_a(src_data_type, wei_data_type, dst_data_type);
    b_data_type = pick_b(src_data_type, wei_data_type, dst_data_type);
    // Always use f32 for accumulation/storing in the main kernel.
    c_data_type = is_bwd_w
            ? data_type::f32
            : pick_c(src_data_type, wei_data_type, dst_data_type);

    if (utils::everyone_is(
                data_type::f32, a_data_type, b_data_type, c_data_type)) {

        // TODO: bf16 and f16 currently perform worse than tf32, this is
        // likely due to an extra reorder required on the b buffer.
        bool use_matching_fpmath
                = gpu_utils::dev_getenv("use_matching_fpmath", false);
        if (use_matching_fpmath
                && attr->mayiconvert(data_type::f32, data_type::bf16)
                && get_supported_fma_kind(
                           hw, data_type::bf16, data_type::bf16, data_type::f32)
                        != fma_kind_t::undef) {
            a_data_type = data_type::bf16;
            b_data_type = data_type::bf16;
        } else if (use_matching_fpmath
                && attr->mayiconvert(data_type::f32, data_type::f16)
                && get_supported_fma_kind(
                           hw, data_type::f16, data_type::f16, data_type::f32)
                        != fma_kind_t::undef) {
            a_data_type = data_type::f16;
            b_data_type = data_type::f16;
        } else if (attr->mayiconvert(data_type::f32, data_type::tf32)
                && get_supported_fma_kind(
                           hw, data_type::tf32, data_type::tf32, data_type::f32)
                        != fma_kind_t::undef) {
            a_data_type = data_type::tf32;
            b_data_type = data_type::tf32;
        }
    }

    a_data_type_size = (int)types::data_type_size(a_data_type);
    b_data_type_size = (int)types::data_type_size(b_data_type);
    c_data_type_size = (int)types::data_type_size(c_data_type);
    return status::success;
}

status_t conv_problem_t::init_acc_data_type() {
    auto a = a_data_type;
    auto b = b_data_type;
    auto c = c_data_type;
    bool is_fp8 = (utils::one_of(data_type::f8_e5m2, a, b, c)
            || utils::one_of(data_type::f8_e4m3, a, b, c));
    bool is_fp4 = (utils::one_of(data_type::f4_e2m1, a, b, c)
            || utils::one_of(data_type::f4_e3m0, a, b, c));
    acc_data_type = data_type::undef;
    if (utils::one_of(a, data_type::s8, data_type::u8)
            && utils::one_of(b, data_type::s8, data_type::u8)) {
        acc_data_type = data_type::s32;
    } else if (utils::everyone_is(data_type::f16, a, b)
            || utils::everyone_is(data_type::bf16, a, b)
            || utils::everyone_is(data_type::tf32, a, b)
            || utils::everyone_is(data_type::f32, a, b) || is_fp8 || is_fp4) {
        acc_data_type = data_type::f32;
    } else if (utils::everyone_is(data_type::f64, a, b)) {
        acc_data_type = data_type::f64;
    }
    if (acc_data_type == data_type::undef) return status::unimplemented;
    acc_data_type_size = (int)types::data_type_size(acc_data_type);
    return status::success;
}

bool conv_problem_t::with_sum_post_op() const {
    auto &post_ops = attr->post_ops_;
    return post_ops.find(primitive_kind::sum) != -1;
}

void conv_problem_t::init_transpose(const hw_t &hw) {
    bool is_dw = (g > 1) && (oc == 1) && (ic == 1);
    bool wei_any
            = (conv_pd->invariant_wei_md()->format_kind == format_kind::any);
    bool has_zp = !attr->zero_points_.has_default_values();
    bool allow_fwd = (mb <= 8 && oc <= 3 && ic <= 3 && kw <= 2)
            || (oc <= 2 && ic <= 2);
    bool allow_bwd_d = (mb <= 8 && oc <= 3 && ic <= 3);
    bool allow_bwd_w = (mb <= 8 && oc <= 3 && ic >= 16);
    ab_swap_transpose = wei_any && !is_dw && !has_zp;
    if (is_fwd) ab_swap_transpose &= allow_fwd;
    if (is_bwd_d) ab_swap_transpose &= allow_bwd_d;
    if (is_bwd_w) ab_swap_transpose &= allow_bwd_w;
    if (is_fwd && is_nchw_ok(*this, hw.to_ngen(), tensor_kind_t::src))
        ab_swap_transpose = true;
    if (is_bwd_d && is_nchw_ok(*this, hw.to_ngen(), tensor_kind_t::dst))
        ab_swap_transpose = true;
    ab_swap_transpose
            = gpu_utils::dev_getenv("ab_swap_transpose", ab_swap_transpose);
}

void normalize_conv_shape(dim_t &id, dim_t &od, dim_t &kd, dim_t &sd, dim_t &dd,
        dim_t &pd, dim_t &ih, dim_t &oh, dim_t &kh, dim_t &sh, dim_t &dh,
        dim_t &ph, dim_t &iw, dim_t &ow, dim_t &kw, dim_t &sw, dim_t &dw,
        dim_t &pw, bool can_flatten_spatial, std::array<int, 3> &dhw_map) {
    for (int i = 0; i < 3; i++)
        dhw_map[i] = -1;
    bool is_1x1 = (kd * kh * kw == 1);
    bool is_eq_oi = (od == id && oh == ih && ow == iw);
    if (is_1x1 && sd == 1 && sh == 1 && sw == 1 && is_eq_oi
            && can_flatten_spatial) {
        // Convert 3D to 1D convolution.
        gpu_assert(pd == 0 && ph == 0 && pw == 0);
        ow = od * oh * ow;
        iw = id * ih * iw;
        od = id = kd = 1;
        oh = ih = kh = 1;
        dhw_map[0] = dhw_map[1] = dhw_map[2] = 2;
        return;
    }
    // Propagate D -> H -> W. If the spatial dimension is not present, map it
    // to the next present dimension.
    std::vector<dim_t *> xd = {&id, &od, &kd, &sd, &dd, &pd};
    std::vector<dim_t *> xh = {&ih, &oh, &kh, &sh, &dh, &ph};
    std::vector<dim_t *> xw = {&iw, &ow, &kw, &sw, &dw, &pw};
    std::vector<dim_t *> x[3] = {std::move(xd), std::move(xh), std::move(xw)};
    std::vector<dim_t> x_old[3];
    std::vector<dim_t> xdef = {1, 1, 1, 1, 0, 0};
    bool has_dim[3] = {false, false, false};
    for (int i = 0; i < 3; i++) {
        x_old[i].resize(xdef.size());
        for (size_t j = 0; j < xdef.size(); j++) {
            if (*x[i][j] != xdef[j]) has_dim[i] = true;
            x_old[i][j] = *x[i][j];
        }
    }
    auto set = [](const std::vector<dim_t *> &x,
                       const std::vector<dim_t> &values) {
        for (size_t i = 0; i < x.size(); i++)
            *x[i] = values[i];
    };
    if (!has_dim[0] && !has_dim[1] && !has_dim[2]) has_dim[2] = true;
    int sp_count = has_dim[0] + has_dim[1] + has_dim[2];
    int shift = 3 - sp_count;
    for (int i = 0, idx = 0; i < 3; i++) {
        if (has_dim[i]) dhw_map[i] = shift + idx++;
        set(x[i], xdef);
    }
    for (int i = 0; i < 3; i++) {
        if (dhw_map[i] != -1) set(x[dhw_map[i]], x_old[i]);
    }
    if (!has_dim[2]) dhw_map[2] = 2;
    if (!has_dim[1]) dhw_map[1] = dhw_map[2];
    if (!has_dim[0]) dhw_map[0] = dhw_map[1];
}

pvar_t to_gemm(const pvar_t &d, prop_kind_t prop, bool is_transpose) {
    const bool is_fwd = (prop == prop_kind::forward);
    const bool is_bwd_d = (prop == prop_kind::backward_data);
    const bool is_bwd_w = (prop == prop_kind::backward_weights);
    auto transpose_gemm = [](const pvar_t &d) {
        if (d == pvars::m) return pvars::n;
        if (d == pvars::n) return pvars::m;
        if (d == pvars::k) return pvars::k;
        gpu_error_not_expected();
        return pvar_t();
    };
    auto pick
            = [&](const pvar_t &fwd, const pvar_t &bwd_d, const pvar_t &bwd_w) {
                  if (is_transpose) {
                      if (is_fwd) return transpose_gemm(fwd);
                      if (is_bwd_d) return transpose_gemm(bwd_d);
                      if (is_bwd_w) return transpose_gemm(bwd_w);
                  }
                  if (is_fwd) return fwd;
                  if (is_bwd_d) return bwd_d;
                  if (is_bwd_w) return bwd_w;
                  gpu_error_not_expected();
                  return pvar_t();
              };
    if (d == pvars::g) return pvars::b;
    if (d == pvars::mb) return pick(pvars::m, pvars::m, pvars::k);
    if (d == pvars::oc) return pick(pvars::n, pvars::k, pvars::n);
    if (d == pvars::ic) return pick(pvars::k, pvars::n, pvars::m);
    if (is_kernel_spatial(d)) return pick(pvars::k, pvars::k, pvars::m);
    if (is_output_spatial(d)) return pick(pvars::m, pvar_t(), pvars::k);
    if (is_input_spatial(d)) return pick(pvar_t(), pvars::m, pvar_t());
    return pvar_t();
}

tile_t to_gemm(const tile_t &t, prop_kind_t prop, bool is_transpose) {
    tile_t ret;
    ret[pvars::b] = 1;
    ret[pvars::m] = 1;
    ret[pvars::n] = 1;
    ret[pvars::k] = 1;
    for (auto &d : t) {
        auto gemm_d = to_gemm(d, prop, is_transpose);
        if (gemm_d.is_undef()) continue;
        ret[gemm_d] *= t[d];
    }
    return ret;
}

// Matches the user-provided descriptor against the list of supported plain tags.
std::string get_plain_user_tag(
        const conv_problem_t &prb, const memory_desc_t &md, bool is_wei) {
    memory_desc_wrapper mdw(md);
    if (mdw.is_plain() && !mdw.is_dense()) return "user";
    if (is_wei) {
        std::vector<const char *> plain_non_group_wei_tags
                = {"abx", "axb", "xba"};
        std::vector<const char *> plain_group_wei_tags
                = {"abcx", "abxc", "axcb"};
        auto &plain_wei_tags = (prb.with_groups ? plain_group_wei_tags
                                                : plain_non_group_wei_tags);
        gpu_assert(
                plain_non_group_wei_tags.size() == plain_group_wei_tags.size());
        for (size_t i = 0; i < plain_wei_tags.size(); i++) {
            if (matches_tag(md, plain_wei_tags[i])) {
                return plain_non_group_wei_tags[i];
            }
        }
    } else {
        for (auto *t : {"axb", "abx"}) {
            if (matches_tag(md, t)) return t;
        }
    }
    return {};
}

bool is_nchw_ok(const conv_problem_t &prb, ngen::HW hw, tensor_kind_t kind,
        bool nested) {
    gpu_assert(utils::one_of(kind, tensor_kind_t::src, tensor_kind_t::dst));
    bool is_src = (kind == tensor_kind_t::src);
    bool is_input = (is_src && prb.is_fwd) || (!is_src && prb.is_bwd_d)
            || prb.is_bwd_w;
    auto &md = (is_src ? *prb.conv_pd->invariant_src_md()
                       : *prb.conv_pd->invariant_dst_md());
    if (get_plain_user_tag(prb, md, false) != "abx") return false;
    // No block 2D message support before XeHPC.
    if (hw < ngen::HW::XeHPC) return false;
    // Strided access or element granularity for X offset are not generally
    // supported by block 2D messages.
    if (is_input) {
        if (prb.kw != 1 || prb.sw != 1) return false;
    }
    dim_t c = prb.g * (is_src ? prb.ic : prb.oc);
    dim_t d = (is_src ? prb.id : prb.od);
    dim_t h = (is_src ? prb.ih : prb.oh);
    dim_t w = (is_src ? prb.iw : prb.ow);
    int type_size = into<int>(types::data_type_size(
            is_src ? prb.src_data_type : prb.dst_data_type));
    if (!block_2d_width_ok(w, type_size)) return false;
    if (!block_2d_height_ok(c)) return false;
    if (!block_2d_pitch_ok(hw, d * h * w, type_size)) return false;
    return true;
}

} // namespace jit
} // namespace intel
} // namespace gpu
} // namespace impl
} // namespace dnnl
