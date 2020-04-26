/******************************************************************************
 * Copyright 2020 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

/*
 * Copyright 2018-2019 Autoware Foundation. All rights reserved.
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
 */

/**
 * @file point_pillars.h
 * @brief Algorithm for PointPillars
 * @author Kosuke Murakami
 * @date 2019/02/26
 */

#pragma once

// headers in STL
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

// headers in TensorRT
#include "NvInfer.h"
#include "NvOnnxParser.h"

// headers in local files
#include "modules/perception/lidar/lib/detection/lidar_point_pillars/anchor_mask_cuda.h"
#include "modules/perception/lidar/lib/detection/lidar_point_pillars/common.h"
#include "modules/perception/lidar/lib/detection/lidar_point_pillars/postprocess_cuda.h"
#include "modules/perception/lidar/lib/detection/lidar_point_pillars/preprocess_points.h"
#include "modules/perception/lidar/lib/detection/lidar_point_pillars/preprocess_points_cuda.h"
#include "modules/perception/lidar/lib/detection/lidar_point_pillars/scatter_cuda.h"

namespace apollo {
namespace perception {
namespace lidar {

// Logger for TensorRT info/warning/errors
class Logger : public nvinfer1::ILogger {
 public:
  explicit Logger(Severity severity = Severity::kWARNING)
      : reportableSeverity(severity) {}

  void log(Severity severity, const char* msg) override {
    // suppress messages with severity enum value greater than the reportable
    if (severity > reportableSeverity) return;

    switch (severity) {
      case Severity::kINTERNAL_ERROR:
        std::cerr << "INTERNAL_ERROR: ";
        break;
      case Severity::kERROR:
        std::cerr << "ERROR: ";
        break;
      case Severity::kWARNING:
        std::cerr << "WARNING: ";
        break;
      case Severity::kINFO:
        std::cerr << "INFO: ";
        break;
      default:
        std::cerr << "UNKNOWN: ";
        break;
    }
    std::cerr << msg << std::endl;
  }

  Severity reportableSeverity;
};

class PointPillars {
 private:
  friend class TestClass;
  // initize in initializer list
  const bool kReproduceResultMode;
  const float kScoreThreshold;
  const float kNmsOverlapThreshold;
  const std::string kPfeOnnxFile;
  const std::string kRpnOnnxFile;
  const int kMaxNumPillars;
  const int kMaxNumPointsPerPillar;
  const int kPfeOutputSize;
  const int kGridXSize;
  const int kGridYSize;
  const int kGridZSize;
  const int kRpnInputSize;
  const int kNumAnchorXInds;
  const int kNumAnchorYInds;
  const int kNumAnchorRInds;
  const int kNumAnchor;
  const int kNumClass;
  const int kRpnBoxOutputSize;
  const int kRpnClsOutputSize;
  const int kRpnDirOutputSize;
  const float kPillarXSize;
  const float kPillarYSize;
  const float kPillarZSize;
  const float kMinXRange;
  const float kMinYRange;
  const float kMinZRange;
  const float kMaxXRange;
  const float kMaxYRange;
  const float kMaxZRange;
  const int kBatchSize;
  const int kNumIndsForScan;
  const int kNumThreads;
  const float kSensorHeight;
  const float kAnchorDxSize;
  const float kAnchorDySize;
  const float kAnchorDzSize;
  const int kNumBoxCorners;
  const int kNumOutputBoxFeature;
  // end initializer list

  int host_pillar_count_[1];

  float* anchors_px_;
  float* anchors_py_;
  float* anchors_pz_;
  float* anchors_dx_;
  float* anchors_dy_;
  float* anchors_dz_;
  float* anchors_ro_;

  float* box_anchors_min_x_;
  float* box_anchors_min_y_;
  float* box_anchors_max_x_;
  float* box_anchors_max_y_;

  // cuda malloc
  float* dev_pillar_x_in_coors_;
  float* dev_pillar_y_in_coors_;
  float* dev_pillar_z_in_coors_;
  float* dev_pillar_i_in_coors_;
  int* dev_pillar_count_histo_;

  int* dev_x_coors_;
  int* dev_y_coors_;
  float* dev_num_points_per_pillar_;
  int* dev_sparse_pillar_map_;
  int* dev_cumsum_along_x_;
  int* dev_cumsum_along_y_;

  float* dev_pillar_x_;
  float* dev_pillar_y_;
  float* dev_pillar_z_;
  float* dev_pillar_i_;

  float* dev_x_coors_for_sub_shaped_;
  float* dev_y_coors_for_sub_shaped_;
  float* dev_pillar_feature_mask_;

  float* dev_box_anchors_min_x_;
  float* dev_box_anchors_min_y_;
  float* dev_box_anchors_max_x_;
  float* dev_box_anchors_max_y_;
  int* dev_anchor_mask_;

  void* pfe_buffers_[9];
  void* rpn_buffers_[4];

  float* dev_scattered_feature_;

  float* dev_anchors_px_;
  float* dev_anchors_py_;
  float* dev_anchors_pz_;
  float* dev_anchors_dx_;
  float* dev_anchors_dy_;
  float* dev_anchors_dz_;
  float* dev_anchors_ro_;
  float* dev_filtered_box_;
  float* dev_filtered_score_;
  int* dev_filtered_label_;
  int* dev_filtered_dir_;
  float* dev_box_for_nms_;
  int* dev_filter_count_;

  std::unique_ptr<PreprocessPoints> preprocess_points_ptr_;
  std::unique_ptr<PreprocessPointsCuda> preprocess_points_cuda_ptr_;
  std::unique_ptr<AnchorMaskCuda> anchor_mask_cuda_ptr_;
  std::unique_ptr<ScatterCuda> scatter_cuda_ptr_;
  std::unique_ptr<PostprocessCuda> postprocess_cuda_ptr_;

