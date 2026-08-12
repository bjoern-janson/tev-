/*
 * tev -- the EDR viewer
 *
 * Copyright (C) 2025 Thomas Müller <contact@tom94.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <tev/Box.h>
#include <tev/Common.h>
#include <tev/Image.h>
#include <tev/Lazy.h>
#include <tev/Measurement.h>
#include <tev/UberShader.h>

#include <nanogui/canvas.h>

#include <memory>
#include <optional>
#include <unordered_map>

namespace tev {

struct CanvasStatistics {
    float mean;
    float maximum;
    float minimum;
    std::vector<float> histogram;
    std::vector<nanogui::Color> histogramColors;
    int nChannels;
    int histogramZero;
};

class ImageCanvas final : public nanogui::Canvas {
public:
    ImageCanvas(nanogui::Widget* parent);

    bool scroll_event(const nanogui::Vector2i& p, const nanogui::Vector2f& rel) override;

    void draw_contents() override;

    void draw(NVGcontext* ctx) override;

    void translate(nanogui::Vector2f amount);
    void scale(float amount, nanogui::Vector2f origin);
    float scale() const { return extractScale(mTransform); }

    float exposure() const { return mExposure; }
    void setExposure(float exposure) { mExposure = exposure; }
    float offset() const { return mOffset; }
    void setOffset(float offset) { mOffset = offset; }
    float gamma() const { return mGamma; }
    void setGamma(float gamma) { mGamma = gamma; }
    float brightnessLimit() const { return mBrightnessLimit; }
    void setBrightnessLimit(float max) { mBrightnessLimit = max; }
    float brightnessLimitSoftness() const { return mBrightnessLimitSoftness; }
    void setBrightnessLimitSoftness(float value) { mBrightnessLimitSoftness = value; }

    void setImage(std::shared_ptr<Image> image) {
        mImage = image;
        mMeasurementStage = mMeasurementReference ? MeasurementStage::MetricOutput : MeasurementStage::Inputs;
        syncMeasurementStage();
    }
    void setReference(std::shared_ptr<Image> reference) {
        mMeasurementReference = reference;
        mMeasurementStage = reference ? MeasurementStage::MetricOutput : MeasurementStage::Inputs;
        syncMeasurementStage();
    }
    void setRequestedChannelGroup(std::string_view groupName) { mRequestedChannelGroup = groupName; }

    nanogui::Vector2i getImageCoords(const Image* image, nanogui::Vector2i mousePos);
    nanogui::Vector2i getDisplayWindowCoords(const Image* image, nanogui::Vector2i mousePos);

    void getValuesAtNanoPos(nanogui::Vector2i nanoPos, std::vector<float>& result, std::span<std::string_view> channels);
    std::vector<float> getValuesAtNanoPos(nanogui::Vector2i nanoPos, std::span<std::string_view> channels) {
        std::vector<float> result;
        getValuesAtNanoPos(nanoPos, result, channels);
        return result;
    }

    ETonemap tonemap() const { return mTonemap; }
    void setTonemap(ETonemap tonemap) { mTonemap = tonemap; }

    EMetric metric() const { return mMeasurementMetric; }
    void setMetric(EMetric metric) {
        mMeasurementMetric = metric;
        mMeasurementStage = mMeasurementReference ? MeasurementStage::MetricOutput : MeasurementStage::Inputs;
        syncMeasurementStage();
    }

    MeasurementStage measurementStage() const { return mMeasurementStage; }
    void setMeasurementStage(MeasurementStage stage) {
        if (!mMeasurementReference && stage != MeasurementStage::Inputs) {
            stage = MeasurementStage::Inputs;
        }

        mMeasurementStage = stage;
        syncMeasurementStage();
    }

    bool showUpstream() {
        if (!mMeasurementReference) {
            return false;
        }

        const auto upstream = measurementUpstreamStage(mMeasurementStage);
        if (!upstream) {
            return false;
        }

        setMeasurementStage(*upstream);
        return true;
    }

    MeasurementLineage measurementLineage() const {
        MeasurementLineage lineage;
        if (mImage) {
            lineage.measurement.candidate = MeasurementInputRef{std::string{mImage->name()}, mImage->id()};
            lineage.measurement.region = cropInImageCoords();
        }
        if (mMeasurementReference) {
            lineage.measurement.reference = MeasurementInputRef{std::string{mMeasurementReference->name()}, mMeasurementReference->id()};
            if (mImage) {
                lineage.measurement.sampling = measurementSamplingSpec(mImage->dataWindow(), mMeasurementReference->dataWindow());
            }
        }
        lineage.measurement.channelGroup = mRequestedChannelGroup;
        lineage.measurement.mask = mChannelMask;
        lineage.measurement.metric.kind = mMeasurementMetric;
        lineage.stage = mMeasurementStage;
        lineage.view.tonemap = mTonemap;
        lineage.view.exposure = mExposure;
        lineage.view.offset = mOffset;
        lineage.view.gamma = mGamma;
        lineage.view.brightnessLimit = mBrightnessLimit;
        lineage.view.brightnessLimitSoftness = mBrightnessLimitSoftness;
        lineage.view.clipToLdr = mClipToLdr;
        return lineage;
    }

    bool areChannelsMasked(EChannelMask mask) const { return hasFlag(mChannelMask, mask); }
    EChannelMask channelMask() const { return mChannelMask; }
    void setChannelMask(EChannelMask mask) { mChannelMask = mask; }

    const std::optional<Box2i>& crop() const & { return mCrop; }
    void setCrop(const std::optional<Box2i>& crop) { mCrop = crop; }
    Box2i cropInImageCoords() const;

    void fitImageToScreen(const Image& image);
    void resetTransform();

    std::optional<float> whiteLevelOverride() const { return mWhiteLevelOverride; }
    void setWhiteLevelOverride(std::optional<float> value) { mWhiteLevelOverride = value; }

    bool clipToLdr() const { return mClipToLdr; }
    void setClipToLdr(bool value) { mClipToLdr = value; }

    auto backgroundColor() { return mBackgroundColor; }
    void setBackgroundColor(const nanogui::Color& color) { mBackgroundColor = color; }

    EInterpolationMode minFilter() const { return mMinFilter; }
    void setMinFilter(EInterpolationMode value) { mMinFilter = value; }

    EInterpolationMode magFilter() const { return mMagFilter; }
    void setMagFilter(EInterpolationMode value) { mMagFilter = value; }

    // The following functions return four values per pixel in RGBA order. The number of pixels is given by `imageDataSize()`. If the canvas
    // does not currently hold an image, or no channels are displayed, then zero pixels are returned.
    nanogui::Vector2i imageDataSize() const { return cropInImageCoords().size(); }
    Task<HeapArray<float>> getRgbaHdrImageData(bool divideAlpha, int priority) const;
    Task<HeapArray<uint8_t>> getRgbaLdrImageData(bool divideAlpha, int priority) const;

    void saveImage(const fs::path& filename) const;

    std::shared_ptr<Lazy<std::shared_ptr<CanvasStatistics>>> canvasStatistics();

    void purgeCanvasStatistics(size_t imageId);

    float pixelRatio() const { return mPixelRatio; }
    void setPixelRatio(float ratio) { mPixelRatio = ratio; }

    chroma_t inspectionChroma() const { return mInspectionChroma; }
    void setInspectionChroma(const chroma_t& chroma) { mInspectionChroma = chroma; }

    ituth273::ETransfer inspectionTransfer() const { return mInspectionTransfer; }
    void setInspectionTransfer(const ituth273::ETransfer transfer) { mInspectionTransfer = transfer; }

    bool inspectionAdaptWhitePoint() const { return mInspectionAdaptWhitePoint; }
    void setInspectionAdaptWhitePoint(bool adapt) { mInspectionAdaptWhitePoint = adapt; }

    bool inspectionPremultipliedAlpha() const { return mInspectionPremultipliedAlpha; }
    void setInspectionPremultipliedAlpha(bool premultipied) { mInspectionPremultipliedAlpha = premultipied; }

    // Assumes the alpha channel is the last one, if present
    void applyInspectionParameters(std::vector<float>& values, bool hasAlpha);

private:
    // MeasurementSpec is kept separately from the effective render/statistics fields below.
    // Moving upstream changes only the exposed stage; it does not destroy the selected
    // terminal metric or reference that define the comparison.
    void syncMeasurementStage() {
        const auto projection = measurementStageProjection(
            mMeasurementStage,
            mMeasurementMetric,
            static_cast<bool>(mMeasurementReference)
        );
        mReference = projection.usesReference ? mMeasurementReference : nullptr;
        mMetric = projection.effectiveMetric;
    }

    static Task<std::shared_ptr<CanvasStatistics>> computeCanvasStatistics(
        std::shared_ptr<Image> image,
        std::shared_ptr<Image> reference,
        std::string_view requestedChannelGroup,
        EMetric metric,
        Box2i region,
        const chroma_t& chroma,
        ituth273::ETransfer transfer,
        bool adaptWhitePoint,
        bool premultipliedAlpha,
        int priority
    );

    void drawPixelValuesAsText(NVGcontext* ctx);
    void drawCoordinateSystem(NVGcontext* ctx);
    void drawEdgeShadows(NVGcontext* ctx);

    nanogui::Vector2f pixelOffset(nanogui::Vector2i size) const;

    // Assembles the transform from canonical space to the [-1, 1] square for the current image.
    nanogui::Matrix3f transform(const Image* image);
    nanogui::Matrix3f textureToNanogui(const Image* image);
    nanogui::Matrix3f displayWindowToNanogui(const Image* image);

    float mPixelRatio = 1;
    float mExposure = 0;
    float mOffset = 0;
    float mGamma = 2.2f;
    float mBrightnessLimit = 8.0f;
    float mBrightnessLimitSoftness = 1.0f;

    std::optional<float> mWhiteLevelOverride = std::nullopt;
    bool mClipToLdr = false;
    nanogui::Color mBackgroundColor = nanogui::Color(0, 0, 0, 0);

    EInterpolationMode mMinFilter = EInterpolationMode::Trilinear;
    EInterpolationMode mMagFilter = EInterpolationMode::Nearest;

    std::shared_ptr<Image> mImage;
    std::shared_ptr<Image> mReference;
    std::shared_ptr<Image> mMeasurementReference;
    EMetric mMeasurementMetric = EMetric::Error;
    MeasurementStage mMeasurementStage = MeasurementStage::Inputs;

    std::string mRequestedChannelGroup = "";

    nanogui::Matrix3f mTransform = nanogui::Matrix3f::scale(nanogui::Vector3f(1.0f));

    std::unique_ptr<UberShader> mShader;

    EChannelMask mChannelMask = EChannelMask::Red | EChannelMask::Green | EChannelMask::Blue | EChannelMask::Alpha;
    ETonemap mTonemap = ETonemap::Gamma;
    EMetric mMetric = EMetric::Error;
    std::optional<Box2i> mCrop;

    std::unordered_map<std::string, std::shared_ptr<Lazy<std::shared_ptr<CanvasStatistics>>>> mCanvasStatistics;
    std::unordered_map<int, std::vector<std::string>> mImageIdToCanvasStatisticsKey;

    chroma_t mInspectionChroma = rec709Chroma();
    ituth273::ETransfer mInspectionTransfer = ituth273::ETransfer::Linear;
    bool mInspectionAdaptWhitePoint = false;
    bool mInspectionPremultipliedAlpha = true;
};

} // namespace tev
