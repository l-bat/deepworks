#include "runtime/cpu/kernels/kernels.hpp"
#include <deepworks/initializers.hpp>

enum Input  {N, C, H, W};
enum Kernel {KH, KW};

void deepworks::CPULinearForward(ConstMatrix X, ConstMatrix W, Matrix result) {
    result = X * W.transpose();
}

void deepworks::CPULinearAddBias(ConstMatrix X, ConstVector b, Matrix result) {
    result = X.rowwise() + b;
}

void deepworks::CPULinearInputGrad(ConstMatrix dx,
                                   ConstMatrix W,
                                   Matrix grad_input) {
    grad_input = dx * W;
}

void deepworks::CPULinearWeightGrad(ConstMatrix input, ConstMatrix dx, Matrix dW) {
    dW = dx.transpose() * input;
}

void deepworks::CPULinearBiasGrad(ConstMatrix dx, Vector db) {
    db = dx.colwise().sum();
}

void deepworks::CPUSoftmaxForward(ConstMatrix X, Matrix result) {
    Eigen::MatrixXf exp_x = (X.colwise() - X.rowwise().maxCoeff()).array().exp();
    result = exp_x.array().colwise() / exp_x.rowwise().sum().array();
}

void deepworks::CPUSoftmaxInputGrad(ConstMatrix output, ConstMatrix grad_output, Matrix grad_input) {
    Eigen::VectorXf k = (grad_output.array() * output.array()).rowwise().sum();
    grad_input = output.array() * (grad_output.colwise() - k).array();
}

void deepworks::CPUReLUForward(ConstVector X, Vector result) {
    result = (X.array() > 0.f).select(X, 0.f);
}

void deepworks::CPUReLUInputGrad(ConstVector input,
                                 ConstVector grad_output,
                                 Vector grad_input) {
    grad_input = (input.array() > 0.f).select(grad_output, 0.f);
}

void deepworks::CPUELUForward(ConstVector X, Vector result, float alpha) {
    result = (X.array() < 0.f).select(((X.array().exp()).array() - 1.0f).array() * alpha, X);
}

void deepworks::CPUELUInputGrad(ConstVector input, ConstVector grad_output, Vector grad_input, float alpha) {
    grad_input = grad_output.array() *
            (input.array() < 0.f).select((input.array().exp()).array() * alpha, 1.f).array();
}

void deepworks::CPUConvolutionalForward(const Tensor& input,
                                        const Tensor& weights,
                                        const Tensor& bias,
                                        Tensor& output,
                                        Tensor& im2col_buf,
                                        const std::array<int, 2>& kernel,
                                        const std::array<int, 2>& padding,
                                        const std::array<int, 2>& stride) {
    auto input_shape = input.shape();
    int batch = input_shape[Input::N];
    int c_in  = input_shape[Input::C];
    int c_out = weights.shape()[0];
    int h_out = output.shape()[Input::H];
    int w_out = output.shape()[Input::W];

    int rows = c_in * kernel[Kernel::KH] * kernel[Kernel::KW];
    int cols = h_out * w_out;

    int input_offset  = c_in * input_shape[Input::H] * input_shape[Input::W];
    int output_offset = c_out * h_out * w_out;

    ConstMatrix W{weights.data(), c_out, rows};
    ConstColVector bias_vec{bias.data(), c_out};
    for (size_t b = 0; b < batch; b++) {
        Tensor src_tensor(input_shape, input.data() + b * input_offset);
        Tensor col_plane({rows, cols}, im2col_buf.data() + (b * rows * cols));
        im2col(src_tensor, col_plane, kernel, padding, stride);

        ConstMatrix X{col_plane.data(), rows, cols};
        Matrix result{output.data() + b * output_offset, c_out, cols};
        result = W * X;
        result = result.colwise() + bias_vec;
    }
}

