/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkGradientShader.h"
#include "SkSVGLinearGradient.h"
#include "SkSVGRenderContext.h"
#include "SkSVGStop.h"
#include "SkSVGValue.h"

SkSVGLinearGradient::SkSVGLinearGradient() : INHERITED(SkSVGTag::kLinearGradient) {}

void SkSVGLinearGradient::setHref(const SkSVGStringType& href) {
    fHref = std::move(href);
}

void SkSVGLinearGradient::setX1(const SkSVGLength& x1) {
    fX1 = x1;
}

void SkSVGLinearGradient::setY1(const SkSVGLength& y1) {
    fY1 = y1;
}

void SkSVGLinearGradient::setX2(const SkSVGLength& x2) {
    fX2 = x2;
}

void SkSVGLinearGradient::setY2(const SkSVGLength& y2) {
    fY2 = y2;
}

void SkSVGLinearGradient::onSetAttribute(SkSVGAttribute attr, const SkSVGValue& v) {
    switch (attr) {
    case SkSVGAttribute::kHref:
        if (const auto* href = v.as<SkSVGStringValue>()) {
            this->setHref(*href);
        }
        break;
    case SkSVGAttribute::kX1:
        if (const auto* x1 = v.as<SkSVGLengthValue>()) {
            this->setX1(*x1);
        }
        break;
    case SkSVGAttribute::kY1:
        if (const auto* y1 = v.as<SkSVGLengthValue>()) {
            this->setY1(*y1);
        }
        break;
    case SkSVGAttribute::kX2:
        if (const auto* x2 = v.as<SkSVGLengthValue>()) {
            this->setX2(*x2);
        }
        break;
    case SkSVGAttribute::kY2:
        if (const auto* y2 = v.as<SkSVGLengthValue>()) {
            this->setY2(*y2);
        }
        break;
    default:
        this->INHERITED::onSetAttribute(attr, v);
    }
}

// https://www.w3.org/TR/SVG/pservers.html#LinearGradientElementHrefAttribute
void SkSVGLinearGradient::collectColorStops(const SkSVGRenderContext& ctx,
                                            SkSTArray<2, SkScalar, true>* pos,
                                            SkSTArray<2, SkColor, true>* colors) const {
    // Used to resolve percentage offsets.
    const SkSVGLengthContext ltx(SkSize::Make(1, 1));

    for (const auto& child : fChildren) {
        if (child->tag() != SkSVGTag::kStop) {
            continue;
        }

        const auto& stop = static_cast<const SkSVGStop&>(*child);
        colors->push_back(SkColorSetA(stop.stopColor(),
                                      SkScalarRoundToInt(stop.stopOpacity() * 255)));
        pos->push_back(SkTPin(ltx.resolve(stop.offset(), SkSVGLengthContext::LengthType::kOther),
                              0.f, 1.f));
    }

    SkASSERT(colors->count() == pos->count());

    if (pos->empty() && !fHref.value().isEmpty()) {
        const auto* ref = ctx.findNodeById(fHref);
        if (ref && ref->tag() == SkSVGTag::kLinearGradient) {
            static_cast<const SkSVGLinearGradient*>(ref)->collectColorStops(ctx, pos, colors);
        }
    }
}

bool SkSVGLinearGradient::onAsPaint(const SkSVGRenderContext& ctx, SkPaint* paint) const {
    const auto& lctx = ctx.lengthContext();
    const auto x1 = lctx.resolve(fX1, SkSVGLengthContext::LengthType::kHorizontal);
    const auto y1 = lctx.resolve(fY1, SkSVGLengthContext::LengthType::kVertical);
    const auto x2 = lctx.resolve(fX2, SkSVGLengthContext::LengthType::kHorizontal);
    const auto y2 = lctx.resolve(fY2, SkSVGLengthContext::LengthType::kVertical);

    const SkPoint pts[2] = { {x1, y1}, {x2, y2}};
    SkSTArray<2, SkColor , true> colors;
    SkSTArray<2, SkScalar, true> pos;

    this->collectColorStops(ctx, &pos, &colors);
    // TODO:
    //       * stop (lazy?) sorting
    //       * href loop detection
    //       * href attribute inheritance (not just color stops)
    //       * spreadMethods support
    //       * objectBoundingBox units support

    paint->setShader(SkGradientShader::MakeLinear(pts, colors.begin(), pos.begin(), colors.count(),
                                                  SkShader::kClamp_TileMode));
    return true;
}
