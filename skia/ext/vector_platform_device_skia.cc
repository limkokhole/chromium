// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "skia/ext/vector_platform_device_skia.h"

#include "skia/ext/bitmap_platform_device.h"
#include "third_party/skia/include/core/SkClipStack.h"
#include "third_party/skia/include/core/SkDraw.h"
#include "third_party/skia/include/core/SkRect.h"
#include "third_party/skia/include/core/SkRegion.h"
#include "third_party/skia/include/core/SkScalar.h"

namespace skia {

SkDevice* VectorPlatformDeviceSkiaFactory::newDevice(SkCanvas* noUsed,
                                                     SkBitmap::Config config,
                                                     int width, int height,
                                                     bool isOpaque,
                                                     bool isForLayer) {
  SkASSERT(config == SkBitmap::kARGB_8888_Config);
  SkPDFDevice::OriginTransform flip = SkPDFDevice::kFlip_OriginTransform;
  if (isForLayer)
    flip = SkPDFDevice::kNoFlip_OriginTransform;
  return new VectorPlatformDeviceSkia(width, height, flip);
}

static inline SkBitmap makeABitmap(int width, int height) {
  SkBitmap bitmap;
  bitmap.setConfig(SkBitmap::kNo_Config, width, height);
  return bitmap;
}

VectorPlatformDeviceSkia::VectorPlatformDeviceSkia(
    int width, int height, SkPDFDevice::OriginTransform flip)
    : PlatformDevice(makeABitmap(width, height)),
      pdf_device_(new SkPDFDevice(width, height, flip)) {
  pdf_device_->unref();  // SkRefPtr and new both took a reference.
  base_transform_.reset();
}

VectorPlatformDeviceSkia::~VectorPlatformDeviceSkia() {
}

bool VectorPlatformDeviceSkia::IsVectorial() {
  return true;
}

bool VectorPlatformDeviceSkia::IsNativeFontRenderingAllowed() {
  return false;
}

PlatformDevice::PlatformSurface VectorPlatformDeviceSkia::BeginPlatformPaint() {
  // Even when drawing a vector representation of the page, we have to
  // provide a raster surface for plugins to render into - they don't have
  // a vector interface.  Therefore we create a BitmapPlatformDevice here
  // and return the context from it, then layer on the raster data as an
  // image in EndPlatformPaint.
  DCHECK(raster_surface_ == NULL);
#if defined(OS_WIN)
  raster_surface_ = BitmapPlatformDevice::create(pdf_device_->width(),
                                                 pdf_device_->height(),
                                                 false, /* not opaque */
                                                 NULL);
#elif defined(OS_LINUX)
  raster_surface_ = BitmapPlatformDevice::Create(pdf_device_->width(),
                                                 pdf_device_->height(),
                                                 false /* not opaque */);
#endif
  raster_surface_->unref();  // SkRefPtr and create both took a reference.

  SkCanvas canvas(raster_surface_.get());
  SkPaint black;
  black.setColor(SK_ColorBLACK);
  canvas.drawPaint(black);
  return raster_surface_->BeginPlatformPaint();
}

void VectorPlatformDeviceSkia::EndPlatformPaint() {
  DCHECK(raster_surface_ != NULL);
  SkPaint paint;
  pdf_device_->drawSprite(SkDraw(),
                          raster_surface_->accessBitmap(false),
                          base_transform_.getTranslateX(),
                          base_transform_.getTranslateY(),
                          paint);
  raster_surface_ = NULL;
}

SkDeviceFactory* VectorPlatformDeviceSkia::getDeviceFactory() {
  return SkNEW(VectorPlatformDeviceSkiaFactory);
}

uint32_t VectorPlatformDeviceSkia::getDeviceCapabilities() {
  return kVector_Capability;
}

int VectorPlatformDeviceSkia::width() const {
  return pdf_device_->width();
}

int VectorPlatformDeviceSkia::height() const {
  return pdf_device_->height();
}

void VectorPlatformDeviceSkia::setMatrixClip(const SkMatrix& matrix,
                                             const SkRegion& region,
                                             const SkClipStack& stack) {
  SkMatrix transform = base_transform_;
  transform.preConcat(matrix);

  DCHECK(SkMatrix::kTranslate_Mask == base_transform_.getType() ||
      SkMatrix::kIdentity_Mask == base_transform_.getType());
  SkRegion clip = region;
  clip.translate(base_transform_.getTranslateX(),
                 base_transform_.getTranslateY());

  pdf_device_->setMatrixClip(transform, clip, stack);
}

bool VectorPlatformDeviceSkia::readPixels(const SkIRect& srcRect,
                                          SkBitmap* bitmap) {
  return false;
}

void VectorPlatformDeviceSkia::drawPaint(const SkDraw& draw,
                                         const SkPaint& paint) {
  pdf_device_->drawPaint(draw, paint);
}

void VectorPlatformDeviceSkia::drawPoints(const SkDraw& draw,
                                          SkCanvas::PointMode mode,
                                          size_t count, const SkPoint pts[],
                                          const SkPaint& paint) {
  pdf_device_->drawPoints(draw, mode, count, pts, paint);
}

void VectorPlatformDeviceSkia::drawRect(const SkDraw& draw,
                                        const SkRect& rect,
                                        const SkPaint& paint) {
  pdf_device_->drawRect(draw, rect, paint);
}

void VectorPlatformDeviceSkia::drawPath(const SkDraw& draw,
                                        const SkPath& path,
                                        const SkPaint& paint,
                                        const SkMatrix* prePathMatrix,
                                        bool pathIsMutable) {
  pdf_device_->drawPath(draw, path, paint, prePathMatrix, pathIsMutable);
}

void VectorPlatformDeviceSkia::drawBitmap(const SkDraw& draw,
                                          const SkBitmap& bitmap,
                                          const SkIRect* srcRectOrNull,
                                          const SkMatrix& matrix,
                                          const SkPaint& paint) {
  pdf_device_->drawBitmap(draw, bitmap, srcRectOrNull, matrix, paint);
}

void VectorPlatformDeviceSkia::drawSprite(const SkDraw& draw,
                                          const SkBitmap& bitmap,
                                          int x, int y,
                                          const SkPaint& paint) {
  pdf_device_->drawSprite(draw, bitmap, x, y, paint);
}

void VectorPlatformDeviceSkia::drawText(const SkDraw& draw,
                                        const void* text,
                                        size_t byteLength,
                                        SkScalar x,
                                        SkScalar y,
                                        const SkPaint& paint) {
  pdf_device_->drawText(draw, text, byteLength, x, y, paint);
}

void VectorPlatformDeviceSkia::drawPosText(const SkDraw& draw,
                                           const void* text,
                                           size_t len,
                                           const SkScalar pos[],
                                           SkScalar constY,
                                           int scalarsPerPos,
                                           const SkPaint& paint) {
  pdf_device_->drawPosText(draw, text, len, pos, constY, scalarsPerPos, paint);
}

void VectorPlatformDeviceSkia::drawTextOnPath(const SkDraw& draw,
                                              const void* text,
                                              size_t len,
                                              const SkPath& path,
                                              const SkMatrix* matrix,
                                              const SkPaint& paint) {
  pdf_device_->drawTextOnPath(draw, text, len, path, matrix, paint);
}

void VectorPlatformDeviceSkia::drawVertices(const SkDraw& draw,
                                            SkCanvas::VertexMode vmode,
                                            int vertexCount,
                                            const SkPoint vertices[],
                                            const SkPoint texs[],
                                            const SkColor colors[],
                                            SkXfermode* xmode,
                                            const uint16_t indices[],
                                            int indexCount,
                                            const SkPaint& paint) {
  pdf_device_->drawVertices(draw, vmode, vertexCount, vertices, texs, colors,
                            xmode, indices, indexCount, paint);
}

void VectorPlatformDeviceSkia::drawDevice(const SkDraw& draw,
                                          SkDevice* device,
                                          int x,
                                          int y,
                                          const SkPaint& paint) {
  SkDevice* real_device = device;
  if ((device->getDeviceCapabilities() & kVector_Capability)) {
    // Assume that a vectorial device means a VectorPlatformDeviceSkia, we need
    // to unwrap the embedded SkPDFDevice.
    VectorPlatformDeviceSkia* vector_device =
        static_cast<VectorPlatformDeviceSkia*>(device);
    real_device = vector_device->pdf_device_.get();
  }
  pdf_device_->drawDevice(draw, real_device, x, y, paint);
}

#if defined(OS_WIN)
void VectorPlatformDeviceSkia::drawToHDC(HDC dc,
                                         int x,
                                         int y,
                                         const RECT* src_rect) {
  SkASSERT(false);
}
#endif

void VectorPlatformDeviceSkia::setInitialTransform(int xOffset, int yOffset,
                                                   float scale_factor) {
  // TODO(vandebo) Supporting a scale factor is some work because we have to
  // transform both matrices and clips that come in, but Region only supports
  // translation.  Instead, we could change SkPDFDevice to include it in the
  // initial transform.  Delay that work until we would use it.  Also checked
  // in setMatrixClip.
  DCHECK_EQ(1.0f, scale_factor);

  base_transform_.setTranslate(xOffset, yOffset);
  SkScalar scale = SkFloatToScalar(scale_factor);
  base_transform_.postScale(scale, scale);

  SkMatrix matrix;
  matrix.reset();
  SkRegion region;
  SkClipStack stack;
  setMatrixClip(matrix, region, stack);
}

}  // namespace skia