void deepworks::CPUConvolutionalInputGrad(const Tensor& grad_output,
                                         const Tensor& weights,
                                         const Tensor& im2col_buf,
                                         Tensor& grad_input,
                                         const std::array<int, 2>& kernel,
                                         const std::array<int, 2>& padding,
                                         const std::array<int, 2>& stride) {
    auto output_shape = grad_output.shape();
    int batch = output_shape[Input::N];
    int c_out = output_shape[Input::C];
    int h_out = output_shape[Input::H];
    int w_out = output_shape[Input::W];

    auto input_shape = grad_input.shape();
    int c_in  = input_shape[Input::C];
    int h_in  = input_shape[Input::H];
    int w_in  = input_shape[Input::W];

    int rows = c_in * kernel[Kernel::KH] * kernel[Kernel::KW];
    int cols = h_out * w_out;
    int output_offset = c_out * h_out * w_out;

    ConstMatrix weights_mat{weights.data(), c_out, rows};
    Tensor grad_im2col_buf;
    grad_im2col_buf.allocate({rows, cols});
    for (size_t b = 0; b < batch; b++) {
        ConstMatrix grad_output_mat{grad_output.data() + b * output_offset, c_out, cols};

        Matrix grad_col_mat{grad_im2col_buf.data(), rows, cols};
        grad_col_mat = weights_mat.transpose() * grad_output_mat;

        Tensor grad_input_plane({c_in, h_in * w_in}, grad_input.data() + c_in * h_in * w_in);
        col2im(grad_im2col_buf, grad_input_plane, kernel, padding, stride);
    }
}

void deepworks::CPUConvolutionalWeightsGrad(const Tensor& grad_output,
                                            const Tensor& im2col_buf,
                                            Tensor& grad_weights) {
    auto output_shape = grad_output.shape();
    int batch = output_shape[Input::N];
    int c_out = output_shape[Input::C];
    int h_out = output_shape[Input::H];
    int w_out = output_shape[Input::W];

    auto weights_shape = grad_weights.shape();
    int rows = weights_shape[Input::C] * weights_shape[Input::H] * weights_shape[Input::W];
    int cols = h_out * w_out;

    initializer::zeros(grad_weights); // zero grad
    Matrix grad_weights_mat{grad_weights.data(), c_out, rows};
    for (size_t b = 0; b < batch; b++) {
        ConstMatrix grad_output_mat{grad_output.data() + b * c_out * cols, c_out, cols};
        ConstMatrix col_mat{im2col_buf.data() + (b * rows * cols), rows, cols};
        grad_weights_mat += grad_output_mat * col_mat.transpose();
    }
}

void deepworks::CPUConvolutionalBiasGrad(const Tensor& grad_output, Tensor& grad_bias) {
    auto output_shape = grad_output.shape();
    int batch = output_shape[Input::N];
    int c_out = output_shape[Input::C];
    int h_out = output_shape[Input::H];
    int w_out = output_shape[Input::W];
    int output_offset = c_out * h_out * w_out;

    initializer::zeros(grad_bias); // zero grad
    Vector grad_bias_vec{grad_bias.data(), c_out};
    for (size_t b = 0; b < batch; b++) {
        ConstMatrix grad_output_mat{grad_output.data() + b * output_offset, c_out, h_out * w_out};
        grad_bias_vec += grad_output_mat.rowwise().sum();
    }
}

void deepworks::CPUMaxPoolingForward(const Tensor& input,
                                     Tensor& max_indices,
                                     Tensor& output,
                                     const std::array<int, 2>& kernel,
                                     const std::array<int, 2>& padding,
                                     const std::array<int, 2>& stride) {
    auto input_shape = input.shape();
    int batch = input_shape[Input::N];
    int c = input_shape[Input::C];
    int h_out = output.shape()[Input::H];
    int w_out = output.shape()[Input::W];
    int input_offset = input_shape[Input::H] * input_shape[Input::W];

    int rows = input_shape[Input::C] * kernel[Kernel::KH] * kernel[Kernel::KW];
    int cols = h_out * w_out;

    Tensor col_buff;
    col_buff.allocate({rows, cols});

    ConstMatrix::Index max_col;
    auto dst     = output.data();
    auto indices = max_indices.data();
    Shape plane_shape{batch * c, 1, input_shape[Input::H], input_shape[Input::W]};
    for (size_t i = 0; i < batch * c; i++) {
        Tensor src_plane(plane_shape, input.data() + i * input_offset);
        im2col(src_plane, col_buff, kernel, padding, stride);

        ConstMatrix col_mat{col_buff.data(), rows, cols};
        for (int j = 0; j < cols; j++) {
            dst[i * cols + j] = col_mat.col(j).maxCoeff(&max_col);
            indices[i * cols + j] = max_col;
        }
    }

    Matrix result{dst, h_out * w_out, batch * c};
    // Why I should create a mat? result = result.transpose() -> error
    auto mat = result.transpose();
}

