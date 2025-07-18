/*******************************************************************************
* Copyright 2022-2025 Intel Corporation
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

#include "utils/parser.hpp"

#include "parser.hpp"
#include "utils.hpp"

namespace graph {

using namespace parser;

namespace {
bool parse_string(
        std::string &val, const char *str, const std::string &option_name) {
    const std::string pattern = std::string("--") + option_name + "=";
    if (pattern.find(str, 0, pattern.size()) == std::string::npos) return false;
    str = str + pattern.size();
    return val = str, true;
}

void parse_key_value(std::vector<std::map<size_t, std::string>> &res_v,
        const std::string &key_val_str, const std::string &option_name = "") {
    if (key_val_str.empty()) return;
    res_v.clear();

    // Expected format: KEY1:VAL1[+KEY2:VAL2...]
    std::string::size_type case_pos = 0;
    while (case_pos != std::string::npos) {
        std::string case_str = get_substr(key_val_str, case_pos, ',');
        // Process empty entry when several passed: `--OPT=,KEY1:VAL1...`.
        if (case_str.empty()) {
            res_v.push_back({{0, "default"}});
            continue;
        }

        std::string::size_type val_pos = 0;
        std::map<size_t, std::string> key_val_case;
        while (val_pos < case_str.size()) {
            std::string single_key_val = get_substr(case_str, val_pos, '+');
            if (single_key_val.empty()) continue;

            std::string::size_type key_pos = 0;
            std::string key_str = get_substr(single_key_val, key_pos, ':');
            if (key_pos == std::string::npos) {
                BENCHDNN_PRINT(0,
                        "Error: a colon separating the key and value was not "
                        "found. Parsed input for option \'%s\': \'%s\'. Please "
                        "check the option documentation.\n",
                        option_name.c_str(), key_str.c_str());
                SAFE_V(FAIL);
            }

            std::string val_str
                    = single_key_val.substr(key_pos, val_pos - key_pos);
            if (val_str.empty()) {
                BENCHDNN_PRINT(0,
                        "Error: a value after colon was not parsed. Parsed "
                        "input for option \'%s\': \'%s\'. Please check the "
                        "option documentation.\n",
                        option_name.c_str(), single_key_val.c_str());
                SAFE_V(FAIL);
            }

            const auto key_num = size_t(stoll(key_str));
            if (key_val_case.count(key_num)) {
                BENCHDNN_PRINT(0,
                        "Error: a tensor with \'%zu\' ID was already updated. "
                        "Previous value for the option \'%s\' with this ID is "
                        "\'%s\', new value is \'%s\'.\n",
                        key_num, option_name.c_str(),
                        key_val_case.at(key_num).c_str(), val_str.c_str());
                SAFE_V(FAIL);
            }
            key_val_case.emplace(key_num, val_str);
        }
        res_v.push_back(key_val_case);
    }
}

// Copy-pasted from utils::parser. Refer to documentation there.
std::string get_substr(const std::string &s, size_t &start_pos, char delim) {
    auto end_pos = s.find_first_of(delim, start_pos);
    auto sub = s.substr(start_pos, end_pos - start_pos);
    start_pos = end_pos + (end_pos != eol);
    return sub;
}

} // namespace

bool parse_input_shapes(
        std::vector<std::map<size_t, std::string>> &in_shapes_vec,
        const char *str, const std::string &option_name) {
    std::string in_shapes_str;
    if (!parse_string(in_shapes_str, str, option_name)) return false;
    parse_key_value(in_shapes_vec, in_shapes_str, option_name);
    return true;
}

bool parse_op_attrs(std::vector<std::map<size_t, std::string>> &op_attrs_vec,
        const char *str) {
    std::string op_attrs_str;
    if (!parse_string(op_attrs_str, str, "op-attrs")) return false;
    return parse_key_value(op_attrs_vec, op_attrs_str), true;
}

bool parse_op_kind(std::vector<std::map<size_t, std::string>> &op_kind_map,
        const char *str, const std::string &option_name) {
    std::string s;
    if (!parse_string(s, str, option_name)) return false;

    //--op-kind=ID:KIND[+ID:KIND], change the kind should not change the topology
    if (s.find(":") == std::string::npos) {
        BENCHDNN_PRINT(0, "%s\n",
                "Error: --op-kind is not correctly specified with a pair of op "
                "id and target op kind.");
        SAFE_V(FAIL);
    }
    parse_key_value(op_kind_map, s, option_name);
    return true;
}

bool parse_dt(std::vector<dnnl_data_type_t> &dt,
        std::vector<std::map<size_t, dnnl_data_type_t>> &dt_map,
        const char *str, const std::string &option_name) {
    std::string dts_str;
    if (!parse_string(dts_str, str, option_name)) return false;

    if (dts_str.find(":") == std::string::npos) {
        // `dt` object: format like --dt=f32,bf16,f16
        const bool has_dt_map
                = dt_map.size() != 1 || (dt_map[0].count(SIZE_MAX) == 0);
        if (has_dt_map) {
            BENCHDNN_PRINT(0, "%s\n",
                    "Error: --dt is specified twice with different styles.");
            SAFE_V(FAIL);
        }

        const std::vector<dnnl_data_type_t> def_dt = {dnnl_data_type_undef};
        return parser::parse_dt(dt, def_dt, str, option_name);
    } else {
        // `dt_map` object: format like --dt=0:f32+1:f32,0:f16+1:f16
        const bool has_dt = dt.size() != 1 || dt[0] != dnnl_data_type_undef;
        if (has_dt) {
            BENCHDNN_PRINT(0, "%s\n",
                    "Error: --dt is specified twice with different styles.");
            SAFE_V(FAIL);
        }

        std::vector<std::map<size_t, std::string>> dts_tmp;
        parse_key_value(dts_tmp, dts_str, option_name);
        dt_map.clear();
        dt_map.resize(dts_tmp.size());
        // convert size_t:string to size_t:dnnl_data_type_t
        for (size_t i = 0; i < dts_tmp.size(); i++) {
            std::map<size_t, dnnl_data_type_t> tmp;
            for (const auto &v : dts_tmp[i]) {
                tmp[v.first] = str2dt(v.second.c_str());
            }
            dt_map[i] = std::move(tmp);
        }
    }

    return true;
}

bool parse_graph_expected_n_partitions(
        std::vector<size_t> &expected_n_partition_vec, const char *str) {
    std::string expected_n_partitions_str;
    if (!parse_string(expected_n_partitions_str, str, "expected-n-partitions"))
        return false;

    dnnl::impl::stringstream_t ss(expected_n_partitions_str);

    std::string expected_n_partitions;
    while (std::getline(ss, expected_n_partitions, ',')) {
        if (!expected_n_partitions.empty()) {
            expected_n_partition_vec.clear();

            const auto int_expected_n_partitions
                    = std::stoi(expected_n_partitions);
            if (int_expected_n_partitions >= 0) {
                expected_n_partition_vec.emplace_back(
                        int_expected_n_partitions);
            } else {
                BENCHDNN_PRINT(0,
                        "Error: expected-n-partitions option supports only"
                        "non-negative numbers, but `%d` was specified.\n",
                        int_expected_n_partitions);
                SAFE_V(FAIL);
            }
        }
    }
    return true;
}

bool parse_graph_fpmath_mode(
        std::vector<graph_fpmath_mode_t> &fpmath_mode_vec, const char *str) {
    std::string graph_attrs_str;
    if (!parse_string(graph_attrs_str, str, "attr-fpmath")) return false;

    dnnl::impl::stringstream_t ss(graph_attrs_str);

    std::string mode;
    while (std::getline(ss, mode, ',')) {
        if (!mode.empty()) {
            // override_json_value == false indicates that the fpmath mode is
            // not from the cml knob.
            if (fpmath_mode_vec.size() == 1
                    && !fpmath_mode_vec.front().override_json_value_)
                fpmath_mode_vec.pop_back();

            size_t start_pos = 0;
            auto mode_subs = get_substr(mode, start_pos, ':');
            if (start_pos != std::string::npos && start_pos >= mode.size()) {
                BENCHDNN_PRINT(0, "%s \'%s\'\n",
                        "Error: dangling symbol at the end of input",
                        mode.c_str());
                SAFE_V(FAIL);
            }

            bool apply_to_int = false;
            if (start_pos != std::string::npos) {
                auto bool_subs = get_substr(mode, start_pos, '\0');
                if (start_pos != std::string::npos) {
                    BENCHDNN_PRINT(0, "%s \'%s\'\n",
                            "Error: dangling symbol at the end of input",
                            mode.c_str());
                    SAFE_V(FAIL);
                }
                apply_to_int = str2bool(bool_subs.c_str());
            }
            fpmath_mode_vec.emplace_back(
                    mode_subs, apply_to_int, /* override_json_value = */ true);
        }
    }
    return true;
}

