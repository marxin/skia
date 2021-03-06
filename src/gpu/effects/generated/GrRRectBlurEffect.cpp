/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrRRectBlurEffect.fp; do not modify.
 **************************************************************************************************/
#include "GrRRectBlurEffect.h"

#include "include/gpu/GrRecordingContext.h"
#include "src/core/SkBlurPriv.h"
#include "src/core/SkGpuBlurUtils.h"
#include "src/core/SkRRectPriv.h"
#include "src/gpu/GrCaps.h"
#include "src/gpu/GrPaint.h"
#include "src/gpu/GrProxyProvider.h"
#include "src/gpu/GrRecordingContextPriv.h"
#include "src/gpu/GrRenderTargetContext.h"
#include "src/gpu/GrStyle.h"
#include "src/gpu/effects/GrTextureEffect.h"

static std::unique_ptr<GrFragmentProcessor> find_or_create_rrect_blur_mask_fp(
        GrRecordingContext* context,
        const SkRRect& rrectToDraw,
        const SkISize& dimensions,
        float xformedSigma) {
    static const GrUniqueKey::Domain kDomain = GrUniqueKey::GenerateDomain();
    GrUniqueKey key;
    GrUniqueKey::Builder builder(&key, kDomain, 9, "RoundRect Blur Mask");
    builder[0] = SkScalarCeilToInt(xformedSigma - 1 / 6.0f);

    int index = 1;
    for (auto c : {SkRRect::kUpperLeft_Corner, SkRRect::kUpperRight_Corner,
                   SkRRect::kLowerRight_Corner, SkRRect::kLowerLeft_Corner}) {
        SkASSERT(SkScalarIsInt(rrectToDraw.radii(c).fX) && SkScalarIsInt(rrectToDraw.radii(c).fY));
        builder[index++] = SkScalarCeilToInt(rrectToDraw.radii(c).fX);
        builder[index++] = SkScalarCeilToInt(rrectToDraw.radii(c).fY);
    }
    builder.finish();

    // It seems like we could omit this matrix and modify the shader code to not normalize
    // the coords used to sample the texture effect. However, the "proxyDims" value in the
    // shader is not always the actual the proxy dimensions. This is because 'dimensions' here
    // was computed using integer corner radii as determined in
    // SkComputeBlurredRRectParams whereas the shader code uses the float radius to compute
    // 'proxyDims'. Why it draws correctly with these unequal values is a mystery for the ages.
    auto m = SkMatrix::Scale(dimensions.width(), dimensions.height());
    static constexpr auto kMaskOrigin = kBottomLeft_GrSurfaceOrigin;
    GrProxyProvider* proxyProvider = context->priv().proxyProvider();

    if (auto view = proxyProvider->findCachedProxyWithColorTypeFallback(key, kMaskOrigin,
                                                                        GrColorType::kAlpha_8, 1)) {
        return GrTextureEffect::Make(std::move(view), kPremul_SkAlphaType, m);
    }

    auto rtc = GrRenderTargetContext::MakeWithFallback(
            context, GrColorType::kAlpha_8, nullptr, SkBackingFit::kExact, dimensions, 1,
            GrMipmapped::kNo, GrProtected::kNo, kMaskOrigin);
    if (!rtc) {
        return nullptr;
    }

    GrPaint paint;

    rtc->clear(SK_PMColor4fTRANSPARENT);
    rtc->drawRRect(nullptr, std::move(paint), GrAA::kYes, SkMatrix::I(), rrectToDraw,
                   GrStyle::SimpleFill());

    GrSurfaceProxyView srcView = rtc->readSurfaceView();
    if (!srcView) {
        return nullptr;
    }
    SkASSERT(srcView.asTextureProxy());
    auto rtc2 = SkGpuBlurUtils::GaussianBlur(context,
                                             std::move(srcView),
                                             rtc->colorInfo().colorType(),
                                             rtc->colorInfo().alphaType(),
                                             nullptr,
                                             SkIRect::MakeSize(dimensions),
                                             SkIRect::MakeSize(dimensions),
                                             xformedSigma,
                                             xformedSigma,
                                             SkTileMode::kClamp,
                                             SkBackingFit::kExact);
    if (!rtc2) {
        return nullptr;
    }

    GrSurfaceProxyView mask = rtc2->readSurfaceView();
    if (!mask) {
        return nullptr;
    }
    SkASSERT(mask.asTextureProxy());
    SkASSERT(mask.origin() == kMaskOrigin);
    proxyProvider->assignUniqueKeyToProxy(key, mask.asTextureProxy());
    return GrTextureEffect::Make(std::move(mask), kPremul_SkAlphaType, m);
}

