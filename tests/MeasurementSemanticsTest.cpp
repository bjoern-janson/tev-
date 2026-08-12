#include <tev/Measurement.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "measurement semantic anchor failed: " << message << '\n';
        std::exit(1);
    }
}

bool sameVector(nanogui::Vector2i a, nanogui::Vector2i b) {
    return a.x() == b.x() && a.y() == b.y();
}

bool sameBox(const tev::Box2i& a, const tev::Box2i& b) {
    return sameVector(a.min, b.min) && sameVector(a.max, b.max);
}

void requireMeasurementStable(const tev::MeasurementSpec& actual, const tev::MeasurementSpec& expected) {
    require(actual.candidate.has_value() == expected.candidate.has_value(), "candidate presence drifted");
    require(actual.reference.has_value() == expected.reference.has_value(), "reference presence drifted");

    if (actual.candidate && expected.candidate) {
        require(actual.candidate->name == expected.candidate->name, "candidate identity drifted");
        require(actual.candidate->runtimeId == expected.candidate->runtimeId, "candidate runtime id drifted");
    }
    if (actual.reference && expected.reference) {
        require(actual.reference->name == expected.reference->name, "reference identity drifted");
        require(actual.reference->runtimeId == expected.reference->runtimeId, "reference runtime id drifted");
    }

    require(actual.sampling.interactivePolicy == expected.sampling.interactivePolicy, "interactive correspondence policy drifted");
    require(actual.sampling.materializedPolicy == expected.sampling.materializedPolicy, "materialized correspondence policy drifted");
    require(sameVector(actual.sampling.interactiveReferenceOffset, expected.sampling.interactiveReferenceOffset), "interactive correspondence offset drifted");
    require(sameVector(actual.sampling.materializedReferenceOffset, expected.sampling.materializedReferenceOffset), "materialized correspondence offset drifted");
    require(actual.alphaPolicy == expected.alphaPolicy, "alpha policy drifted");
    require(actual.channelGroup == expected.channelGroup, "channel group drifted");
    require(sameBox(actual.region, expected.region), "region drifted");
    require(actual.mask == expected.mask, "channel mask drifted");
    require(actual.metric.kind == expected.metric.kind, "terminal metric drifted");
    require(actual.metric.relativeEpsilon == expected.metric.relativeEpsilon, "metric epsilon drifted");
}

void requireProjection(const tev::MeasurementLineage& lineage, bool usesReference, tev::EMetric effectiveMetric) {
    const auto projection = tev::measurementStageProjection(
        lineage.stage,
        lineage.measurement.metric.kind,
        lineage.measurement.reference.has_value()
    );
    require(projection.usesReference == usesReference, "reference projection is wrong");
    require(projection.effectiveMetric == effectiveMetric, "metric projection is wrong");
}

} // namespace

int main() {
    using namespace tev;

    MeasurementLineage lineage;
    lineage.measurement.candidate = MeasurementInputRef{"candidate.exr", 11};
    lineage.measurement.reference = MeasurementInputRef{"reference.exr", 22};
    lineage.measurement.channelGroup = "RGB";
    lineage.measurement.mask = EChannelMask::Red | EChannelMask::Green | EChannelMask::Blue;
    lineage.measurement.region = Box2i{nanogui::Vector2i{100, 40}, nanogui::Vector2i{104, 44}};
    lineage.measurement.metric.kind = EMetric::RelativeSquaredError;

    const Box2i candidateWindow{nanogui::Vector2i{100, 40}, nanogui::Vector2i{104, 44}};
    const Box2i referenceWindow{nanogui::Vector2i{0, 0}, nanogui::Vector2i{4, 4}};
    lineage.measurement.sampling = measurementSamplingSpec(candidateWindow, referenceWindow);

    require(sameVector(lineage.measurement.sampling.interactiveReferenceOffset, nanogui::Vector2i{100, 40}),
        "interactive correspondence did not preserve data-window coordinates");
    require(sameVector(lineage.measurement.sampling.materializedReferenceOffset, nanogui::Vector2i{0, 0}),
        "materialized correspondence did not preserve centered image extents");
    require(!measurementCorrespondencePathsAgree(lineage.measurement.sampling),
        "fixture must force T_interactive != T_materialized");

    lineage.stage = MeasurementStage::MetricOutput;
    const MeasurementSpec terminalMeasurement = lineage.measurement;

    requireProjection(lineage, true, EMetric::RelativeSquaredError);
    requireMeasurementStable(lineage.measurement, terminalMeasurement);

    lineage.stage = *measurementUpstreamStage(lineage.stage);
    require(lineage.stage == MeasurementStage::SignedDifference, "metric -> signed difference return failed");
    requireProjection(lineage, true, EMetric::Error);
    requireMeasurementStable(lineage.measurement, terminalMeasurement);

    lineage.stage = *measurementUpstreamStage(lineage.stage);
    require(lineage.stage == MeasurementStage::Inputs, "signed difference -> inputs return failed");
    requireProjection(lineage, false, EMetric::RelativeSquaredError);
    requireMeasurementStable(lineage.measurement, terminalMeasurement);

    lineage.stage = *measurementDownstreamStage(lineage.stage);
    require(lineage.stage == MeasurementStage::SignedDifference, "inputs -> signed difference traversal failed");
    requireProjection(lineage, true, EMetric::Error);
    requireMeasurementStable(lineage.measurement, terminalMeasurement);

    lineage.stage = *measurementDownstreamStage(lineage.stage);
    require(lineage.stage == MeasurementStage::MetricOutput, "signed difference -> metric traversal failed");
    requireProjection(lineage, true, EMetric::RelativeSquaredError);
    requireMeasurementStable(lineage.measurement, terminalMeasurement);

    const auto stageBeforeViewChange = lineage.stage;
    lineage.view.exposure = 4.0f;
    lineage.view.offset = -0.25f;
    lineage.view.gamma = 1.8f;
    lineage.view.tonemap = ETonemap::FalseColor;
    require(lineage.stage == stageBeforeViewChange, "view mutation changed stage");
    requireMeasurementStable(lineage.measurement, terminalMeasurement);

    std::cout << "measurement semantic anchor: correspondence divergence + round trip + view isolation OK\n";
    return 0;
}
