/*
   Copyright 2018 Marius Appel <marius.appel@uni-muenster.de>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include "cube_factory.h"

#include "apply_pixel.h"
#include "dummy.h"
#include "external/json.hpp"
#include "filesystem.h"
#include "filter_pixel.h"
#include "image_collection_cube.h"
#include "join_bands.h"
#include "reduce.h"
#include "reduce_time.h"
#include "select_bands.h"
#include "stream.h"
#include "window_time.h"

namespace gdalcubes {

cube_factory* cube_factory::_instance = 0;

std::shared_ptr<cube> cube_factory::create_from_json(nlohmann::json j) {
    if (!j.count("cube_type")) {
        throw std::string("ERROR in cube_factory::create_from_json(): invalid object, missing cube_type key.");
    }

    std::string cube_type = j["cube_type"];

    return (cube_generators[cube_type](j));  //recursive creation
}

void cube_factory::register_cube_type(std::string type_name,
                                      std::function<std::shared_ptr<cube>(nlohmann::json&)> generator) {
    cube_generators.insert(std::make_pair(type_name, generator));
}

void cube_factory::register_default() {
    /* register data cube types */
    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "reduce", [](nlohmann::json& j) {
            auto x = reduce_cube::create(instance()->create_from_json(j["in_cube"]), j["reducer"].get<std::string>());
            return x;
        }));
    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "reduce_time", [](nlohmann::json& j) {
            // std::vector<std::pair<std::string, std::string>> band_reducers = j["reducer_bands"].get<std::vector<std::pair<std::string, std::string>>>();
            auto x = reduce_time_cube::create(instance()->create_from_json(j["in_cube"]), j["reducer_bands"].get<std::vector<std::pair<std::string, std::string>>>());
            return x;
        }));
    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "reduce_space", [](nlohmann::json& j) {
            // std::vector<std::pair<std::string, std::string>> band_reducers = j["reducer_bands"].get<std::vector<std::pair<std::string, std::string>>>();
            auto x = reduce_time_cube::create(instance()->create_from_json(j["in_cube"]), j["reducer_bands"].get<std::vector<std::pair<std::string, std::string>>>());
            return x;
        }));

    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "window_time", [](nlohmann::json& j) {
            if (j.count("kernel") > 0) {
                return window_time_cube::create(instance()->create_from_json(j["in_cube"]), j["kernel"].get<std::vector<double>>(),
                                                j["win_size_l"].get<uint16_t>(), j["win_size_r"].get<std::uint16_t>());
            } else {
                return window_time_cube::create(instance()->create_from_json(j["in_cube"]), j["reducer_bands"].get<std::vector<std::pair<std::string, std::string>>>(),
                                                j["win_size_l"].get<uint16_t>(), j["win_size_r"].get<std::uint16_t>());
            }
        }));
    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "select_bands", [](nlohmann::json& j) {
            auto x = select_bands_cube::create(instance()->create_from_json(j["in_cube"]), j["bands"].get<std::vector<std::string>>());
            return x;
        }));

    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "filter_pixel", [](nlohmann::json& j) {
            auto x = filter_pixel_cube::create(instance()->create_from_json(j["in_cube"]), j["predicate"].get<std::string>());
            return x;
        }));

    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "apply_pixel", [](nlohmann::json& j) {
            if (j.count("band_names") > 0) {
                auto x = apply_pixel_cube::create(instance()->create_from_json(j["in_cube"]), j["expr"].get<std::vector<std::string>>(), j["band_names"].get<std::vector<std::string>>());
                return x;
            } else {
                auto x = apply_pixel_cube::create(instance()->create_from_json(j["in_cube"]), j["expr"].get<std::vector<std::string>>());
                return x;
            }
        }));
    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "join_bands", [](nlohmann::json& j) {
            auto x = join_bands_cube::create(instance()->create_from_json(j["A"]), instance()->create_from_json(j["B"]), j["prefix_A"].get<std::string>(), j["prefix_B"].get<std::string>());
            return x;
        }));

    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "stream", [](nlohmann::json& j) {
            auto x = stream_cube::create(instance()->create_from_json(j["in_cube"]), j["command"].get<std::string>(), j["file_streaming"].get<bool>());
            return x;
        }));

    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "image_collection", [](nlohmann::json& j) {
            if (!filesystem::exists(j["file"].get<std::string>())) {
                throw std::string("ERROR in cube_generators[\"image_collection\"](): image collection file does not exist.");
            }
            cube_view v = cube_view::read_json_string(j["view"].dump());
            auto x = image_collection_cube::create(j["file"].get<std::string>(), v);
            x->set_chunk_size(j["chunk_size"][0].get<uint32_t>(), j["chunk_size"][1].get<uint32_t>(), j["chunk_size"][2].get<uint32_t>());

            if (j.count("mask") > 0) {
                if (j["mask"].count("mask_type") == 0) {
                    GCBS_WARN("ERROR in cube_generators[\"image_collection\"](): missing mask type, mask will be ignored");
                } else {
                    std::string mask_type = j["mask"]["mask_type"];
                    if (mask_type == "value_mask") {
                        x->set_mask(j["mask"]["mask_band"], std::make_shared<value_mask>(j["mask"]["values"].get<std::unordered_set<double>>(), j["mask"]["invert"].get<bool>()));
                    } else if (mask_type == "range_mask") {
                        x->set_mask(j["mask"]["mask_band"], std::make_shared<range_mask>(j["mask"]["min"].get<double>(), j["mask"]["max"].get<double>(), j["mask"]["invert"].get<bool>()));
                    } else {
                        GCBS_WARN("ERROR in cube_generators[\"image_collection\"](): invalid mask type, mask will be ignored");
                    }
                }
            }

            if (j.count("warp_args") > 0) {
                x->set_warp_args(j["warp_args"].get<std::vector<std::string>>());
            }

            return x;
        }));

    cube_generators.insert(std::make_pair<std::string, std::function<std::shared_ptr<cube>(nlohmann::json&)>>(
        "dummy", [](nlohmann::json& j) {
            cube_view v = cube_view::read_json_string(j["view"].dump());
            auto x = dummy_cube::create(v, j["nbands"].get<uint16_t>(), j["fill"].get<double>());
            x->set_chunk_size(j["chunk_size"][0].get<uint32_t>(), j["chunk_size"][1].get<uint32_t>(), j["chunk_size"][2].get<uint32_t>());
            return x;
        }));
}

}  // namespace gdalcubes