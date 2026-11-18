// SPDX-License-Identifier: BSD-3-Clause
#include "shell/controlcenter/control_center_view.h"

#include <cmath>

#include "shell/controlcenter/panel_style.h"
#include "shell/controlcenter/status_panel.h"
#include "ui/animation.h"
#include "ui/window.h"

namespace cutefish::shell {

ControlCenterPager::ControlCenterPager(std::unique_ptr<ControlCenterView> main)
{
    m_main = main.get();
    addChild(std::move(main));
    m_main->onHeightChanged = [this] { sizeChanged(); };
}

ControlCenterPager::~ControlCenterPager() = default;

bool ControlCenterPager::isSliding() const
{
    return m_tween && m_tween->isRunning();
}

void ControlCenterPager::showDetail(std::unique_ptr<StatusPanelView> page)
{
    if (m_detail)
        takeChild(m_detail);
    m_detail = page.get();
    addChild(std::move(page));
    m_detail->onShownBottomChanged = [this] {
        requestLayout();
        sizeChanged();
    };
    m_detail->setGeometry({0, 0, status_panel::kWidth, m_detail->contentHeight()});
    m_detail->setShown(false);
    m_detail->setShown(true);
    slideTo(1);
}

void ControlCenterPager::showMain()
{
    if (m_detail)
        slideTo(0);
}

void ControlCenterPager::slideTo(double target)
{
    m_target = target;
    if (!window() || m_progress == target) {
        if (m_tween)
            m_tween->stop();
        setProgress(target);
        settle();
        return;
    }
    if (!m_tween) {
        m_tween = std::make_unique<ui::Animation>(window());
        m_tween->setDuration(status_panel::kHeightDuration);
        m_tween->setEasing(ui::Animation::Easing::Linear);
        m_tween->onUpdate = [this](double t) { setProgress(m_from + (m_target - m_from) * panel::glide(t)); };
        m_tween->onFinished = [this] { settle(); };
    }
    m_tween->stop();
    m_from = m_progress;
    m_main->setVisible(true);
    m_main->setLayered(true);
    m_detail->setLayered(true);
    setClipsChildren(true);
    m_tween->start();
}

void ControlCenterPager::setProgress(double progress)
{
    if (progress == m_progress)
        return;
    m_progress = progress;
    requestLayout();
    sizeChanged();
}

void ControlCenterPager::settle()
{
    if (m_target == 0 && m_detail) {
        takeChild(m_detail);
        m_detail = nullptr;
    }
    m_main->setVisible(m_target == 0);
    m_main->setLayered(false);
    if (m_detail)
        m_detail->setLayered(false);
    setClipsChildren(false);
    requestLayout();
    sizeChanged();
}

void ControlCenterPager::sizeChanged()
{
    if (onSizeChanged)
        onSizeChanged();
}

double ControlCenterPager::windowWidth() const
{
    const double main = m_main->windowWidth();
    return m_detail ? std::round(main + (status_panel::kWidth - main) * m_progress) : main;
}

double ControlCenterPager::windowHeight(double scale) const
{
    const double main = m_main->windowHeight(scale);
    if (!m_detail)
        return main;
    const double detail = std::max(1.0, std::round(m_detail->shownBottom()));
    return std::round(main + (detail - main) * m_progress);
}

void ControlCenterPager::layout()
{
    const double scale = window() ? window()->scale() : 1;
    const auto snapped = [scale](double x) { return std::round(x * scale) / scale; };
    m_main->setGeometry({0, 0, m_main->windowWidth(), m_main->windowHeight(scale)});
    m_main->setOpacity(1 - m_progress);
    m_main->setTransform({{snapped(-kSlide * m_progress), 0}, 1});
    if (m_detail) {
        m_detail->setGeometry({0, 0, status_panel::kWidth, std::max(1.0, std::ceil(m_detail->shownBottom()))});
        m_detail->setOpacity(m_progress);
        m_detail->setTransform({{snapped(kSlide * (1 - m_progress)), 0}, 1});
    }
}

} // namespace cutefish::shell