std::unique_ptr<GrFragmentProcessor> GrRRectBlurEffect::Make(
        std::unique_ptr<GrFragmentProcessor> inputFP,
        GrRecordingContext* context,
        float sigma,
        float xformedSigma,
        const SkRRect& srcRRect,
        const SkRRect& devRRect) {
    SkASSERT(!SkRRectPriv::IsCircle(devRRect) &&
             !devRRect.isRect());  // Should've been caught up-stream

    // TODO: loosen this up
    if (!SkRRectPriv::IsSimpleCircular(devRRect)) {
        return nullptr;
    }

    // Make sure we can successfully ninepatch this rrect -- the blur sigma has to be
    // sufficiently small relative to both the size of the corner radius and the
    // width (and height) of the rrect.
    SkRRect rrectToDraw;
    SkISize dimensions;
    SkScalar ignored[kSkBlurRRectMaxDivisions];

    bool ninePatchable =
            SkComputeBlurredRRectParams(srcRRect, devRRect, sigma, xformedSigma, &rrectToDraw,
                                        &dimensions, ignored, ignored, ignored, ignored);
    if (!ninePatchable) {
        return nullptr;
    }

    std::unique_ptr<GrFragmentProcessor> maskFP =
            find_or_create_rrect_blur_mask_fp(context, rrectToDraw, dimensions, xformedSigma);
    if (!maskFP) {
        return nullptr;
    }

    return std::unique_ptr<GrFragmentProcessor>(
            new GrRRectBlurEffect(std::move(inputFP), xformedSigma, devRRect.getBounds(),
                                  SkRRectPriv::GetSimpleRadii(devRRect).fX, std::move(maskFP)));
}
#include "src/core/SkUtils.h"
#include "src/gpu/GrTexture.h"
#include "src/gpu/glsl/GrGLSLFragmentProcessor.h"
#include "src/gpu/glsl/GrGLSLFragmentShaderBuilder.h"
#include "src/gpu/glsl/GrGLSLProgramBuilder.h"
#include "src/sksl/SkSLCPP.h"
#include "src/sksl/SkSLUtil.h"
class GrGLSLRRectBlurEffect : public GrGLSLFragmentProcessor {
public:
    GrGLSLRRectBlurEffect() {}
    void emitCode(EmitArgs& args) override {
        GrGLSLFPFragmentBuilder* fragBuilder = args.fFragBuilder;
        const GrRRectBlurEffect& _outer = args.fFp.cast<GrRRectBlurEffect>();
        (void)_outer;
        auto sigma = _outer.sigma;
        (void)sigma;
        auto rect = _outer.rect;
        (void)rect;
        auto cornerRadius = _outer.cornerRadius;
        (void)cornerRadius;
        cornerRadiusVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag,
                                                           kHalf_GrSLType, "cornerRadius");
        proxyRectVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag,
                                                        kFloat4_GrSLType, "proxyRect");
        blurRadiusVar = args.fUniformHandler->addUniform(&_outer, kFragment_GrShaderFlag,
                                                         kHalf_GrSLType, "blurRadius");
        fragBuilder->codeAppendf(
                R"SkSL(half2 translatedFragPos = half2(sk_FragCoord.xy - %s.xy);
half2 proxyCenter = half2((%s.zw - %s.xy) * 0.5);
half edgeSize = (2.0 * %s + %s) + 0.5;
translatedFragPos -= proxyCenter;
half2 fragDirection = sign(translatedFragPos);
translatedFragPos = abs(translatedFragPos);
translatedFragPos -= proxyCenter - edgeSize;
translatedFragPos = max(translatedFragPos, 0.0);
translatedFragPos *= fragDirection;
translatedFragPos += half2(edgeSize);
half2 proxyDims = half2(2.0 * edgeSize);
half2 texCoord = translatedFragPos / proxyDims;)SkSL",
                args.fUniformHandler->getUniformCStr(proxyRectVar),
                args.fUniformHandler->getUniformCStr(proxyRectVar),
                args.fUniformHandler->getUniformCStr(proxyRectVar),
                args.fUniformHandler->getUniformCStr(blurRadiusVar),
                args.fUniformHandler->getUniformCStr(cornerRadiusVar));
        SkString _sample9345 = this->invokeChild(0, args);
        fragBuilder->codeAppendf(
                R"SkSL(
half4 inputColor = %s;)SkSL",
                _sample9345.c_str());
        SkString _coords9393("float2(texCoord)");
        SkString _sample9393 = this->invokeChild(1, args, _coords9393.c_str());
        fragBuilder->codeAppendf(
                R"SkSL(
%s = inputColor * %s;
)SkSL",
                args.fOutputColor, _sample9393.c_str());
    }

