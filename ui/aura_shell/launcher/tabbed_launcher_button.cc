// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/aura_shell/launcher/tabbed_launcher_button.h"

#include <algorithm>

#include "grit/ui_resources.h"
#include "ui/aura_shell/launcher/launcher_button_host.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/insets.h"
#include "views/painter.h"

namespace aura_shell {

namespace internal {

// The images drawn inside the background tab are drawn at this offset from
// the edge.
const int kBgImageContentInset = 12;

// Padding between each of the images.
const int kImagePadding = 8;

// Insets used in painting the background if it's rendered bigger than the size
// of the background image. See ImagePainter::CreateImagePainter for how these
// are interpreted.
const int kBgTopInset = 12;
const int kBgLeftInset = 30;
const int kBgBottomInset = 12;
const int kBgRightInset = 8;

// static
SkBitmap* TabbedLauncherButton::bg_image_ = NULL;

TabbedLauncherButton::TabbedLauncherButton(views::ButtonListener* listener,
                                           LauncherButtonHost* host)
    : views::CustomButton(listener),
      host_(host) {
  if (!bg_image_) {
    ResourceBundle& rb = ResourceBundle::GetSharedInstance();
    bg_image_ = new SkBitmap(
        *rb.GetImageNamed(IDR_AURA_LAUNCHER_TABBED_BROWSER).ToSkBitmap());
  }
}

TabbedLauncherButton::~TabbedLauncherButton() {
}

void TabbedLauncherButton::SetImages(const LauncherTabbedImages& images) {
  images_ = images;
}

gfx::Size TabbedLauncherButton::GetPreferredSize() {
  if (images_.empty())
    return gfx::Size(bg_image_->width(), bg_image_->height());

  // Assume all images are the same size.
  int num_images = static_cast<int>(images_.size());
  int padding = (num_images - 1) * kImagePadding;
  return gfx::Size(
      std::max(kBgImageContentInset * 2 +
               images_[0].image.width() * num_images + padding,
               bg_image_->width()),
      bg_image_->height());
}

void TabbedLauncherButton::OnPaint(gfx::Canvas* canvas) {
  if (width() == bg_image_->width()) {
    canvas->DrawBitmapInt(*bg_image_, 0, 0);
  } else {
    scoped_ptr<views::Painter> bg_painter(views::Painter::CreateImagePainter(
        *bg_image_, gfx::Insets(kBgTopInset, kBgLeftInset, kBgBottomInset,
                                kBgRightInset), true));
    bg_painter->Paint(width(), height(), canvas);
  }

  if (images_.empty())
    return;

  int num_images = static_cast<int>(images_.size());
  int padding = (num_images - 1) * kImagePadding;
  int x = std::max(kBgImageContentInset,
      (width() - num_images * images_[0].image.width() - padding) / 2);
  int y = (height() - images_[0].image.height()) / 2;
  for (LauncherTabbedImages::const_iterator i = images_.begin();
       i != images_.end(); ++i) {
    canvas->DrawBitmapInt(i->image, x, y);
    x += i->image.width() + kImagePadding;
  }
}

bool TabbedLauncherButton::OnMousePressed(const views::MouseEvent& event) {
  CustomButton::OnMousePressed(event);
  host_->MousePressedOnButton(this, event);
  return true;
}

void TabbedLauncherButton::OnMouseReleased(const views::MouseEvent& event) {
  host_->MouseReleasedOnButton(this, false);
  CustomButton::OnMouseReleased(event);
}

void TabbedLauncherButton::OnMouseCaptureLost() {
  host_->MouseReleasedOnButton(this, true);
  CustomButton::OnMouseCaptureLost();
}

bool TabbedLauncherButton::OnMouseDragged(const views::MouseEvent& event) {
  CustomButton::OnMouseDragged(event);
  host_->MouseDraggedOnButton(this, event);
  return true;
}

}  // namespace internal
}  // namespace aura_shell
