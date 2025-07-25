/*******************************************************************************
* Copyright 2019-2025 Intel Corporation
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

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

#include "common/math_utils.hpp"
#include "common/utils.hpp"
#include "gpu/intel/compute/compute_engine.hpp"
#include "gpu/intel/compute/dispatch.hpp"
#include "gpu/intel/compute/utils.hpp"

namespace dnnl {
namespace impl {
namespace gpu {
namespace intel {
namespace compute {

// Compute optimal local work size for the given global work size.
compute::range_t get_optimal_lws(compute::range_t &gws,
        const dim_idx_t mapped_vec_dim_idx, const gpu_arch_t gpu_arch) {
    // Factors in descending order, prefer bigger sizes for local work size.
    const size_t optimal_lws_values[]
            = {256, 224, 192, 160, 128, 96, 64, 32, 16, 8, 7, 6, 5, 4, 3, 2, 1};
    const size_t optimal_vect_values[] = {256, 128, 64, 32, 16, 8, 4, 2, 1};

    auto match = [](const size_t *values, size_t gws_i, size_t max_lws_i,
                         size_t min_lws_i) {
        size_t lws_idx = 0;
        while (max_lws_i < values[lws_idx])
            lws_idx++;
        while (gws_i % values[lws_idx])
            lws_idx++;
        if (values[lws_idx] < min_lws_i) return min_lws_i;
        return values[lws_idx];
    };

    const auto ndims = gws.ndims();
    // Avoid GPU limitation where work-group size must fit in uint32_t
    auto lws_min = compute::range_t::empty(ndims);
    for (size_t i = 0; i < ndims; i++)
        lws_min[i]
                = utils::div_up(gws[i], std::numeric_limits<uint32_t>::max());

    const compute::range_t lws_max = [&]() {
        auto ret = compute::range_t::empty(ndims);
        size_t max = 256;
        size_t min = 1;
        for (dim_t i = ndims - 1; i >= 0; i--) {
            ret[i] = std::max(max / min, size_t(1));
            min *= lws_min[i];
        }
        return ret;
    }();

    // Starting from XE_HP subgroups may not be contained in lws[0] when lws[0]
    // is not a power of 2. To account for this, we consider multiple allocation
    // strategies which require subgroups to be contained in lws[0] and take the
    // best outcome.

    auto lws_1d = [&]() {
        auto ret = compute::range_t::one(ndims);
        if (lws_min.nelems() == lws_min[0]) {
            ret[0] = match(optimal_lws_values, gws[0], lws_max[0], lws_min[0]);
        }
        return ret;
    }();

    auto lws_nd = compute::range_t::one(ndims);
    size_t total_lws = 1;

    // Iterate through global work size and calculate max divisor from
    // the array optimal_lws_values.
    for (size_t i = 0; i < ndims; ++i) {
        auto rest_lws = std::max(lws_max[i] / total_lws, size_t(1));
        auto lws_i = (static_cast<size_t>(mapped_vec_dim_idx) == i
                             && gpu_arch >= gpu_arch_t::xe_hp)
                ? match(optimal_vect_values, gws[i], rest_lws,
                        utils::rnd_up_pow2(lws_min[i]))
                : match(optimal_lws_values, gws[i], rest_lws, lws_min[i]);

        lws_nd[i] *= lws_i;
        total_lws *= lws_i;
    }

    auto ret_lws = lws_nd.nelems() >= lws_1d.nelems() ? lws_nd : lws_1d;

    // Ensure uniform work-groups
    for (dim_idx_t i = 0; i < ndims; i++) {
        gws[i] = utils::rnd_up(gws[i], ret_lws[i]);
    }

    return ret_lws;
}

dispatch_t::dispatch_t(const compute_engine_t *engine, const memory_desc_t *md)
    : engine_(engine) {

    if (md && md->format_kind == dnnl_blocked) {
        md_ndims_ = md->ndims;
        auto &blocking = md->format_desc.blocking;
        auto *strides = blocking.strides;
        std::pair<int, dim_t> sorted_strides[DNNL_MAX_NDIMS];
        for (int i = 0; i < md->ndims; ++i) {
            sorted_strides[i] = {i, strides[i]};
            for (int j = 0; j < blocking.inner_nblks; j++) {
                if (blocking.inner_idxs[j] == i) {
                    int str = 1;
                    for (int k = blocking.inner_nblks - 1; k > j; k--)
                        str *= blocking.inner_blks[k];
                    sorted_strides[i] = {i, str};
                    break;
                }
            }
        }
        std::sort(sorted_strides, sorted_strides + md->ndims,
                [](const std::pair<int, dim_t> &a,
                        const std::pair<int, dim_t> &b) {
                    return a.second < b.second;
                });
        for (int i = 0; i < md->ndims; i++) {
            md_nesting_levels_[sorted_strides[i].first] = md->ndims - i - 1;
        }
    }
}

std::string dispatch_t::str() const {
    ostringstream_t oss;
    for (dim_idx_t i = 0; i < ndims_; ++i) {
        auto &d = dims_[i];
        oss << "    "
            << "dim #" << i << " name: " << std::setw(10) << d.name
            << " size: " << std::setw(6) << d.size << " block: " << std::setw(4)
            << d.block << " nesting_level: " << std::setw(4) << d.nesting_level
            << " vsize: " << std::setw(4) << d.vector_size
            << " gws_idx: " << d.gws_index << std::endl;
    }
    return oss.str();
}

void dispatch_t::define_dim_with_nesting_level(
        const std::string &name, int nesting_level, dim_t size, dim_t block) {
    for (dim_idx_t i = 0; i < ndims_; ++i)
        gpu_assert(dims_[i].name != name)
                << "Name " << dims_[i].name << " is not unique";

    dim_info_t di;
    di.name = name;
    di.size = size;
    di.block = block;
    di.nesting_level = nesting_level;
    di.vector_size = 1;
    di.gws_index = -1;
    dims_[ndims_] = std::move(di);

    ++ndims_;
}

status_t dispatch_t::vectorize_dim(const std::string &name, int vector_size) {
    if (!engine_->mayiuse_sub_group(vector_size)) return status::unimplemented;
    assert(vector_size > 1);
    for (dim_idx_t i = 0; i < ndims_; ++i) {
        if (dims_[i].name == name) {
            assert(dims_[i].size % vector_size == 0);
            assert(dims_[i].size % (vector_size * dims_[i].block) == 0);
            dims_[i].vector_size = vector_size;
            return status::success;
        }
    }
    assert(!"not found");
    return status::invalid_arguments;
}

void dispatch_t::def_kernel_macros(kernel_ctx_t &kernel_ctx) const {
    assert(generate_called && "generate() must be called.");

    // Find a unique prefix (in case there are many kernels in a file).
    std::string gws_prefix;
    for (int i = 0; i < 4; i++) {
        if (!kernel_ctx.has_macro(utils::format("GWS%d_DEF", i))) {
            gws_prefix = "GWS" + std::to_string(i);
            break;
        }
    }

    assert(!gws_prefix.empty());

    kernel_ctx.define_int(utils::format("%s_DEF", gws_prefix.c_str()), 1);

    for (dim_idx_t i = 0; i < ndims_; ++i) {
        auto get_dim_str = utils::format("-DGWS_GET_%s=%s_GET_ID%d",
                dims_[i].name.c_str(), gws_prefix.c_str(), i);
        kernel_ctx.add_option(get_dim_str);

        auto get_block_str = utils::format("-DGWS_GET_%s_BLOCK=%s_GET_BLOCK%d",
                dims_[i].name.c_str(), gws_prefix.c_str(), i);
        kernel_ctx.add_option(get_block_str);
        kernel_ctx.define_int(utils::format("%s_IDX%d", gws_prefix.c_str(), i),
                dims_[i].gws_index);
        kernel_ctx.define_int(
                utils::format("%s_STRIDE%d", gws_prefix.c_str(), i),
                get_gws_stride(i));

        bool is_zero = dims_[i].size <= 1;
        bool is_zero_stride = get_gws_stride(i) == 0;
        bool is_outermost = (i == ndims_ - 1)
                || dims_[i + 1].gws_index != dims_[i].gws_index;
        const char *op_name = is_zero || is_zero_stride ? "GWS_OP_ZERO"
                : is_outermost                          ? "GWS_OP_FIRST"
                                                        : "GWS_OP_MOD";
        kernel_ctx.add_option(
                utils::format("-D%s_OP%d=%s", gws_prefix.c_str(), i, op_name));
        kernel_ctx.define_int(utils::format("%s_DIM%d", gws_prefix.c_str(), i),
                dims_[i].size);
        kernel_ctx.define_int(
                utils::format("%s_VEC_SIZE%d", gws_prefix.c_str(), i),
                dims_[i].vector_size);
        kernel_ctx.define_int(
                utils::format("%s_BLOCK%d", gws_prefix.c_str(), i),
                dims_[i].block);
    }

    // Local work size and subgroup sizes.
    dim_idx_t vec_dim_idx = find_vectorized_dim();
    kernel_ctx.define_int(utils::format("GWS_WITH_SG_%s", attr_suffix_),
            vec_dim_idx != dim_not_found);

    if (vec_dim_idx != dim_not_found)
        kernel_ctx.define_int(utils::format("GWS_SGS_%s", attr_suffix_),
                dims_[vec_dim_idx].vector_size);

    auto r = nd_range();
    if (r.local_range()) {
        for (size_t i = 0; i < r.global_range().ndims(); i++) {
            kernel_ctx.define_int(
                    utils::format("GWS_LWS%zu_%s", i, attr_suffix_),
                    into<int64_t>(r.local_range()[i]));
        }
    }

    compute::range_t gws_actual {1, 1, 1};
    for (dim_idx_t i = 0; i < ndims_; i++) {
        const auto &d = dims_[i];
        gws_actual[d.gws_index] *= utils::div_up(d.size, d.block);
    };
    auto &gws = nd_range_.global_range();
    for (dim_idx_t i = 0; i < 3; i++) {
        if (i < nd_range_.ndims() && gws[i] > gws_actual[i]) {
            std::string overflow_check = utils::format(
                    "-DGWS%d_OVERFLOW=\"(get_global_id(%d) >= %zu%s)\"", i, i,
                    gws_actual[i], gws_actual[i] > UINT32_MAX ? "ul" : "u");
            kernel_ctx.add_option(overflow_check);
        } else {
            std::string overflow_check
                    = utils::format("-DGWS%d_OVERFLOW=false", i);
            kernel_ctx.add_option(overflow_check);
        }
    }
}

void dispatch_t::generate(bool generate_lws) {
    // Keep order of elements with the same nesting level unchanged.
    std::stable_sort(dims_, dims_ + ndims_,
            [](const dim_info_t &a, const dim_info_t &b) {
                return a.nesting_level > b.nesting_level;
            });

    // XXX: Move dimensions with size = 1 to the end.
    for (int i = ndims_ - 2; i >= 0; --i) {
        if (dims_[i].size == 1) {
            for (dim_idx_t j = i; j < ndims_ - 1; ++j) {
                if (dims_[j + 1].size == 1) break;
                std::swap(dims_[j], dims_[j + 1]);
            }
        }
    }

    // Find vectorized dimension (if any).
    dim_idx_t vec_dim_idx = find_vectorized_dim();

    // Compute GWS indices.
    for (dim_idx_t i = 0; i < ndims_; ++i) {
        if (vec_dim_idx == dim_not_found) {
            // Keep up to 4 dims in gws[0] to have bigger choice for work group
            // size.
            dims_[i].gws_index = std::min(2, std::max(0, into<int>(i) - 3));
        } else {
            // With vectorized dimension, work group size choices are more
            // limited so no need to group dimensions together.
            dims_[i].gws_index = std::min(2, into<int>(i));
        }
    }

    compute::range_t gws = compute::range_t::one();
    for (int i = ndims_ - 1; i >= 0; --i) {
        dim_t block = std::max(dims_[i].block, (dim_t)1);
        int gws_index = dims_[i].gws_index;
        gws[gws_index] *= utils::div_up(dims_[i].size, block);
    }

    size_t gws_size = gws.nelems();

    auto *dev_info = engine_->device_info();
    size_t hw_threads = dev_info->hw_threads();

    // Calculate block sizes for the dimensions with flexible blocking.
    for (dim_idx_t i = 0; i < ndims_; ++i) {
        if (dims_[i].block == 0) {
            int gws_index = dims_[i].gws_index;
            // Heuristic: use max blocking but keep at least eu_count work items.
            size_t max_block = std::max((size_t)1, gws_size / hw_threads);
            size_t block = utils::max_div(dims_[i].size, max_block);
            gws[gws_index] /= block;
            gws_size /= block;
            dims_[i].block = block;
        }
    }

    // Handle a vectorized dimension (if presented).
    compute::range_t lws;
    if (generate_lws) {

        if (vec_dim_idx != dim_not_found) {
            lws = compute::range_t::one(gws.ndims());
            int gws_index = dims_[vec_dim_idx].gws_index;
            size_t vec_size = into<size_t>(dims_[vec_dim_idx].vector_size);
            size_t nblocks = into<size_t>(
                    dims_[vec_dim_idx].size / dims_[vec_dim_idx].block);
            // XXX: max 256 work items per group
            lws[gws_index]
                    = utils::max_div(gws[gws_index] / vec_size, 256 / vec_size)
                    * vec_size;
            lws[gws_index] = utils::max_div(nblocks / vec_size,
                                     lws[gws_index] / vec_size)
                    * vec_size;

            // Move the vectorized dimension to the first place in the group.
            dim_idx_t group_beg = ndims_ - 1;
            dim_idx_t group_end = 0;
            for (dim_idx_t i = 0; i < ndims_; ++i) {
                if (dims_[i].gws_index == gws_index) {
                    group_beg = std::min(group_beg, i);
                    group_end = std::max(group_end, i);
                }
            }

            if (vec_dim_idx != group_beg) {
                auto vec_dim_info = dims_[vec_dim_idx];
                for (int i = vec_dim_idx - 1; i >= into<int>(group_beg); --i) {
                    dims_[i + 1] = dims_[i];
                }
                dims_[group_beg] = std::move(vec_dim_info);
            }
        }

        // Use a work-group size = 1 if the number of work items < HW threads.
        if (!lws && gws_size < hw_threads) {
            lws = compute::range_t::one(gws.ndims());
        }

        if (!lws) {
            // Compute the best lws.
            lws = get_optimal_lws(gws,
                    vec_dim_idx != dim_idx::invalid
                            ? dims_[vec_dim_idx].gws_index
                            : dim_idx::invalid,
                    dev_info->gpu_arch());
            gpu_assert(lws) << "Unexpected missing lws";
        } else {
            // Last ditch effort to avoid dispatching restriction on Intel GPUs.
            for (size_t i = 0; i < gws.ndims(); i++) {
                if (gws[i] > lws[i] * UINT_MAX) {
                    lws[i] *= utils::div_up(gws[i], UINT_MAX);
                    gws[i] = utils::rnd_up(gws[i], lws[i]);
                }
            }
        }
    }

    nd_range_ = nd_range_t(gws, lws);
    generate_called = true;
}

// Allows manual setting of global and local work sizes.
void dispatch_t::generate_override(
        const range_t &grange, const range_t &lrange) {
    dims_[1].gws_index = 2;
    dims_[2].gws_index = 1;
    dims_[3].gws_index = 0;

    nd_range_ = nd_range_t(grange, lrange);
    generate_called = true;
}

// Allows manual setting of local work sizes.
void dispatch_t::set_lws(const compute::range_t &lrange) {
    assert(generate_called);
    const auto &grange = nd_range_.global_range();
    nd_range_ = nd_range_t(grange, lrange);
}

void dispatch_t::define_dim_with_md_hint(const std::string &name,
        dim_idx_t md_hint_index, dim_t size, dim_t block) {
    int nesting_level = min_nesting_level;
    if (md_ndims_ > 0) {
        assert(md_hint_index >= 0 && md_hint_index < md_ndims_);
        nesting_level = md_nesting_levels_[md_hint_index];
    }

    define_dim_with_nesting_level(name, nesting_level, size, block);
}

} // namespace compute
} // namespace intel
} // namespace gpu
} // namespace impl
} // namespace dnnl