private:
    void onSetData(const GrGLSLProgramDataManager& pdman,
                   const GrFragmentProcessor& _proc) override {
        const GrRRectBlurEffect& _outer = _proc.cast<GrRRectBlurEffect>();
        { pdman.set1f(cornerRadiusVar, (_outer.cornerRadius)); }
        auto sigma = _outer.sigma;
        (void)sigma;
        auto rect = _outer.rect;
        (void)rect;
        UniformHandle& cornerRadius = cornerRadiusVar;
        (void)cornerRadius;
        UniformHandle& proxyRect = proxyRectVar;
        (void)proxyRect;
        UniformHandle& blurRadius = blurRadiusVar;
        (void)blurRadius;

        float blurRadiusValue = 3.f * SkScalarCeilToScalar(sigma - 1 / 6.0f);
        pdman.set1f(blurRadius, blurRadiusValue);

        SkRect outset = rect;
        outset.outset(blurRadiusValue, blurRadiusValue);
        pdman.set4f(proxyRect, outset.fLeft, outset.fTop, outset.fRight, outset.fBottom);
    }
    UniformHandle proxyRectVar;
    UniformHandle blurRadiusVar;
    UniformHandle cornerRadiusVar;
};
GrGLSLFragmentProcessor* GrRRectBlurEffect::onCreateGLSLInstance() const {
    return new GrGLSLRRectBlurEffect();
}
void GrRRectBlurEffect::onGetGLSLProcessorKey(const GrShaderCaps& caps,
                                              GrProcessorKeyBuilder* b) const {}
bool GrRRectBlurEffect::onIsEqual(const GrFragmentProcessor& other) const {
    const GrRRectBlurEffect& that = other.cast<GrRRectBlurEffect>();
    (void)that;
    if (sigma != that.sigma) return false;
    if (rect != that.rect) return false;
    if (cornerRadius != that.cornerRadius) return false;
    return true;
}
GrRRectBlurEffect::GrRRectBlurEffect(const GrRRectBlurEffect& src)
        : INHERITED(kGrRRectBlurEffect_ClassID, src.optimizationFlags())
        , sigma(src.sigma)
        , rect(src.rect)
        , cornerRadius(src.cornerRadius) {
    this->cloneAndRegisterAllChildProcessors(src);
}
std::unique_ptr<GrFragmentProcessor> GrRRectBlurEffect::clone() const {
    return std::make_unique<GrRRectBlurEffect>(*this);
}
#if GR_TEST_UTILS
SkString GrRRectBlurEffect::onDumpInfo() const {
    return SkStringPrintf("(sigma=%f, rect=float4(%f, %f, %f, %f), cornerRadius=%f)", sigma,
                          rect.left(), rect.top(), rect.right(), rect.bottom(), cornerRadius);
}
#endif
GR_DEFINE_FRAGMENT_PROCESSOR_TEST(GrRRectBlurEffect);
#if GR_TEST_UTILS
std::unique_ptr<GrFragmentProcessor> GrRRectBlurEffect::TestCreate(GrProcessorTestData* d) {
    SkScalar w = d->fRandom->nextRangeScalar(100.f, 1000.f);
    SkScalar h = d->fRandom->nextRangeScalar(100.f, 1000.f);
    SkScalar r = d->fRandom->nextRangeF(1.f, 9.f);
    SkScalar sigma = d->fRandom->nextRangeF(1.f, 10.f);
    SkRRect rrect;
    rrect.setRectXY(SkRect::MakeWH(w, h), r, r);
    return GrRRectBlurEffect::Make(d->inputFP(), d->context(), sigma, sigma, rrect, rrect);
}
#endif
