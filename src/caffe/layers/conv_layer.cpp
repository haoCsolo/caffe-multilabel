// Copyright 2014 BVLC and contributors.

#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"
#include "caffe/util/im2col.hpp"
#include "caffe/filler.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {

template <typename Dtype> Blob<Dtype> ConvolutionLayer<Dtype>::col_buffer_;
template <typename Dtype> Blob<Dtype> ConvolutionLayer<Dtype>::bias_buffer_;
template <typename Dtype> Blob<Dtype> ConvolutionLayer<Dtype>::trans_buffer_;

template <typename Dtype>
void ConvolutionLayer<Dtype>::SetUp(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  CHECK_EQ(bottom.size(), 1) << "Conv Layer takes a single blob as input.";
  CHECK_EQ(top->size(), 1) << "Conv Layer takes a single blob as output.";
  kernel_size_ = this->layer_param_.convolution_param().kernel_size();
  stride_ = this->layer_param_.convolution_param().stride();
  group_ = this->layer_param_.convolution_param().group();
  pad_ = this->layer_param_.convolution_param().pad();
  mem_group_size = this->layer_param_.convolution_param().mem_group_size();
  LOG(INFO)<< "Grouping im2col at size "<<mem_group_size;
  num_ = bottom[0]->num();
  channels_ = bottom[0]->channels();
  height_ = bottom[0]->height();
  width_ = bottom[0]->width();
  num_output_ = this->layer_param_.convolution_param().num_output();
  CHECK_GT(num_output_, 0);
  CHECK_EQ(channels_ % group_, 0);
  // The im2col result buffer would only hold one image at a time to avoid
  // overly large memory usage.
  int height_out = (height_ + 2 * pad_ - kernel_size_) / stride_ + 1;
  int width_out = (width_ + 2 * pad_ - kernel_size_) / stride_ + 1;
  if (mem_group_size*channels_ * kernel_size_ * kernel_size_*height_out*width_out> col_buffer_.count()){

		col_buffer_.Reshape(
		mem_group_size, channels_ * kernel_size_ * kernel_size_, height_out, width_out);
  }
  
  // Set the parameters
  CHECK_EQ(num_output_ % group_, 0)
      << "Number of output should be multiples of group.";
  bias_term_ = this->layer_param_.convolution_param().bias_term();
  // Figure out the dimensions for individual gemms.
  M_ = num_output_ / group_;
  K_ = channels_ * kernel_size_ * kernel_size_ / group_;
  N_ = height_out * width_out;
  (*top)[0]->Reshape(bottom[0]->num(), num_output_, height_out, width_out);
  
  if (num_output_*N_>bias_buffer_.count()){
	bias_buffer_.Reshape(
		1, 1,num_output_, N_);
  }
  if (mem_group_size*num_output_*N_ > trans_buffer_.count()){

	trans_buffer_.Reshape(
		mem_group_size,1,num_output_, N_);
  }

  //CUDA_CHECK(cudaMalloc(&row_sumer_, num_*sizeof(Dtype)));
  /*Dtype *temp = new Dtype[num_];
  for(int i = 0; i<num_; i++) temp[i] = (Dtype)1.;
  CUBLAS_CHECK(cublasSetVector(num_, sizeof(Dtype), temp, 1, row_sumer_, 1));*/

  // Check if we need to set up the weights
  if (this->blobs_.size() > 0) {
    LOG(INFO) << "Skipping parameter initialization";
  } else {
    if (bias_term_) {
      this->blobs_.resize(2);
    } else {
      this->blobs_.resize(1);
    }
    // Intialize the weight
    this->blobs_[0].reset(new Blob<Dtype>(
        num_output_, channels_ / group_, kernel_size_, kernel_size_));
    // fill the weights
    shared_ptr<Filler<Dtype> > weight_filler(GetFiller<Dtype>(
        this->layer_param_.convolution_param().weight_filler()));
    weight_filler->Fill(this->blobs_[0].get());
    // If necessary, intiialize and fill the bias term
    if (bias_term_) {
      this->blobs_[1].reset(new Blob<Dtype>(1, 1, 1, num_output_));
      shared_ptr<Filler<Dtype> > bias_filler(GetFiller<Dtype>(
          this->layer_param_.convolution_param().bias_filler()));
      bias_filler->Fill(this->blobs_[1].get());
    }
  }
  // Set up the bias filler
  if (bias_term_) {
    bias_multiplier_.reset(new SyncedMemory(N_ * sizeof(Dtype)));
    Dtype* bias_multiplier_data =
        reinterpret_cast<Dtype*>(bias_multiplier_->mutable_cpu_data());
    for (int i = 0; i < N_; ++i) {
        bias_multiplier_data[i] = 1.;
    }
  }

  row_sumer_.reset(new SyncedMemory(num_*sizeof(Dtype)));
  Dtype* row_sumer_data = reinterpret_cast<Dtype*>(row_sumer_->mutable_cpu_data());
  for (int i = 0; i < num_; i++){
	  row_sumer_data[i] = 1.;
  }

	// let's initialize the buffer we need for slave gpu device
  //Note: the memory is not allocated here, it's done when the slave device is really specified.
  slave_col_buffer_.reset(new SyncedMemory( mem_group_size * kernel_size_ * kernel_size_ * channels_ * N_ * sizeof(Dtype)));
  slave_weight_buffer_.reset(new SyncedMemory( num_output_ * kernel_size_ * kernel_size_ * channels_ * sizeof(Dtype)));
  slave_trans_buffer_.reset(new SyncedMemory( mem_group_size * num_output_ * N_ * sizeof(Dtype)));
  slave_bias_buffer_.reset(new SyncedMemory( num_output_ * N_ * sizeof(Dtype)));
  slave_row_summer.reset(new SyncedMemory( num_*sizeof(Dtype)));
  slave_bottom_buffer.reset(new SyncedMemory( num_ * channels_ * width_ * height_ * sizeof(Dtype)));
  slave_top_buffer.reset(new SyncedMemory( num_ * num_output_ * N_ * sizeof(Dtype)));

  slave_col_diff_buffer_.reset(new SyncedMemory( mem_group_size * kernel_size_ * kernel_size_ * channels_ * N_ * sizeof(Dtype)));
  slave_weight_diff_buffer_.reset(new SyncedMemory( num_output_ * kernel_size_ * kernel_size_ * channels_ * sizeof(Dtype)));
  master_weight_diff_buffer_.reset(new SyncedMemory( num_output_ * kernel_size_ * kernel_size_ * channels_ * sizeof(Dtype)));


}


