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

#ifndef REDUCE_H
#define REDUCE_H

#include "cube.h"

/**
 * @brief A data cube that applies a reducer function to another data cube over time
 * @deprecated This class will be eventually replace by the more flexible reduce_time_cube, which allows to apply
 * different reducers to different bands of the input cube
 */
class reduce_cube : public cube {
   public:
    /**
     * @brief Create a data cube that applies a reducer function on a given input data cube over time
     * @note This static creation method should preferably be used instead of the constructors as
     * the constructors will not set connections between cubes properly.
     * @param in input data cube
     * @param reducer reducer function
     * @return a shared pointer to the created data cube instance
     */
    static std::shared_ptr<reduce_cube> create(std::shared_ptr<cube> in, std::string reducer = "mean") {
        std::shared_ptr<reduce_cube> out = std::make_shared<reduce_cube>(in, reducer);
        in->add_child_cube(out);
        out->add_parent_cube(in);
        return out;
    }

   public:
    reduce_cube(std::shared_ptr<cube> in, std::string reducer = "mean") : cube(std::make_shared<cube_st_reference>(*(in->st_reference()))), _in_cube(in), _reducer(reducer) {  // it is important to duplicate st reference here, otherwise changes will affect input cube as well
        _st_ref->dt() = _st_ref->t1() - _st_ref->t0();
        _st_ref->t1() = _st_ref->t0();  // set nt=1
        assert(_st_ref->nt() == 1);
        _chunk_size[0] = 1;
        _chunk_size[1] = _in_cube->chunk_size()[1];
        _chunk_size[2] = _in_cube->chunk_size()[2];

        for (uint16_t ib = 0; ib < in->bands().count(); ++ib) {
            band b = in->bands().get(ib);
            if (in->size_t() > 1) {
                b.name = b.name + "_" + reducer;  // Change name only if input is not yet reduced
            }
            _bands.add(b);
        }

        if (!(reducer == "min" ||
              reducer == "max" ||
              reducer == "mean" ||
              reducer == "median" ||
              reducer == "count" ||
              reducer == "var" ||
              reducer == "sd" ||
              reducer == "prod" ||
              reducer == "sum"))
            throw std::string("ERROR in reduce_cube::reduce_cube(): Unknown reducer given");
    }

   public:
    ~reduce_cube() {}

    std::shared_ptr<chunk_data> read_chunk(chunkid_t id) override;

    /**
 * Combines all chunks and produces a single GDAL image
 * @param path path to output image file
 * @param format GDAL format (see https://www.gdal.org/formats_list.html)
 * @param co GDAL create options
     * @param p chunk processor instance, defaults to the current global configuration in config::instance()->get_default_chunk_processor()
 */
    void write_gdal_image(std::string path, std::string format = "GTiff", std::vector<std::string> co = std::vector<std::string>(), std::shared_ptr<chunk_processor> p = config::instance()->get_default_chunk_processor());

    nlohmann::json make_constructible_json() override {
        nlohmann::json out;
        out["cube_type"] = "reduce";
        out["reducer"] = _reducer;
        out["in_cube"] = _in_cube->make_constructible_json();
        return out;
    }

   private:
    std::shared_ptr<cube> _in_cube;
    std::string _reducer;

    virtual void set_st_reference(std::shared_ptr<cube_st_reference> stref) override {
        // copy fields from st_reference type
        _st_ref->win() = stref->win();
        _st_ref->proj() = stref->proj();
        _st_ref->ny() = stref->ny();
        _st_ref->nx() = stref->nx();
        _st_ref->t0() = stref->t0();
        _st_ref->t1() = stref->t1();
        _st_ref->dt() = stref->dt();

        _st_ref->dt() = _st_ref->t1() - _st_ref->t0();
        _st_ref->t1() = _st_ref->t0();  // set nt=1
        //assert(_st_ref->nt() == 1);
    }
};

#endif  //REDUCE_H