  Logger g_logger_;
  nvinfer1::IExecutionContext* pfe_context_;
  nvinfer1::IExecutionContext* rpn_context_;
  nvinfer1::IRuntime* pfe_runtime_;
  nvinfer1::IRuntime* rpn_runtime_;
  nvinfer1::ICudaEngine* pfe_engine_;
  nvinfer1::ICudaEngine* rpn_engine_;

  /**
   * @brief Memory allocation for device memory
   * @details Called in the constructor
   */
  void DeviceMemoryMalloc();

  /**
   * @brief Initializing anchor
   * @details Called in the constructor
   */
  void InitAnchors();

  /**
   * @brief Initializing TensorRT instances
   * @details Called in the constructor
   */
  void InitTRT();

  /**
   * @brief Generate anchors
   * @param[in] anchors_px_ Represents x-coordinate values for a corresponding
   * anchor
   * @param[in] anchors_py_ Represents y-coordinate values for a corresponding
   * anchor
   * @param[in] anchors_pz_ Represents z-coordinate values for a corresponding
   * anchor
   * @param[in] anchors_dx_ Represents x-dimension values for a corresponding
   * anchor
   * @param[in] anchors_dy_ Represents y-dimension values for a corresponding
   * anchor
   * @param[in] anchors_dz_ Represents z-dimension values for a corresponding
   * anchor
   * @param[in] anchors_ro_ Represents rotation values for a corresponding
   * anchor
   * @details Generate anchors for each grid
   */
  void GenerateAnchors(float* anchors_px_, float* anchors_py_,
                       float* anchors_pz_, float* anchors_dx_,
                       float* anchors_dy_, float* anchors_dz_,
                       float* anchors_ro_);

  /**
   * @brief Convert ONNX to TensorRT model
   * @param[in] model_file ONNX model file path
   * @param[out] trt_model_stream TensorRT model made out of ONNX model
   * @details Load ONNX model, and convert it to TensorRT model
   */
  void OnnxToTRTModel(const std::string& model_file,
                      nvinfer1::IHostMemory** trt_model_stream);

  /**
   * @brief Preproces points
   * @param[in] in_points_array pointcloud array
   * @param[in] in_num_points Number of points
   * @details Call CPU or GPU preprocess
   */
  void Preprocess(const float* in_points_array, const int in_num_points);

  /**
   * @brief Preproces by CPU
   * @param[in] in_points_array pointcloud array
   * @param[in] in_num_points Number of points
   * @details The output from preprocessCPU is reproducible, while preprocessGPU
   * is not
   */
  void PreprocessCPU(const float* in_points_array, const int in_num_points);

  /**
   * @brief Preproces by GPU
   * @param[in] in_points_array pointcloud array
   * @param[in] in_num_points Number of points
   * @details Faster preprocess comapared with CPU preprocess
   */
  void PreprocessGPU(const float* in_points_array, const int in_num_points);

  /**
   * @brief Convert anchors to box form like min_x, min_y, max_x, max_y anchors
   * @param[in] anchors_px_ Represents x-coordinate value for a corresponding
   * anchor
   * @param[in] anchors_py_ Represents y-coordinate value for a corresponding
   * anchor
   * @param[in] anchors_dx_ Represents x-dimension value for a corresponding
   * anchor
   * @param[in] anchors_dy_ Represents y-dimension value for a corresponding
   * anchor
   * @param[in] box_anchors_min_x_ Represents minimum x value for a
   * correspomding anchor
   * @param[in] box_anchors_min_y_ Represents minimum y value for a
   * correspomding anchor
   * @param[in] box_anchors_max_x_ Represents maximum x value for a
   * correspomding anchor
   * @param[in] box_anchors_max_y_ Represents maximum y value for a
   * correspomding anchor
   * @details Make box anchors for nms
   */
  void ConvertAnchors2BoxAnchors(float* anchors_px_, float* anchors_py_,
                                 float* anchors_dx_, float* anchors_dy_,
                                 float* box_anchors_min_x_,
                                 float* box_anchors_min_y_,
                                 float* box_anchors_max_x_,
                                 float* box_anchors_max_y_);

  /**
   * @brief Memory allocation for anchors
   * @details Memory allocation for anchors
   */
  void PutAnchorsInDeviceMemory();

 public:
  /**
   * @brief Constructor
   * @param[in] reproduce_result_mode Boolean, if true, the output is
   * reproducible for the same input
   * @param[in] score_threshold Score threshold for filtering output
   * @param[in] nms_overlap_threshold IOU threshold for NMS
   * @param[in] pfe_onnx_file Pillar Feature Extractor ONNX file path
   * @param[in] rpn_onnx_file Region Proposal Network ONNX file path
   * @details Variables could be chaned through rosparam
   */
  PointPillars(const bool reproduce_result_mode,
               const int num_class,
               const float score_threshold,
               const float nms_overlap_threshold,
               const std::string pfe_onnx_file,
               const std::string rpn_onnx_file);
  ~PointPillars();

  /**
   * @brief Call PointPillars for the inference
   * @param[in] in_points_array Pointcloud array
   * @param[in] in_num_points Number of points
   * @param[out] out_detections Network output bounding box
   * @param[out] out_labels Network output object's label
   * @details This is an interface for the algorithm
   */
  void DoInference(const float* in_points_array,
                   const int in_num_points,
                   std::vector<float>* out_detections,
                   std::vector<int>* out_labels);
};

}  // namespace lidar
}  // namespace perception
}  // namespace apollo