template <typename Dtype>
Dtype ConvolutionLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      vector<Blob<Dtype>*>* top) {
  const Dtype* bottom_data = bottom[0]->cpu_data();
  Dtype* top_data = (*top)[0]->mutable_cpu_data();
  Dtype* col_data = col_buffer_.mutable_cpu_data();
  const Dtype* weight = this->blobs_[0]->cpu_data();
  int weight_offset = M_ * K_;
  int col_offset = K_ * N_;
  int top_offset = M_ * N_;
  for (int n = 0; n < num_; ++n) {
    // First, im2col
    im2col_cpu(bottom_data + bottom[0]->offset(n), channels_, height_,
                      width_, kernel_size_, pad_, stride_, col_data);
  //bu_im2col_gpu(bottom_data + bottom[0]->offset(n), channels_, height_,
 //                     width_, kernel_size_, pad_, stride_, col_data, 1);
    // Second, innerproduct with groups
    for (int g = 0; g < group_; ++g) {
      caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, M_, N_, K_,
        (Dtype)1., weight + weight_offset * g, col_data + col_offset * g,
        (Dtype)0., top_data + (*top)[0]->offset(n) + top_offset * g);
    }
    // third, add bias
    if (bias_term_) {
      caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, num_output_,
          N_, 1, (Dtype)1., this->blobs_[1]->cpu_data(),
          reinterpret_cast<const Dtype*>(bias_multiplier_->cpu_data()),
          (Dtype)1., top_data + (*top)[0]->offset(n));
    }
  }
  return Dtype(0.);
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const bool propagate_down, vector<Blob<Dtype>*>* bottom) {
  const Dtype* top_diff = top[0]->cpu_diff();
  const Dtype* weight = this->blobs_[0]->cpu_data();
  Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
  const Dtype* bottom_data = (*bottom)[0]->cpu_data();
  Dtype* bottom_diff = (*bottom)[0]->mutable_cpu_diff();
  Dtype* col_data = col_buffer_.mutable_cpu_data();
  Dtype* col_diff = col_buffer_.mutable_cpu_diff();
  // bias gradient if necessary
  Dtype* bias_diff = NULL;

  if (bias_term_) {
    bias_diff = this->blobs_[1]->mutable_cpu_diff();
    memset(bias_diff, 0, sizeof(Dtype) * this->blobs_[1]->count());
    for (int n = 0; n < num_; ++n) {
      caffe_cpu_gemv<Dtype>(CblasNoTrans, num_output_, N_,
          1., top_diff + top[0]->offset(n),
          reinterpret_cast<const Dtype*>(bias_multiplier_->cpu_data()), 1.,
          bias_diff);
    }
  }

  int weight_offset = M_ * K_;
  int col_offset = K_ * N_;
  int top_offset = M_ * N_;
  memset(weight_diff, 0, sizeof(Dtype) * this->blobs_[0]->count());
  for (int n = 0; n < num_; ++n) {
    // since we saved memory in the forward pass by not storing all col data,
    // we will need to recompute them.
    im2col_cpu(bottom_data + (*bottom)[0]->offset(n), channels_, height_,
                      width_, kernel_size_, pad_, stride_, col_data);
    // gradient w.r.t. weight. Note that we will accumulate diffs.
    for (int g = 0; g < group_; ++g) {
      caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasTrans, M_, K_, N_,
        (Dtype)1., top_diff + top[0]->offset(n) + top_offset * g,
        col_data + col_offset * g, (Dtype)1.,
        weight_diff + weight_offset * g);
    }
    // gradient w.r.t. bottom data, if necessary
    if (propagate_down) {
      for (int g = 0; g < group_; ++g) {
        caffe_cpu_gemm<Dtype>(CblasTrans, CblasNoTrans, K_, N_, M_,
          (Dtype)1., weight + weight_offset * g,
          top_diff + top[0]->offset(n) + top_offset * g,
          (Dtype)0., col_diff + col_offset * g);
      }
      // col2im back to the data
      col2im_cpu(col_diff, channels_, height_, width_, kernel_size_, pad_,
          stride_, bottom_diff + (*bottom)[0]->offset(n));
    }
  }
}

INSTANTIATE_CLASS(ConvolutionLayer);

}  // namespace caffe
