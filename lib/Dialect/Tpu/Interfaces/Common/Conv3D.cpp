//===----------------------------------------------------------------------===//
//
// Copyright (C) 2022 Sophgo Technologies Inc.  All rights reserved.
//
// TPU-MLIR is licensed under the 2-Clause BSD License except for the
// third-party components.
//
//===----------------------------------------------------------------------===//

#include "tpu_mlir/Dialect/Tpu/IR/TpuOps.h"
#include "tpu_mlir/Support/Dnnl/Dnnl.h"
#include "tpu_mlir/Support/Float16.h"
#include "tpu_mlir/Support/Module.h"

#include "tpu_mlir/Support/MathUtils.h"



conv_attr_t tpu::Conv3DOp::parseParam() {
  conv_attr_t p = {0};
  auto i_s = getInput().getType().cast<RankedTensorType>().getShape();
  auto o_s = getOutput().getType().cast<RankedTensorType>().getShape();
  p.do_relu = getDoRelu();
  p.relu_limit = getReluLimit().convertToDouble();
  p.has_bias = getWithBias();
  p.n = i_s[0];
  p.ic = i_s[1];
  p.id = i_s[2];
  p.ih = i_s[3];
  p.iw = i_s[4];
  p.oc = o_s[1];
  p.od = o_s[2];
  p.oh = o_s[3];
  p.ow = o_s[4];
  auto kernel = module::getI64Array(getKernelShape());
  p.kd = kernel->at(0);
  p.kh = kernel->at(1);
  p.kw = kernel->at(2);
  auto pads_v = module::getI64Array(getPads());
  p.pdf = pads_v->at(0);
  p.pht = pads_v->at(1);
  p.pwl = pads_v->at(2);
  p.pdb = pads_v->at(3);
  p.phb = pads_v->at(4);
  p.pwr = pads_v->at(5);
  if (module::isUniformQuantized(getInput())) {
    p.pad_value = module::getUniformQuantizedType(getInput()).getZeroPoint();
  }
  if (module::isUniformQuantized(getFilter())) {
    p.kernel_zp = module::getUniformQuantizedType(getFilter()).getZeroPoint();
  }
  auto strides_v = module::getI64Array(getStrides());
  p.sd = strides_v->at(0);
  p.sh = strides_v->at(1);
  p.sw = strides_v->at(2);
  auto dilation = module::getI64Array(getDilations(), 3, 1);
  p.dd = dilation->at(0);
  p.dh = dilation->at(1);
  p.dw = dilation->at(2);
  auto ins = module::getI64Array(getInserts(), 3, 0);
  p.ins_d = ins->at(0);
  p.ins_h = ins->at(1);
  p.ins_w = ins->at(2);
  assert(p.ins_d == 0 && p.ins_h == 0 && p.ins_w == 0);
  p.groups = getGroup();
  p.is_dw = (p.oc == p.ic && p.oc == p.groups && p.groups > 1);
  return p;
}

LogicalResult tpu::Conv3DOp::init(InferenceParameter &p) {
  auto conv = new Conv();
  auto attr = parseParam();

  conv->setup(p.inputs[0], p.inputs[1], p.inputs[2], p.outputs[0], attr);
  p.handle = (void *)conv;
  return success();
}

void tpu::Conv3DOp::deinit(InferenceParameter &p) {
  if (p.handle != nullptr) {
    auto conv = (Conv *)p.handle;
    delete conv;
    p.handle = nullptr;
  }
}

LogicalResult tpu::Conv3DOp::inference(InferenceParameter &p) {
  if (p.handle == nullptr) {
    return failure();
  }
  auto conv = (Conv *)p.handle;
  conv->run();
  // requant
  auto out_type = module::getStorageType(getOutput());
  auto num_elem = module::getNumElements(getOutput());
  if (out_type.isa<FloatType>()) {
    if (out_type.isBF16()) {
      BF16(p.outputs[0], p.outputs[0], num_elem);
    } else if (out_type.isF16()) {
      F16(p.outputs[0], p.outputs[0], num_elem);
    }
  }

  return success();
}

