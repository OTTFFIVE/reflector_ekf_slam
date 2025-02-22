#include "io/submap_painter.h"

namespace io
{
namespace
{

Eigen::Affine3d ToEigen(const transform::Rigid3d &rigid3)
{
  return Eigen::Translation3d(rigid3.translation()) * rigid3.rotation();
}

void CairoPaintSubmapSlices(
    const double scale,
    const SubmapSlice &submap,
    cairo_t *cr, std::function<void(const SubmapSlice &)> draw_callback)
{
  cairo_scale(cr, scale, scale);

  if (submap.surface == nullptr)
  {
    return;
  }
  const Eigen::Matrix4d homo =
      ToEigen(submap.pose * submap.slice_pose).matrix();

  cairo_save(cr);
  cairo_matrix_t matrix;
  cairo_matrix_init(&matrix, homo(1, 0), homo(0, 0), -homo(1, 1), -homo(0, 1),
                    homo(0, 3), -homo(1, 3));
  cairo_transform(cr, &matrix);

  const double submap_resolution = submap.resolution;
  cairo_scale(cr, submap_resolution, submap_resolution);

  // Invokes caller's callback to utilize slice data in global cooridnate
  // frame. e.g. finds bounding box, paints slices.
  draw_callback(submap);
  cairo_restore(cr);
}

} // namespace

PaintSubmapSlicesResult PaintSubmapSlices(
    const SubmapSlice &submap,
    const double resolution)
{
  Eigen::AlignedBox2f bounding_box;
  {
    auto surface = MakeUniqueCairoSurfacePtr(
        cairo_image_surface_create(kCairoFormat, 1, 1));
    auto cr = MakeUniqueCairoPtr(cairo_create(surface.get()));
    const auto update_bounding_box = [&bounding_box, &cr](double x, double y) {
      cairo_user_to_device(cr.get(), &x, &y);
      bounding_box.extend(Eigen::Vector2f(x, y));
    };

    CairoPaintSubmapSlices(
        1. / resolution, submap, cr.get(),
        [&update_bounding_box](const SubmapSlice &submap_slice) {
          update_bounding_box(0, 0);
          update_bounding_box(submap_slice.width, 0);
          update_bounding_box(0, submap_slice.height);
          update_bounding_box(submap_slice.width, submap_slice.height);
        });
  }

  const int kPaddingPixel = 5;
  const Eigen::Array2i size(
      std::ceil(bounding_box.sizes().x()) + 2 * kPaddingPixel,
      std::ceil(bounding_box.sizes().y()) + 2 * kPaddingPixel);
  const Eigen::Array2f origin(-bounding_box.min().x() + kPaddingPixel,
                              -bounding_box.min().y() + kPaddingPixel);

  auto surface = MakeUniqueCairoSurfacePtr(
      cairo_image_surface_create(kCairoFormat, size.x(), size.y()));
  {
    auto cr = MakeUniqueCairoPtr(cairo_create(surface.get()));
    cairo_set_source_rgba(cr.get(), 0.5, 0.0, 0.0, 1.);
    cairo_paint(cr.get());
    cairo_translate(cr.get(), origin.x(), origin.y());
    CairoPaintSubmapSlices(1. / resolution, submap, cr.get(),
                           [&cr](const SubmapSlice &submap_slice) {
                             cairo_set_source_surface(
                                 cr.get(), submap_slice.surface.get(), 0., 0.);
                             cairo_paint(cr.get());
                           });
    cairo_surface_flush(surface.get());
  }
  return PaintSubmapSlicesResult(std::move(surface), origin);
}

void FillSubmapSlice(
    const transform::Rigid3d &global_submap_pose,
    const mapping::SubmapTexture &proto,
    SubmapSlice *const submap_slice,
    mapping::ValueConversionTables *conversion_tables)
{

  submap_slice->pose = global_submap_pose;

  auto &texture_proto = proto;
  const SubmapTexture::Pixels pixels = UnpackTextureData(
      texture_proto.cells, texture_proto.width, texture_proto.height);
  submap_slice->width = texture_proto.width;
  submap_slice->height = texture_proto.height;
  submap_slice->resolution = texture_proto.resolution;
  submap_slice->slice_pose = texture_proto.slice_pose;
  submap_slice->surface =
      DrawTexture(pixels.intensity, pixels.alpha, texture_proto.width,
                  texture_proto.height, &submap_slice->cairo_data);
}

SubmapTexture::Pixels UnpackTextureData(const std::string &compressed_cells,
                                        const int width, const int height)
{
  SubmapTexture::Pixels pixels;
  std::string cells;
  common::FastGunzipString(compressed_cells, &cells);
  const int num_pixels = width * height;
  CHECK_EQ(cells.size(), 2 * num_pixels);
  pixels.intensity.reserve(num_pixels);
  pixels.alpha.reserve(num_pixels);
  for (int i = 0; i < height; ++i)
  {
    for (int j = 0; j < width; ++j)
    {
      pixels.intensity.push_back(cells[(i * width + j) * 2]);
      pixels.alpha.push_back(cells[(i * width + j) * 2 + 1]);
    }
  }
  return pixels;
}

UniqueCairoSurfacePtr DrawTexture(const std::vector<char> &intensity,
                                  const std::vector<char> &alpha,
                                  const int width, const int height,
                                  std::vector<uint32_t> *const cairo_data)
{
  CHECK(cairo_data->empty());

  // Properly dealing with a non-common stride would make this code much more
  // complicated. Let's check that it is not needed.
  const int expected_stride = 4 * width;
  CHECK_EQ(expected_stride, cairo_format_stride_for_width(kCairoFormat, width));
  for (size_t i = 0; i < intensity.size(); ++i)
  {
    // We use the red channel to track intensity information. The green
    // channel we use to track if a cell was ever observed.
    const uint8_t intensity_value = intensity.at(i);
    const uint8_t alpha_value = alpha.at(i);
    const uint8_t observed =
        (intensity_value == 0 && alpha_value == 0) ? 0 : 255;
    cairo_data->push_back((alpha_value << 24) | (intensity_value << 16) |
                          (observed << 8) | 0);
  }

  auto surface = MakeUniqueCairoSurfacePtr(cairo_image_surface_create_for_data(
      reinterpret_cast<unsigned char *>(cairo_data->data()), kCairoFormat, width,
      height, expected_stride));
  CHECK_EQ(cairo_surface_status(surface.get()), CAIRO_STATUS_SUCCESS)
      << cairo_status_to_string(cairo_surface_status(surface.get()));
  return surface;
}

} // namespace io
