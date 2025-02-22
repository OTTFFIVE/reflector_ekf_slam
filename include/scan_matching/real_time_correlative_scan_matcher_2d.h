#ifndef SCAN_MATCHING_REAL_TIME_CORRELATIVE_SCAN_MATCHER_2D_H_
#define SCAN_MATCHING_REAL_TIME_CORRELATIVE_SCAN_MATCHER_2D_H_

// This is an implementation of the algorithm described in "Real-Time
// Correlative Scan Matching" by Olson.
//
// The correlative scan matching algorithm is exhaustively evaluating the scan
// matching search space. As described by the paper, the basic steps are:
//
// 1) Evaluate the probability p(z|xi, m) over the entire 3D search window using
// the low-resolution table.
// 2) Find the best voxel in the low-resolution 3D space that has not already
// been considered. Denote this value as Li. If Li < Hbest, terminate: Hbest is
// the best scan matching alignment.
// 3) Evaluate the search volume inside voxel i using the high resolution table.
// Suppose the log-likelihood of this voxel is Hi. Note that Hi <= Li since the
// low-resolution map overestimates the log likelihoods. If Hi > Hbest, set
// Hbest = Hi.
//
// This can be made even faster by transforming the scan exactly once over some
// discretized range.

#include <iostream>
#include <memory>
#include <vector>

#include "Eigen/Core"
#include "mapping/grid_2d.h"
#include "scan_matching/correlative_scan_matcher_2d.h"

namespace scan_matching
{

struct RealTimeCorrelativeScanMatcherOptions
{
    double linear_search_window;
    double angular_search_window;
    double translation_delta_cost_weight;
    double rotation_delta_cost_weight;
};

// An implementation of "Real-Time Correlative Scan Matching" by Olson.
class RealTimeCorrelativeScanMatcher2D
{
  public:
    explicit RealTimeCorrelativeScanMatcher2D(
        const RealTimeCorrelativeScanMatcherOptions &options);

    RealTimeCorrelativeScanMatcher2D(const RealTimeCorrelativeScanMatcher2D &) =
        delete;
    RealTimeCorrelativeScanMatcher2D &operator=(
        const RealTimeCorrelativeScanMatcher2D &) = delete;

    // Aligns 'point_cloud' within the 'grid' given an
    // 'initial_pose_estimate' then updates 'pose_estimate' with the result and
    // returns the score.
    double Match(const transform::Rigid2d &initial_pose_estimate,
                 const sensor::PointCloud &point_cloud, const mapping::Grid2D &grid,
                 transform::Rigid2d *pose_estimate) const;

    // Computes the score for each Candidate2D in a collection. The cost is
    // computed as the sum of probabilities or normalized TSD values, different
    // from the Ceres CostFunctions: http://ceres-solver.org/modeling.html
    //
    // Visible for testing.
    void ScoreCandidates(const mapping::Grid2D &grid,
                         const std::vector<DiscreteScan2D> &discrete_scans,
                         const SearchParameters &search_parameters,
                         std::vector<Candidate2D> *candidates) const;

  private:
    std::vector<Candidate2D> GenerateExhaustiveSearchCandidates(
        const SearchParameters &search_parameters) const;

    const RealTimeCorrelativeScanMatcherOptions options_;
};

} // namespace scan_matching

#endif // SCAN_MATCHING_REAL_TIME_CORRELATIVE_SCAN_MATCHER_2D_H_
