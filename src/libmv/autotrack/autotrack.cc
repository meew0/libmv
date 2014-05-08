// Copyright (c) 2014 libmv authors.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//
// Author: mierle@gmail.com (Keir Mierle)

#include "libmv/autotrack/autotrack.h"
#include "libmv/autotrack/quad.h"
#include "libmv/autotrack/frame_accessor.h"
#include "libmv/logging/logging.h"
#include "libmv/numeric/numeric.h"

namespace mv {

namespace {

template<typename QuadT, typename ArrayT>
void QuadToArrays(const QuadT& quad, ArrayT* x, ArrayT* y) {
  for (int i = 0; i < 4; ++i) {
    x[i] = quad.coordinates(i, 0);
    y[i] = quad.coordinates(i, 1);
  }
}

void MarkerToArrays(const Marker& marker,
                    Vec2i* origin,
                    double* x, double* y) {
  *origin = marker.center.cast<int>() + marker.search_region.min;
  Quad2Df offset_quad = marker.patch;
  for (int i = 0; i < 4; ++i) {
    offset_quad.coordinates.row(i) -= origin->cast<float>();
  }
  QuadToArrays(offset_quad, x, y);
  x[4] = marker.center.x() - (*origin)(0);
  y[4] = marker.center.y() - (*origin)(1);
}

FrameAccessor::Key GetImageForMarker(const Marker& marker,
                                     FrameAccessor* frame_accessor,
                                     FloatImage* image) {
  // The input region has the coordinate centered around the marker center
  // (e.g. a typical window would have (-20, -20, 20, 20) as the coordinates);
  // so shift the region so it is centered on the marker in the frame's
  // coordinate system.
  // TODO(keir): Perhaps this should be a marker helper?
  Region region_in_frame = marker.search_region;
  region_in_frame.Offset(marker.center);

  return frame_accessor->GetImage(marker.clip,
                                  marker.frame,
                                  FrameAccessor::MONO,
                                  0,  // No downscale for now.
                                  &region_in_frame,
                                  NULL,
                                  image);
}

}  // namespace

bool AutoTrack::TrackMarkerToFrame(const Marker& reference_marker,
                                   const TrackRegionOptions& track_options,
                                   Marker* tracked_marker,
                                   TrackRegionResult* result) {
  // Convert markers into the format expected by TrackRegion.
  double x1[5], y1[5];
  Vec2i reference_origin;
  MarkerToArrays(reference_marker, &reference_origin, x1, y1);

  double x2[5], y2[5];
  Vec2i tracked_origin;
  MarkerToArrays(*tracked_marker, &tracked_origin, x2, y2);

  // TODO(keir): Technically this could take a smaller slice from the source
  // image instead of taking one the size of the search window.
  FloatImage reference_image;
  FrameAccessor::Key reference_key = GetImageForMarker(reference_marker,
                                                       frame_accessor_,
                                                       &reference_image);
  if (!reference_key) {
    LG << "Couldn't get frame for reference marker: " << reference_marker;
    return false;
  }

  FloatImage tracked_image;
  FrameAccessor::Key tracked_key = GetImageForMarker(*tracked_marker,
                                                     frame_accessor_,
                                                     &tracked_image);
  if (!tracked_key) {
    LG << "Couldn't get frame for tracked marker: " << tracked_marker;
    return false;
  }

  // Do the tracking!
  TrackRegionOptions local_track_region_options = track_options;
  local_track_region_options.num_extra_points = 1;  // For extra center point.
  TrackRegion(reference_image,
              tracked_image,
              x1, y1,
              local_track_region_options,
              x2, y2,
              result);

  // Copy results over the tracked marker.
  for (int i = 0; i < 4; ++i) {
    tracked_marker->patch.coordinates(i, 0) = x2[i] + tracked_origin[0];
    tracked_marker->patch.coordinates(i, 1) = y2[i] + tracked_origin[1];
  }
  tracked_marker->center(0) = x2[4] + tracked_origin[0];
  tracked_marker->center(1) = y2[4] + tracked_origin[1];
  tracked_marker->source = Marker::TRACKED;
  tracked_marker->status = Marker::UNKNOWN;
  tracked_marker->reference_clip  = reference_marker.clip;
  tracked_marker->reference_frame = reference_marker.frame;

  // Release the images from the accessor cache.
  frame_accessor_->ReleaseImage(reference_key);
  frame_accessor_->ReleaseImage(tracked_key);

  // TODO(keir): Possibly the return here should get removed since the results
  // are part of TrackResult. However, eventually the autotrack stuff will have
  // extra status (e.g. prediction fail, etc) that should get included.
  return true;
}

void AutoTrack::AddMarker(const Marker& marker) {
  tracks_.AddMarker(marker);
}

void AutoTrack::SetMarkers(vector<Marker>* markers) {
  tracks_.SetMarkers(markers);
}

}  // namespace mv