void deepworks::CPUMaxPoolingInputGrad(const Tensor& grad_output,
                                       const Tensor& max_indices,
                                       Tensor& grad_input,
                                       const std::array<int, 2>& kernel,
                                       const std::array<int, 2>& padding,
                                       const std::array<int, 2>& stride) {
    auto input_shape = grad_input.shape();
    int batch = input_shape[Input::N];
    int c = input_shape[Input::C];
    int h_out = grad_output.shape()[Input::H];
    int w_out = grad_output.shape()[Input::W];

    int h_in = grad_input.shape()[Input::H];
    int w_in = grad_input.shape()[Input::W];

    int rows = input_shape[Input::C] * kernel[Kernel::KH] * kernel[Kernel::KW];
    int cols = h_out * w_out;

    Tensor col_buff;
    col_buff.allocate({rows, cols});

    std::vector<float> grad_output_copy(grad_output.data(), grad_output.data() + grad_output.total());
    Matrix grad{grad_output_copy.data(), batch * c, h_out * w_out};
    auto grad_output_tr = grad.transpose();

    auto out_grad_ptr = grad_output_copy.data();
    auto indices_ptr  = max_indices.data();
    auto col_buff_ptr = col_buff.data();

    Shape img_shape{1, 1, input_shape[Input::H], input_shape[Input::W]};
    for (size_t i = 0; i < batch * c; i++) {
        initializer::zeros(col_buff);
        // check range [0, cols) or [0, rows)
        for (int row = 0; row < rows; row++) {
            int col = indices_ptr[i * cols + row];
            col_buff_ptr[row * cols + col] = out_grad_ptr[i * cols + row];
        }

        Tensor grad_input_plane({h_in, w_in}, grad_input.data() + h_in * w_in);
        col2im(col_buff, grad_input_plane, kernel, padding, stride);
    }
}

void deepworks::CPULog(ConstMatrix X, Matrix LogX) {
    LogX.array() = X.array().log();
}

std::vector<int> deepworks::MatchTargetTo1dMatrix(ConstVector target, int batch_size, int n_classes) {
    std::vector<int> slice(batch_size);
    for (int i = 0; i < batch_size; ++i) {
        slice[i] = static_cast<int>(target(0, i)) + n_classes * i;
    }
    return slice;
}

float deepworks::CPUNLLLoss(Matrix predictions, ConstVector target) {
    int batch_size = predictions.rows();
    int n_classes = predictions.cols();

    Vector X_1d(predictions.data(), predictions.size());

    std::vector<int> slice = MatchTargetTo1dMatrix(target, batch_size, n_classes);

    float loss = -X_1d(0, slice).array().sum() / static_cast<float>(batch_size);
    return loss;
}

void deepworks::CPUBatchNorm1DForward(ConstMatrix input, Matrix output,
                                      Matrix input_centered, Vector std,
                                      Vector running_mean, Vector running_var,
                                      bool is_training, float eps, float alpha,
                                      ConstVector gamma, ConstVector beta) {

    if (is_training) {
        auto input_mean = input.colwise().mean();
        input_centered = input.rowwise() - input_mean;

        auto input_var = input_centered.cwiseAbs2().colwise().mean();
        std = (input_var.array() + eps).cwiseSqrt();

        running_mean = running_mean * alpha + input_mean * (1 - alpha);
        running_var = running_var * alpha + input_var * (1 - alpha);
    } else {
        input_centered = input.rowwise() - running_mean;
        std = (running_var.array() + eps).cwiseSqrt();
    }

    output = (input_centered.array().rowwise() * (gamma.array() / std.array())).array().rowwise() + beta.array();
}

void deepworks::CPUBatchNorm1DInputGrad(ConstMatrix input_centered, ConstVector std,
                                        ConstMatrix grad_output, Matrix grad_input,
                                        ConstVector gamma) {
    auto batch_size = input_centered.outerSize();

    auto grad_x_norm = grad_output.array().rowwise() * gamma.array();

    auto grad_std = (((input_centered.array() * grad_x_norm.array()).array().rowwise() / std.cwiseAbs2().array())
                    .colwise().sum()).array() * (-1.0);

    auto grad_var = grad_std.array() / (std.array() * 2.0).array();

    auto grad_x_centered = (grad_x_norm.array().rowwise() / std.array()) +
                           ((input_centered.array().rowwise() * grad_var.array()).array() * (2.0 / batch_size));

    auto grad_mu = grad_x_centered.colwise().sum();

    grad_input = grad_x_centered.rowwise() - (grad_mu.array() / batch_size).array();
}

