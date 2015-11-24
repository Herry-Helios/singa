/************************************************************
*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
*
*************************************************************/

#ifndef SINGA_NEURALNET_NEURON_LAYER_H_
#define SINGA_NEURALNET_NEURON_LAYER_H_

#include <vector>
#include "singa/neuralnet/layer.h"
#include "singa/proto/job.pb.h"
namespace singa {

/* Activation layer applies following activations,
 * - "relu",    @f$ f(x) = max(0, x)@f$
 * - "sigmoid", @f$ f(x)=1/(1+exp(-x)) @f$
 * - "tanh",    @f$ f(x) = tanh(x) @f$
 * - "stanh",   scaled tanh @f$f(x)=1.7159047 * tanh(0.66666667 * x)@f$, valid
 *   only for CPU training.
 * It may share data and grad with its (single) source layer depending on
 * the share_srclayer_blob configuration field.
 */
class ActivationLayer : public NeuronLayer {
 public:
  void Setup(const LayerProto& conf, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  bool share_with_srclayer = false;
  std::string method_;
};

/**
 * Convolution layer.
 * Currently using Mshadow to do convolution operations. TODO(wangwei) remove
 * dependency on Mshadow and using im2col from Caffe to implement this for CPU
 * version. For GPU version, there is class CudnnConvLayer.
 */
class ConvolutionLayer : public NeuronLayer {
 public:
  ~ConvolutionLayer();

  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
  const std::vector<Param*> GetParams() const override {
    std::vector<Param*> params{weight_, bias_};
    return params;
  }
  ConnectionType src_neuron_connection(int k) const  override {
    // CHECK_LT(k, srclayers_.size());
    return kOneToAll;
  }

 protected:
  int kernel_x_, pad_x_,  stride_x_;
  int kernel_y_, pad_y_,  stride_y_;
  int batchsize_,  channels_, height_, width_;
  int col_height_, col_width_, conv_height_, conv_width_, num_filters_;
  Param* weight_ = nullptr, *bias_ = nullptr;
  Blob<float> col_data_, col_grad_;
};

/**
 * Implement convolution operations using im2col from Caffe.
 */
class CConvolutionLayer : public ConvolutionLayer {
 public:
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
};

/**
 * Layer that drops out some neurons randomly according to a user defined drop
 * ratio (default is 0.5). It helps reduce overfitting.
 */
class DropoutLayer : public NeuronLayer {
 public:
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
 protected:
  // drop probability
  float pdrop_;
  /* record which neuron is dropped, required for back propagating gradients,
   * if mask[i]=0, then the i-th neuron is dropped.
   */
  Blob<float> mask_;
};

/**
 * Layer that applys linear transformations as
 * @f$ h = v*W+b @f$, where W and b are weight matrix and bias vector.
 */
class InnerProductLayer : public NeuronLayer {
 public:
  ~InnerProductLayer();
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
  const std::vector<Param*> GetParams() const override {
    std::vector<Param*> params{weight_, bias_};
    return params;
  }

