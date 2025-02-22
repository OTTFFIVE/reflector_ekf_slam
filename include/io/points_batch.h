
#ifndef IO_POINTS_BATCH_H_
#define IO_POINTS_BATCH_H_

#include <array>
#include <cstdint>
#include <unordered_set>
#include <vector>

#include "Eigen/Core"
#include "common/time.h"
#include "io/color.h"

namespace io
{

// A number of points, captured around the same 'time' and by a
// sensor at the same 'origin'.
struct PointsBatch
{
  PointsBatch()
  {
    origin = Eigen::Vector3f::Zero();
    trajectory_id = 0;
  }

  // Time at which the first point of this batch has been acquired.
  common::Time start_time;

  // Origin of the data, i.e. the location of the sensor in the world at
  // 'time'.
  Eigen::Vector3f origin;

  // Sensor that generated this data's 'frame_id' or empty if this information
  // is unknown.
  std::string frame_id;

  // Trajectory ID that produced this point.
  int trajectory_id;

  // Geometry of the points in the map frame.
  std::vector<Eigen::Vector3f> points;

  // Intensities are optional and may be unspecified. The meaning of these
  // intensity values varies by device. For example, the VLP16 provides values
  // in the range [0, 100] for non-specular return values and values up to 255
  // for specular returns. On the other hand, Hokuyo lasers provide a 16-bit
  // value that rarely peaks above 4096.
  std::vector<float> intensities;

  // Colors are optional. If set, they are RGB values.
  std::vector<FloatColor> colors;
};

// Removes the indices in 'to_remove' from 'batch'.
void RemovePoints(std::unordered_set<int> to_remove, PointsBatch *batch);

} // namespace io

#endif // IO_POINTS_BATCH_H_