void deepworks::CPUBatchNorm1DParamGrad(ConstMatrix input_centered, ConstVector std, ConstMatrix grad_output,
                                        Vector gamma_grad, Vector beta_grad) {

    beta_grad = grad_output.colwise().sum();
    gamma_grad = ((input_centered.array().rowwise() / std.array()).array() * grad_output.array()).colwise().sum();
}

void deepworks::im2col(const Tensor& image,
                       Tensor& col_buff,
                       const std::array<int, 2>& kernel,
                       const std::array<int, 2>& padding,
                       const std::array<int, 2>& stride,
                       const std::array<int, 2>& dilation) {
    auto image_shape = image.shape();
    auto src = image.data();
    auto dst = col_buff.data();
    int ch = image_shape[Input::C];
    int h = image_shape[Input::H];
    int w = image_shape[Input::W];

    int kernel_h = kernel[Kernel::KH];
    int kernel_w = kernel[Kernel::KW];

    int pad_h = padding[Kernel::KH];
    int pad_w = padding[Kernel::KW];

    int stride_h = stride[Kernel::KH];
    int stride_w = stride[Kernel::KW];

    int dilation_h = dilation[Kernel::KH];
    int dilation_w = dilation[Kernel::KW];

    int out_h = (h + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h + 1;
    int out_w = (w + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w + 1;

    int plane_size = h * w;
    for (int c = ch; c--; src+=plane_size) {
        for (int k_h = 0; k_h < kernel_h; k_h++) {
            for (int k_w = 0; k_w < kernel_w; k_w++) {
                int input_row = k_h * dilation_h - pad_h;
                for (int out_r = out_h; out_r; out_r--) {
                    if (!(input_row >= 0 && input_row < h)) {
                        for (int out_col = out_w; out_col; out_col--) {
                            *(dst++) = 0;
                        }
                    } else {
                        int input_col = k_w * dilation_w - pad_w;
                        for (int out_col = out_w; out_col; out_col--) {
                            *(dst++) = (input_col >= 0 && input_col < w) ? src[input_row * w + input_col] : 0;
                            input_col += stride_w;
                        }
                    }
                    input_row += stride_h;
                }
            }
        }
    }
}

void deepworks::col2im(const Tensor& col_buff,
                       Tensor& image,
                       const std::array<int, 2>& kernel,
                       const std::array<int, 2>& padding,
                       const std::array<int, 2>& stride,
                       const std::array<int, 2>& dilation) {
    auto image_shape = image.shape();
    auto src = col_buff.data();
    auto dst = image.data();

    int ch = image_shape[Input::C];
    int h = image_shape[Input::H];
    int w = image_shape[Input::W];

    int kernel_h = kernel[Kernel::KH];
    int kernel_w = kernel[Kernel::KW];

    int pad_h = padding[Kernel::KH];
    int pad_w = padding[Kernel::KW];

    int stride_h = stride[Kernel::KH];
    int stride_w = stride[Kernel::KW];

    int dilation_h = dilation[Kernel::KH];
    int dilation_w = dilation[Kernel::KW];

    int out_h = (h + 2 * pad_h - (dilation_h * (kernel_h - 1) + 1)) / stride_h + 1;
    int out_w = (w + 2 * pad_w - (dilation_w * (kernel_w - 1) + 1)) / stride_w + 1;
    int plane_size = h * w;

    std::fill(&dst[0], &dst[0] + ch * h * w, 0.0f);
    for (int c = ch; c--; dst += plane_size) {
        for (int k_h = 0; k_h < kernel_h; k_h++) {
            for (int k_w = 0; k_w < kernel_w; k_w++) {
                int input_row = k_h * dilation_h - pad_h;
                for (int out_r = out_h; out_r; out_r--) {
                    if (!(input_row >= 0 && input_row < h)) {
                        src += out_w;
                    } else {
                        int input_col = k_w * dilation_w - pad_w;
                        for (int out_col = out_w; out_col; out_col--) {
                            if (input_col >= 0 && input_col < w) {
                                dst[input_row * w + input_col] += *src;
                            }
                            src++;
                            input_col += stride_w;
                        }
                    }
                    input_row += stride_h;
                }
            }
        }
    }
}