 private:
  int batchsize_;
  int vdim_, hdim_;
  bool transpose_;
  Param *weight_, *bias_;
};

/**
 * Local Response Normalization edge
 *
 * @f$ b_i=a_i/x_i^beta @f$
 * @f$x_i=knorm+alpha*\sum_{j=max(0,i-n/2)}^{min(N,i+n/2)}(a_j)^2 @f$
 * n is size of local response area.
 * @f$a_i@f$, the activation (after ReLU) of a neuron convolved with the i-th kernel.
 * @f$b_i@f$, the neuron after normalization, N is the total num of kernels
 */
class LRNLayer : public NeuronLayer {
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  //!< shape of the feature blob of the src layer
  int batchsize_, channels_, height_, width_;
  //!< size local response (neighbor) area
  int lsize_;
  //!< hyper-parameter
  float alpha_, beta_, knorm_;
  Blob<float> norm_;
};

/**
 * Layer that applies the pooling operation.
 * TODO(wangwei) remove dependenices on mshadow
 */
class PoolingLayer : public NeuronLayer {
 public:
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  int kernel_x_, pad_x_, stride_x_;
  int kernel_y_, pad_y_, stride_y_;
  int batchsize_, channels_, height_, width_, pooled_height_, pooled_width_;
  PoolingProto_PoolMethod pool_;
};
/**
 * Use book-keeping for BP following Caffe's pooling implementation
 */
class CPoolingLayer : public PoolingLayer {
 public:
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers);
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 private:
  Blob<float> mask_;
};

/**
 * @deprecated {please use ActivationLayer}
 */
class ReLULayer : public NeuronLayer {
 public:
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
};

/**
 * Softmax layer applies softmax transformation to features from source layers.
 * The feature blob of this layer is of shape (batchsize,
 * num_softmax_per_instance, count_per_softmax), where num_softmax_per_instance
 * is controled by users (default is 1),
 * @f$ count_per_softmax = count / batchsize / num_softmax_per_instance @f$.
 * The softmax is conducted over count_per_softmax elements each time.
  */
class SoftmaxLayer : public NeuronLayer {
 public:
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
  /**
   * This layer is not recommendeded for partition because it requires the whole
   * src layer for normalization.
   */
  ConnectionType src_neuron_connection(int k) const override {
    // CHECK_LT(k, srclayers_.size());
    return kOneToAll;
  }
 protected:
  int batchsize_;
  //!< set by users (default is 1)
  int num_softmax_per_instance_;
  //!< size of the softmax area/length
  int count_per_softmax_;
};
/**
 * @deprecated {please use ActivationLayer}
 *
 * This layer apply Sigmoid function to neuron activations.
 * f(x)=1/(1+exp(-x))
 * f'(x)=f(x)*(1-f(x))
 */
class SigmoidLayer: public Layer {
 public:
  using Layer::ComputeFeature;
  using Layer::ComputeGradient;

  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
};

/**
 * @deprecated {please use ActivationLayer}
 * This layer apply scaled Tanh function to neuron activations.
 * f(x)=1.7159047  tanh(0.66666667 x)
 */
class STanhLayer : public NeuronLayer {
 public:
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
};

/*************** Layers implemented using cudnn v3 ***************/
#ifdef USE_CUDNN
#include <cudnn.h>
#define CHECK_CUDNN(x) CHECK_EQ(x, CUDNN_STATUS_SUCCESS)

class CudnnLayer : virtual public NeuronLayer {
 public:
  ~CudnnLayer() {
    CHECK_CUDNN(cudnnDestroyTensorDescriptor(src_desc_));
    CHECK_CUDNN(cudnnDestroyTensorDescriptor(my_desc_));
    CHECK_CUDNN(cudnnDestroy(handle_));
  }
  void virtual InitCudnn() {
    CHECK(!has_init_cudnn_);
    CHECK_CUDNN(cudnnCreate(&handle_));
    has_init_cudnn_ = true;
  }
 protected:
  bool has_init_cudnn_ = false;
  cudnnHandle_t handle_;
  cudnnTensorDescriptor_t src_desc_, my_desc_;
};

/**
 * Activation layer implemented using cudnn v3.
 * Activation methods including
 * - "sigmoid"
 * - "tanh"
 * - "relu"
 */
class CudnnActivationLayer : public ActivationLayer, public CudnnLayer {
 public:
  void InitCudnn() override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  cudnnActivationMode_t mode_;
};

/**
 * Convolution layer implemeneted using cudnn (v3 version backward functions).
 */
class CudnnConvLayer : public ConvolutionLayer, public CudnnLayer {
 public:
  ~CudnnConvLayer();
  void InitCudnn() override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  cudnnTensorDescriptor_t bias_desc_;
  cudnnFilterDescriptor_t filter_desc_;
  cudnnConvolutionDescriptor_t conv_desc_;
  cudnnConvolutionFwdAlgo_t fp_alg_;
  cudnnConvolutionBwdFilterAlgo_t bp_filter_alg_;
  cudnnConvolutionBwdDataAlgo_t bp_data_alg_;
  size_t workspace_byte_limit_, workspace_count_;
};

class CudnnLRNLayer : public LRNLayer, public CudnnLayer {
 public:
  ~CudnnLRNLayer();
  void InitCudnn() override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  cudnnLRNMode_t mode_;
  cudnnLRNDescriptor_t norm_desc_;
};
/**
 * Pooling layer implemented using cudnn.
 */
class CudnnPoolLayer : public PoolingLayer, public CudnnLayer {
 public:
  ~CudnnPoolLayer();
  void InitCudnn() override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 protected:
  cudnnPoolingDescriptor_t pool_desc_;
};

/**
 * Cudnn Softmax layer.
 */
class CudnnSoftmaxLayer : public SoftmaxLayer, public CudnnLayer {
 public:
  void InitCudnn() override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;
};
#endif  // USE_CUDNN

/******************** RBM layers *****************/
/**
 * Base layer for RBM models.
 */
class RBMLayer: virtual public NeuronLayer {
 public:
  virtual ~RBMLayer() {}
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  const Blob<float>& neg_data(const Layer* layer) {
    return neg_data_;
  }
  Blob<float>* mutable_neg_data(const Layer* layer) {
    return &neg_data_;
  }
  const std::vector<Param*> GetParams() const override {
    std::vector<Param*> params{weight_, bias_};
    return params;
  }
  virtual Blob<float>* Sample(int flat);

 protected:
  //! if ture, sampling according to guassian distribution
  bool gaussian_;
  //! dimension of the hidden layer
  int hdim_;
  //! dimension of the visible layer
  int vdim_;
  int batchsize_;
  bool first_gibbs_;
  Param* weight_, *bias_;

  Blob<float> neg_data_;
  Blob<float> neg_sample_;
  Blob<float> sample_;
};

/**
 * RBM visible layer
 */
class RBMVisLayer: public RBMLayer, public LossLayer {
 public:
  ~RBMVisLayer();
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 private:
  RBMLayer* hid_layer_;
  Layer* input_layer_;
};
/**
 * RBM hidden layer
 */
class RBMHidLayer: public RBMLayer {
 public:
  ~RBMHidLayer();
  void Setup(const LayerProto& proto, const vector<Layer*>& srclayers) override;
  void ComputeFeature(int flag, const vector<Layer*>& srclayers) override;
  void ComputeGradient(int flag, const vector<Layer*>& srclayers) override;

 private:
  RBMLayer *vis_layer_;
};

}  // namespace singa
#endif  // SINGA_NEURALNET_NEURON_LAYER_H_
