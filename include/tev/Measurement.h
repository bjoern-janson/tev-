/*
 * tev++ -- inspectable measurement lineage for tev
 *
 * Copyright (C) 2026 tev++ contributors
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

#include <optional>
#include <string>
#include <string_view>

namespace tev {

// The relative metrics in upstream tev currently use this fixed stabilizer.
// Keeping it in the measurement model makes that otherwise implicit assumption inspectable
// without changing numerical behavior in v0.1.
inline constexpr float MEASUREMENT_DEFAULT_RELATIVE_EPSILON = 0.01f;

enum class MeasurementStage : int {
    Inputs = 0,
    SignedDifference,
    MetricOutput,
};

// A runtime input identity. runtimeId is intentionally not a source revision or provenance
// identifier; it mirrors tev's mutation-sensitive Image::id() only for local disambiguation.
struct MeasurementInputRef {
    std::string name;
    int runtimeId = -1;
};

enum class EMeasurementCorrespondencePolicy : int {
    DataWindowCoordinates = 0,
    CenteredImageExtents,
};

// tev currently has two correspondence paths. Interactive rendering/pixel inspection maps
// samples through data-window coordinates, while materialized CPU comparisons (statistics and
// export) center the image extents. v0.1 records both rather than pretending they are identical.
struct MeasurementSamplingSpec {
    EMeasurementCorrespondencePolicy interactivePolicy = EMeasurementCorrespondencePolicy::DataWindowCoordinates;
    nanogui::Vector2i interactiveReferenceOffset = {0, 0};

    EMeasurementCorrespondencePolicy materializedPolicy = EMeasurementCorrespondencePolicy::CenteredImageExtents;
    nanogui::Vector2i materializedReferenceOffset = {0, 0};
};

inline MeasurementSamplingSpec measurementSamplingSpec(Box2i candidateDataWindow, Box2i referenceDataWindow) {
    MeasurementSamplingSpec result;
    result.interactiveReferenceOffset = candidateDataWindow.min - referenceDataWindow.min;
    result.materializedReferenceOffset = (referenceDataWindow.size() - candidateDataWindow.size()) / 2;
    return result;
}

enum class EMeasurementAlphaPolicy : int {
    AverageCoverage = 0,
};

struct MeasurementMetricSpec {
    EMetric kind = EMetric::Error;
    float relativeEpsilon = MEASUREMENT_DEFAULT_RELATIVE_EPSILON;
};

struct MeasurementSpec {
    std::optional<MeasurementInputRef> candidate;
    std::optional<MeasurementInputRef> reference;
    MeasurementSamplingSpec sampling;
    EMeasurementAlphaPolicy alphaPolicy = EMeasurementAlphaPolicy::AverageCoverage;
    std::string channelGroup;
    Box2i region = Box2i{0};
    EChannelMask mask = EChannelMask::All;
    MeasurementMetricSpec metric;
};

// Presentation-only state. Contract: changing ViewSpec must never mutate MeasurementSpec,
// MeasurementStage, or the numerical semantics of data exposed by a stage.
struct ViewSpec {
    ETonemap tonemap = ETonemap::Gamma;
    float exposure = 0.0f;
    float offset = 0.0f;
    float gamma = 2.2f;
    float brightnessLimit = 8.0f;
    float brightnessLimitSoftness = 1.0f;
    bool clipToLdr = false;
};

// This is a live description of one comparison lineage, not a stored MeasurementResult.
struct MeasurementLineage {
    MeasurementSpec measurement;
    MeasurementStage stage = MeasurementStage::Inputs;
    ViewSpec view;
};

struct MeasurementStageProjection {
    bool usesReference = false;
    EMetric effectiveMetric = EMetric::Error;
};

inline MeasurementStageProjection measurementStageProjection(
    MeasurementStage stage,
    EMetric terminalMetric,
    bool hasReference
) {
    if (!hasReference || stage == MeasurementStage::Inputs) {
        return {false, terminalMetric};
    }

    return {
        true,
        stage == MeasurementStage::SignedDifference ? EMetric::Error : terminalMetric,
    };
}

inline std::optional<MeasurementStage> measurementUpstreamStage(MeasurementStage stage) {
    switch (stage) {
        case MeasurementStage::MetricOutput: return MeasurementStage::SignedDifference;
        case MeasurementStage::SignedDifference: return MeasurementStage::Inputs;
        case MeasurementStage::Inputs: return std::nullopt;
        default: return std::nullopt;
    }
}

inline std::optional<MeasurementStage> measurementDownstreamStage(MeasurementStage stage) {
    switch (stage) {
        case MeasurementStage::Inputs: return MeasurementStage::SignedDifference;
        case MeasurementStage::SignedDifference: return MeasurementStage::MetricOutput;
        case MeasurementStage::MetricOutput: return std::nullopt;
        default: return std::nullopt;
    }
}

inline std::string_view measurementStageName(MeasurementStage stage) {
    switch (stage) {
        case MeasurementStage::Inputs: return "Inputs";
        case MeasurementStage::SignedDifference: return "Signed difference";
        case MeasurementStage::MetricOutput: return "Metric output";
        default: return "Unknown";
    }
}

inline std::string_view measurementMetricName(EMetric metric) {
    switch (metric) {
        case EMetric::Error: return "E";
        case EMetric::AbsoluteError: return "AE";
        case EMetric::SquaredError: return "SE";
        case EMetric::RelativeAbsoluteError: return "RAE";
        case EMetric::RelativeSquaredError: return "RSE";
        default: return "?";
    }
}

inline std::string measurementMetricFormula(const MeasurementMetricSpec& metric) {
    switch (metric.kind) {
        case EMetric::Error: return "C - R";
        case EMetric::AbsoluteError: return "|C - R|";
        case EMetric::SquaredError: return "(C - R)^2";
        case EMetric::RelativeAbsoluteError: return fmt::format("|C - R| / (R + {:g})", metric.relativeEpsilon);
        case EMetric::RelativeSquaredError: return fmt::format("(C - R)^2 / (R^2 + {:g})", metric.relativeEpsilon);
        default: return "unknown";
    }
}

inline std::string_view measurementTonemapName(ETonemap tonemap) {
    switch (tonemap) {
        case ETonemap::Gamma: return "gamma";
        case ETonemap::FalseColor: return "false color";
        case ETonemap::PositiveNegative: return "+/-";
        default: return "unknown";
    }
}

inline std::string measurementStageCaption(const MeasurementLineage& lineage) {
    switch (lineage.stage) {
        case MeasurementStage::Inputs: return "Inputs";
        case MeasurementStage::SignedDifference: return "Signed diff";
        case MeasurementStage::MetricOutput: return fmt::format("Metric: {}", measurementMetricName(lineage.measurement.metric.kind));
        default: return "Measurement";
    }
}

inline std::string measurementUpstreamCaption(const MeasurementLineage& lineage) {
    switch (lineage.stage) {
        case MeasurementStage::MetricOutput: return "Up: diff";
        case MeasurementStage::SignedDifference: return "Up: inputs";
        case MeasurementStage::Inputs: return "Upstream";
        default: return "Upstream";
    }
}

inline std::string measurementOffsetFormula(std::string_view name, nanogui::Vector2i offset) {
    return fmt::format("{}(x,y)=(x{:+d}, y{:+d})", name, offset.x(), offset.y());
}

inline bool measurementCorrespondencePathsAgree(const MeasurementSamplingSpec& sampling) {
    return sampling.interactiveReferenceOffset.x() == sampling.materializedReferenceOffset.x() &&
        sampling.interactiveReferenceOffset.y() == sampling.materializedReferenceOffset.y();
}

inline std::string describeMeasurementLineage(const MeasurementLineage& lineage) {
    const auto candidate = lineage.measurement.candidate ?
        fmt::format("{} [runtime id {}]", lineage.measurement.candidate->name, lineage.measurement.candidate->runtimeId) :
        std::string{"none"};
    const auto reference = lineage.measurement.reference ?
        fmt::format("{} [runtime id {}]", lineage.measurement.reference->name, lineage.measurement.reference->runtimeId) :
        std::string{"none"};

    std::string interactiveSampling = "n/a";
    std::string materializedSampling = "n/a";
    std::string samplingAgreement = "n/a";
    if (lineage.measurement.reference) {
        interactiveSampling = fmt::format(
            "{} [data-window coordinates]",
            measurementOffsetFormula("T_display", lineage.measurement.sampling.interactiveReferenceOffset)
        );
        materializedSampling = fmt::format(
            "{} [centered image extents]",
            measurementOffsetFormula("T_materialized", lineage.measurement.sampling.materializedReferenceOffset)
        );
        samplingAgreement = measurementCorrespondencePathsAgree(lineage.measurement.sampling) ? "agree" : "DIFFER";
    }

    return fmt::format(
        "Stage: {}\n"
        "Candidate: {}\n"
        "Reference: {}\n"
        "Correspondence (display/pixel inspection): {}\n"
        "Correspondence (statistics/export): {}\n"
        "Correspondence paths: {}\n"
        "Sample defaults: non-alpha missing/out-of-bounds -> 0; alpha has tev's existing coverage defaults\n"
        "Alpha policy: average candidate/reference coverage; selected metric is not applied to alpha\n"
        "Channels: {} (mask={})\n"
        "Region: {}\n"
        "Metric: {} = {}\n\n"
        "View (presentation only): tonemap={}, exposure={:+g}, offset={:+g}, gamma={:g}, brightness-limit={:g}, rolloff={:g}, clip-LDR={}",
        measurementStageName(lineage.stage),
        candidate,
        reference,
        interactiveSampling,
        materializedSampling,
        samplingAgreement,
        lineage.measurement.channelGroup.empty() ? "<none>" : lineage.measurement.channelGroup,
        static_cast<int>(lineage.measurement.mask),
        lineage.measurement.region,
        measurementMetricName(lineage.measurement.metric.kind),
        measurementMetricFormula(lineage.measurement.metric),
        measurementTonemapName(lineage.view.tonemap),
        lineage.view.exposure,
        lineage.view.offset,
        lineage.view.gamma,
        lineage.view.brightnessLimit,
        lineage.view.brightnessLimitSoftness,
        lineage.view.clipToLdr ? "yes" : "no"
    );
}

} // namespace tev