LogicalResult tpu::Conv3DOp::BackwardH(int64_t &in_idx, int64_t &in_slice,
                                       int64_t out_idx, int64_t out_slice) {
  auto attr = parseParam();
  int kh_with_dh = (attr.kh - 1) * attr.dh + 1;
  in_slice = (out_slice - 1) * attr.sh +
             (kh_with_dh >= attr.sh ? kh_with_dh : attr.sh);
  in_idx = out_idx * attr.sh - attr.pht;
  bool is_last = (out_idx + out_slice == attr.oh);
  LocalGenInterface::fixSlice(in_idx, in_slice, attr.ih, is_last);
  return success();
}

void tpu::Conv3DOp::assign_sec_info(int64_t n_step, int64_t h_step,
                                    group_type_t group_type,
                                    local_sec_info_t &sec_info) {
  memset(&sec_info, 0, sizeof(local_sec_info_t));
  sec_info.group_type = group_type;

  auto attr = parseParam();
  auto gi = getGroupInfo(n_step, h_step);
  auto in_gi = LocalGenInterface::getGroupInfo(getInput(), n_step, h_step);
  sec_info.n_slice = in_gi.n_slice;
  sec_info.d_slice = 1;
  sec_info.h_slice = in_gi.h_slice;
  sec_info.h_idx = in_gi.h_idx;
  sec_info.is_h_split = !(in_gi.h_idx == 0 && in_gi.h_slice == attr.ih);
  sec_info.w_slice = attr.iw;
  sec_info.out_n_slice = gi.n_slice;
  sec_info.out_h_idx = gi.h_idx;
  sec_info.out_h_slice = gi.h_slice;
  sec_info.out_w_slice = attr.ow;
}

LogicalResult tpu::Conv3DOp::LocalGenSupport() {
  if (module::isBM1684X()) {
      return success();
  } else {
    return failure();
  }
}

mlir::Type tpu::Conv3DOp::type_verify(uint64_t opd_idx, TypeCastMode &mode) {
  return type_verify_case_i32(getOperation(), opd_idx, mode);
}

LogicalResult tpu::Conv3DOp::DynBackwardH(int64_t &in_idx, int64_t &in_slice,
                                       int64_t out_idx, int64_t out_slice) {
  auto attr = parseParam();
  int kh_with_dh = (attr.kh - 1) * attr.dh + 1;
  in_slice = (out_slice - 1) * attr.sh +
             (kh_with_dh >= attr.sh ? kh_with_dh : attr.sh);
  in_idx = out_idx * attr.sh - attr.pht;
  return success();
}

LogicalResult tpu::Conv3DOp::DynBackwardKh(int64_t &in_kh, int64_t out_kh) {
  auto attr = parseParam();
  int kh_with_dh = (attr.kh - 1) * attr.dh + 1;
  in_kh = (out_kh - 1) * attr.sh + (kh_with_dh >= attr.sh ? kh_with_dh : attr.sh);
  return success();
}


LogicalResult tpu::Conv3DOp::DynBackwardStrideH(int64_t &in_stride_h, int64_t out_stride_h) {
  auto attr = parseParam();
  in_stride_h = out_stride_h * attr.sh;
  return success();
}

LogicalResult tpu::Conv3DOp::DynBackwardUpPadH(int64_t &in_up_pad_h, int64_t out_up_pad_h) {
  auto attr = parseParam();
  in_up_pad_h = out_up_pad_h * attr.sh + attr.pht;
  return success();
}

LogicalResult tpu::Conv3DOp::DynBackwardDownPadH(int64_t &in_down_pad_h, int64_t out_down_pad_h) {
  auto attr = parseParam();
  in_down_pad_h = out_down_pad_h * attr.sh + attr.phb;
  return success();
}

int64_t tpu::Conv3DOp::DynForwardHeight(int64_t in_height) {
  //Todo
  return in_height;
}