std::map<std::string, std::string> parse_attrs(const std::string &attrs_str) {
    std::map<std::string, std::string> attrs_map;
    std::string::size_type key_pos = 0;
    std::string::size_type key_end, val_pos, val_end;
    std::map<size_t, std::string> key_val_case;
    while ((key_end = attrs_str.find(':', key_pos)) != std::string::npos) {
        if ((val_pos = attrs_str.find_first_not_of(':', key_end))
                == std::string::npos)
            break;
        val_end = attrs_str.find('*', val_pos);
        std::string key_str = attrs_str.substr(key_pos, key_end - key_pos);
        std::string val_str = attrs_str.substr(val_pos, val_end - val_pos);
        // Validation of input happens at `deserialized_op_t::create()`.
        if (attrs_map.count(key_str)) {
            attrs_map[key_str] = val_str;
            BENCHDNN_PRINT(0, "Repeat attr: %s, will use last value for it.\n",
                    key_str.c_str());
        } else {
            attrs_map.emplace(key_str, val_str);
        }
        key_pos = val_end;
        if (key_pos != std::string::npos) ++key_pos;
    }
    return attrs_map;
}

// Convert f32 vec attrs string into f32 vec
// e.g. 1.0x2.2 -> {1.0, 2.2}
std::vector<float> string_to_f32_vec(const std::string &val_str) {
    std::vector<float> f32_vec;
    std::string::size_type pos = 0;
    while (pos != std::string::npos) {
        std::string num_str = get_substr(val_str, pos, 'x');
        if (!num_str.empty()) {
            f32_vec.emplace_back(atof(num_str.c_str()));
        } else {
            fprintf(stderr,
                    "graph: Parser: invalid attr value `%s`, exiting...\n",
                    val_str.c_str());
            SAFE_V(FAIL);
        }
    }
    return f32_vec;
}

// Convert shape string from cml into dims_t
// e.g. 1x2x3 -> {1,2,3}
dims_t string_to_shape(const std::string &shape_str) {
    dims_t shape;
    std::string::size_type pos = 0;
    while (pos != std::string::npos) {
        std::string dim_str = get_substr(shape_str, pos, 'x');
        if (!dim_str.empty()) {
            shape.emplace_back(atof(dim_str.c_str()));
        } else {
            fprintf(stderr,
                    "graph: Parser: invalid shape value `%s`, exiting...\n",
                    shape_str.c_str());
            SAFE_V(FAIL);
        }
    }
    return shape;
}

bool parse_input_file(std::string &json_file, const char *str) {
    return parse_string(json_file, str, "case");
}

} // namespace graph
