#include "scan_matching/real_time_correlative_scan_matcher_2d.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "Eigen/Geometry"
#include "common/math.h"
#include "mapping/probability_grid.h"
#include "sensor/sensor_data.h"
#include "transform/transform.h"
#include "glog/logging.h"

namespace scan_matching
{
namespace
{

float ComputeCandidateScore(const mapping::ProbabilityGrid &probability_grid,
                            const DiscreteScan2D &discrete_scan,
                            int x_index_offset, int y_index_offset)
{
    float candidate_score = 0.f;
    for (const Eigen::Array2i &xy_index : discrete_scan)
    {
        const Eigen::Array2i proposed_xy_index(xy_index.x() + x_index_offset,
                                               xy_index.y() + y_index_offset);
        const float probability =
            probability_grid.GetProbability(proposed_xy_index);
        candidate_score += probability;
    }
    candidate_score /= static_cast<float>(discrete_scan.size());
    CHECK_GT(candidate_score, 0.f);
    return candidate_score;
}

} // namespace

RealTimeCorrelativeScanMatcher2D::RealTimeCorrelativeScanMatcher2D(
    const RealTimeCorrelativeScanMatcherOptions &options)
    : options_(options) {}

std::vector<Candidate2D>
RealTimeCorrelativeScanMatcher2D::GenerateExhaustiveSearchCandidates(
    const SearchParameters &search_parameters) const
{
    int num_candidates = 0;
    for (int scan_index = 0; scan_index != search_parameters.num_scans;
         ++scan_index)
    {
        const int num_linear_x_candidates =
            (search_parameters.linear_bounds[scan_index].max_x -
             search_parameters.linear_bounds[scan_index].min_x + 1);
        const int num_linear_y_candidates =
            (search_parameters.linear_bounds[scan_index].max_y -
             search_parameters.linear_bounds[scan_index].min_y + 1);
        num_candidates += num_linear_x_candidates * num_linear_y_candidates;
    }
    std::vector<Candidate2D> candidates;
    candidates.reserve(num_candidates);
    for (int scan_index = 0; scan_index != search_parameters.num_scans;
         ++scan_index)
    {
        for (int x_index_offset = search_parameters.linear_bounds[scan_index].min_x;
             x_index_offset <= search_parameters.linear_bounds[scan_index].max_x;
             ++x_index_offset)
        {
            for (int y_index_offset =
                     search_parameters.linear_bounds[scan_index].min_y;
                 y_index_offset <= search_parameters.linear_bounds[scan_index].max_y;
                 ++y_index_offset)
            {
                candidates.emplace_back(scan_index, x_index_offset, y_index_offset,
                                        search_parameters);
            }
        }
    }
    CHECK_EQ(candidates.size(), num_candidates);
    return candidates;
}

double RealTimeCorrelativeScanMatcher2D::Match(
    const transform::Rigid2d &initial_pose_estimate,
    const sensor::PointCloud &point_cloud, const mapping::Grid2D &grid,
    transform::Rigid2d *pose_estimate) const
{
    CHECK(pose_estimate != nullptr);

    const Eigen::Rotation2Dd initial_rotation = initial_pose_estimate.rotation();
    const sensor::PointCloud rotated_point_cloud = sensor::TransformPointCloud(
        point_cloud,
        transform::Project2D(transform::Rigid3f::Rotation(Eigen::AngleAxisf(
            initial_rotation.cast<float>().angle(), Eigen::Vector3f::UnitZ()))));
    const SearchParameters search_parameters(
        options_.linear_search_window, options_.angular_search_window,
        rotated_point_cloud, grid.limits().resolution());

    const std::vector<sensor::PointCloud> rotated_scans =
        GenerateRotatedScans(rotated_point_cloud, search_parameters);
    const std::vector<DiscreteScan2D> discrete_scans = DiscretizeScans(
        grid.limits(), rotated_scans,
        Eigen::Translation2f(initial_pose_estimate.translation().x(),
                             initial_pose_estimate.translation().y()));
    std::vector<Candidate2D> candidates =
        GenerateExhaustiveSearchCandidates(search_parameters);
    ScoreCandidates(grid, discrete_scans, search_parameters, &candidates);

    const Candidate2D &best_candidate =
        *std::max_element(candidates.begin(), candidates.end());
    *pose_estimate = transform::Rigid2d(
        {initial_pose_estimate.translation().x() + best_candidate.x,
         initial_pose_estimate.translation().y() + best_candidate.y},
        initial_rotation * Eigen::Rotation2Dd(best_candidate.orientation));
    return best_candidate.score;
}

void RealTimeCorrelativeScanMatcher2D::ScoreCandidates(
    const mapping::Grid2D &grid, const std::vector<DiscreteScan2D> &discrete_scans,
    const SearchParameters &search_parameters,
    std::vector<Candidate2D> *const candidates) const
{
    for (Candidate2D &candidate : *candidates)
    {
        candidate.score = ComputeCandidateScore(
            static_cast<const mapping::ProbabilityGrid &>(grid),
            discrete_scans[candidate.scan_index], candidate.x_index_offset,
            candidate.y_index_offset);

        candidate.score *=
            std::exp(-common::Pow2(std::hypot(candidate.x, candidate.y) *
                                       options_.translation_delta_cost_weight +
                                   std::abs(candidate.orientation) *
                                       options_.rotation_delta_cost_weight));
    }
}

} // namespace scan_matching
