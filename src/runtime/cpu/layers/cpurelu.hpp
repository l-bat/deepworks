#pragma once

#include <deepworks/tensor.hpp>

#include "runtime/cpu/layers/cpulayer.hpp"
#include "runtime/cpu/kernels/kernels.hpp"

#include "util/assert.hpp"

namespace deepworks {
namespace cpu {

class CPUReLU : public ICPULayer {
public:
    CPUReLU(deepworks::LayerInfo&& info);
    virtual void forward(const std::vector<deepworks::Tensor>& inputs,
                               std::vector<deepworks::Tensor>& outputs) override;
private:
    void validate(const std::vector<deepworks::Tensor>& inputs,
                  const std::vector<deepworks::Tensor>& outputs);
};

} // namespace cpu
} // namespace deepworks
