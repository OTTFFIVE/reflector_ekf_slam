#ifndef SCAN_MATCHING_CORRELATIVE_SCAN_MATCHER_2D_H_
#define SCAN_MATCHING_CORRELATIVE_SCAN_MATCHER_2D_H_

#include <vector>

#include "Eigen/Core"
#include "mapping/map_limits.h"
#include "mapping/xy_index.h"
#include "sensor/sensor_data.h"

namespace scan_matching
{

typedef std::vector<Eigen::Array2i> DiscreteScan2D;

// Describes the search space.
struct SearchParameters
{
  // Linear search window in pixel offsets; bounds are inclusive.
  struct LinearBounds
  {
    int min_x;
    int max_x;
    int min_y;
    int max_y;
  };

  SearchParameters(double linear_search_window, double angular_search_window,
                   const sensor::PointCloud &point_cloud, double resolution);

  // For testing.
  SearchParameters(int num_linear_perturbations, int num_angular_perturbations,
                   double angular_perturbation_step_size, double resolution);

  // Tightens the search window as much as possible.
  void ShrinkToFit(const std::vector<DiscreteScan2D> &scans,
                   const mapping::CellLimits &cell_limits);

  int num_angular_perturbations;
  double angular_perturbation_step_size;
  double resolution;
  int num_scans;
  std::vector<LinearBounds> linear_bounds; // Per rotated scans.
};

// Generates a collection of rotated scans.
std::vector<sensor::PointCloud> GenerateRotatedScans(
    const sensor::PointCloud &point_cloud,
    const SearchParameters &search_parameters);

// Translates and discretizes the rotated scans into a vector of integer
// indices.
std::vector<DiscreteScan2D> DiscretizeScans(
    const mapping::MapLimits &map_limits, const std::vector<sensor::PointCloud> &scans,
    const Eigen::Translation2f &initial_translation);

// A possible solution.
struct Candidate2D
{
  Candidate2D(const int init_scan_index, const int init_x_index_offset,
              const int init_y_index_offset,
              const SearchParameters &search_parameters)
      : scan_index(init_scan_index),
        x_index_offset(init_x_index_offset),
        y_index_offset(init_y_index_offset),
        x(-y_index_offset * search_parameters.resolution),
        y(-x_index_offset * search_parameters.resolution),
        orientation((scan_index - search_parameters.num_angular_perturbations) *
                    search_parameters.angular_perturbation_step_size) {}

  // Index into the rotated scans vector.
  int scan_index = 0;

  // Linear offset from the initial pose.
  int x_index_offset = 0;
  int y_index_offset = 0;

  // Pose of this Candidate2D relative to the initial pose.
  double x = 0.;
  double y = 0.;
  double orientation = 0.;

  // Score, higher is better.
  float score = 0.f;

  bool operator<(const Candidate2D &other) const { return score < other.score; }
  bool operator>(const Candidate2D &other) const { return score > other.score; }
};

} // namespace scan_matching

#endif // SCAN_MATCHING_CORRELATIVE_SCAN_MATCHER_2D_H_
