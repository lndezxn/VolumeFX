#include "Labs/5-Visualization/tasks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>

#include <glm/glm.hpp>

using VCX::Labs::Common::ImageRGB;
namespace VCX::Labs::Visualization {

    struct CoordinateStates {
        static constexpr std::size_t AxisCount = 7;

        std::array<char const *, AxisCount> axisLabels = { "MPG", "Cylinders", "Displacement", "Horsepower", "Weight", "0-60 mph", "Year" };
        std::array<int, AxisCount>          axisPrecisions = { 1, 0, 0, 0, 0, 2, 0 };
        std::array<float, AxisCount>        minValues {};
        std::array<float, AxisCount>        maxValues {};
        int                                 highlightedIndex = -1;

        CoordinateStates() {
            minValues.fill(0.f);
            maxValues.fill(1.f);
        }

        static float ReadAttribute(Car const & car, std::size_t axis) {
            switch (axis) {
                case 0: return car.mileage;
                case 1: return static_cast<float>(car.cylinders);
                case 2: return car.displacement;
                case 3: return car.horsepower;
                case 4: return car.weight;
                case 5: return car.acceleration;
                case 6: return static_cast<float>(car.year);
                default: return 0.f;
            }
        }

        void UpdateRanges(std::vector<Car> const & data) {
            if (data.empty()) {
                minValues.fill(0.f);
                maxValues.fill(1.f);
                return;
            }

            minValues.fill(std::numeric_limits<float>::max());
            maxValues.fill(std::numeric_limits<float>::lowest());
            for (auto const & car : data) {
                for (std::size_t axis = 0; axis < AxisCount; ++axis) {
                    float value = ReadAttribute(car, axis);
                    minValues[axis] = std::min(minValues[axis], value);
                    maxValues[axis] = std::max(maxValues[axis], value);
                }
            }

            for (std::size_t axis = 0; axis < AxisCount; ++axis) {
                if (! std::isfinite(minValues[axis]) || ! std::isfinite(maxValues[axis])) {
                    minValues[axis] = 0.f;
                    maxValues[axis] = 1.f;
                    continue;
                }
                if (std::abs(maxValues[axis] - minValues[axis]) < 1e-4f) {
                    maxValues[axis] = minValues[axis] + 1.f;
                }
            }
        }

        std::array<float, AxisCount> NormalizeCar(Car const & car) const {
            std::array<float, AxisCount> normalized {};
            for (std::size_t axis = 0; axis < AxisCount; ++axis) {
                float value = ReadAttribute(car, axis);
                float range = maxValues[axis] - minValues[axis];
                float factor = range < 1e-4f ? 0.5f : (value - minValues[axis]) / range;
                normalized[axis] = std::clamp(factor, 0.f, 1.f);
            }
            return normalized;
        }

        glm::vec2 AxisPoint(std::size_t axisIndex, float normalizedValue, glm::vec2 origin, float axisSpacing, float axisHeight) const {
            float x = origin.x + axisSpacing * static_cast<float>(axisIndex);
            float y = origin.y + (1.f - normalizedValue) * axisHeight;
            return glm::vec2(x, y);
        }

        static float SegmentDistance(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
            glm::vec2 ab = b - a;
            float      len2 = glm::dot(ab, ab);
            if (len2 < 1e-8f) return glm::length(p - a);
            float t = glm::dot(p - a, ab) / len2;
            t       = std::clamp(t, 0.f, 1.f);
            glm::vec2 projection = a + t * ab;
            return glm::length(p - projection);
        }

        void UpdateHighlight(InteractProxy const & proxy,
                             glm::vec2 origin,
                             float axisSpacing,
                             float axisHeight,
                             std::vector<std::array<float, AxisCount>> const & normalized) {
            highlightedIndex = -1;
            if (! proxy.IsHovering() || normalized.empty()) return;

            glm::vec2 mouse = proxy.MousePos();
            float     bestDistance = std::numeric_limits<float>::max();
            int       bestIndex = -1;

            for (std::size_t row = 0; row < normalized.size(); ++row) {
                for (std::size_t axis = 0; axis + 1 < AxisCount; ++axis) {
                    glm::vec2 p0 = AxisPoint(axis, normalized[row][axis], origin, axisSpacing, axisHeight);
                    glm::vec2 p1 = AxisPoint(axis + 1, normalized[row][axis + 1], origin, axisSpacing, axisHeight);
                    float distance = SegmentDistance(mouse, p0, p1);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestIndex    = static_cast<int>(row);
                    }
                }
            }

            constexpr float hoverThreshold = 0.018f;
            if (bestDistance <= hoverThreshold) highlightedIndex = bestIndex;
        }

        std::string FormatValue(float value, std::size_t axis) const {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(axisPrecisions[axis]) << value;
            return oss.str();
        }
    };

    bool PaintParallelCoordinates(Common::ImageRGB & input, InteractProxy const & proxy, std::vector<Car> const & data, bool force) {
        static CoordinateStates states;
        (void)force; // always repaint for clarity

        states.UpdateRanges(data);

        std::vector<std::array<float, CoordinateStates::AxisCount>> normalizedValues;
        normalizedValues.reserve(data.size());
        for (auto const & car : data) normalizedValues.emplace_back(states.NormalizeCar(car));

        glm::vec2 plotOrigin = glm::vec2(0.08f, 0.1f);
        glm::vec2 plotSize   = glm::vec2(0.84f, 0.8f);
        float      axisSpacing = plotSize.x / static_cast<float>(CoordinateStates::AxisCount - 1);
        float      axisHeight  = plotSize.y;

        states.UpdateHighlight(proxy, plotOrigin, axisSpacing, axisHeight, normalizedValues);

        SetBackGround(input, glm::vec4(0.08f, 0.09f, 0.12f, 1.f));
        DrawRect(input, glm::vec4(1.f, 1.f, 1.f, 0.08f), plotOrigin, plotSize, 2.f);

        constexpr int horizontalDivisions = 5;
        for (int i = 0; i <= horizontalDivisions; ++i) {
            float      t = static_cast<float>(i) / horizontalDivisions;
            float      y = plotOrigin.y + t * axisHeight;
            glm::vec2  start(plotOrigin.x, y);
            glm::vec2  end(plotOrigin.x + plotSize.x, y);
            float      alpha = i == 0 || i == horizontalDivisions ? 0.25f : 0.12f;
            DrawLine(input, glm::vec4(0.5f, 0.52f, 0.6f, alpha), start, end, 1.f);
        }

        for (std::size_t axis = 0; axis < CoordinateStates::AxisCount; ++axis) {
            float      x = plotOrigin.x + axisSpacing * static_cast<float>(axis);
            glm::vec2  top(x, plotOrigin.y);
            glm::vec2  bottom(x, plotOrigin.y + axisHeight);
            DrawLine(input, glm::vec4(0.78f, 0.79f, 0.85f, 0.55f), top, bottom, 1.8f);

            PrintText(input, glm::vec4(0.95f, 0.96f, 1.f, 0.9f), glm::vec2(x, plotOrigin.y - 0.035f), 0.024f, states.axisLabels[axis]);
            std::string maxCaption = states.FormatValue(states.maxValues[axis], axis);
            std::string minCaption = states.FormatValue(states.minValues[axis], axis);
            PrintText(input, glm::vec4(0.75f, 0.77f, 0.85f, 0.9f), glm::vec2(x, plotOrigin.y - 0.01f), 0.018f, maxCaption);
            PrintText(input, glm::vec4(0.75f, 0.77f, 0.85f, 0.9f), glm::vec2(x, plotOrigin.y + axisHeight + 0.02f), 0.018f, minCaption);
        }

        if (! normalizedValues.empty()) {
            for (std::size_t row = 0; row < normalizedValues.size(); ++row) {
                bool  isHighlighted = states.highlightedIndex == static_cast<int>(row);
                bool  dimLine = states.highlightedIndex != -1 && ! isHighlighted;
                float width   = isHighlighted ? 3.0f : 1.4f;
                float alpha   = dimLine ? 0.08f : (isHighlighted ? 0.95f : 0.28f);

                float yearFactor = normalizedValues[row][CoordinateStates::AxisCount - 1];
                glm::vec3 coolColor(0.16f, 0.64f, 0.93f);
                glm::vec3 warmColor(0.98f, 0.46f, 0.35f);
                glm::vec3 rgb = glm::mix(coolColor, warmColor, yearFactor);

                for (std::size_t axis = 0; axis + 1 < CoordinateStates::AxisCount; ++axis) {
                    glm::vec2 p0 = states.AxisPoint(axis, normalizedValues[row][axis], plotOrigin, axisSpacing, axisHeight);
                    glm::vec2 p1 = states.AxisPoint(axis + 1, normalizedValues[row][axis + 1], plotOrigin, axisSpacing, axisHeight);
                    DrawLine(input, glm::vec4(rgb, alpha), p0, p1, width);
                }

                if (isHighlighted) {
                    for (std::size_t axis = 0; axis < CoordinateStates::AxisCount; ++axis) {
                        glm::vec2 anchor = states.AxisPoint(axis, normalizedValues[row][axis], plotOrigin, axisSpacing, axisHeight);
                        DrawFilledCircle(input, glm::vec4(rgb, 0.9f), anchor, 0.006f);
                    }
                }
            }
        }

        if (states.highlightedIndex >= 0 && states.highlightedIndex < static_cast<int>(data.size())) {
            Car const & car = data[states.highlightedIndex];

            glm::vec2 panelSize(0.26f, 0.12f);
            glm::vec2 panelTopLeft(plotOrigin.x + plotSize.x + 0.02f, plotOrigin.y + 0.02f);
            panelTopLeft.x = std::min(panelTopLeft.x, 0.98f - panelSize.x);
            panelTopLeft.y = std::min(panelTopLeft.y, 0.98f - panelSize.y);

            DrawFilledRect(input, glm::vec4(0.05f, 0.07f, 0.11f, 0.82f), panelTopLeft, panelSize);
            DrawRect(input, glm::vec4(1.f, 1.f, 1.f, 0.08f), panelTopLeft, panelSize, 1.5f);

            glm::vec2 infoAnchor = panelTopLeft + glm::vec2(panelSize.x * 0.5f, 0.03f);

            std::ostringstream title;
            title << "Car #" << (states.highlightedIndex + 1) << " — Year " << car.year;
            PrintText(input, glm::vec4(1.f, 0.98f, 0.88f, 1.f), infoAnchor, 0.024f, title.str());

            std::ostringstream lineA;
            lineA << states.FormatValue(car.mileage, 0) << " mpg | " << car.cylinders << " cyl | ";
            lineA << states.FormatValue(car.horsepower, 3) << " hp";
            PrintText(input, glm::vec4(0.9f, 0.92f, 0.95f, 1.f), infoAnchor + glm::vec2(0.f, 0.03f), 0.02f, lineA.str());

            std::ostringstream lineB;
            lineB << states.FormatValue(car.weight, 4) << " lb | disp " << states.FormatValue(car.displacement, 2);
            lineB << " ci | 0-60 " << states.FormatValue(car.acceleration, 5) << " s";
            PrintText(input, glm::vec4(0.9f, 0.92f, 0.95f, 1.f), infoAnchor + glm::vec2(0.f, 0.056f), 0.02f, lineB.str());
        }

        return true;
    }

    void LIC(ImageRGB & output, Common::ImageRGB const & noise, VectorField2D const & field, int const & step) {
        auto const width  = static_cast<int>(noise.GetSizeX());
        auto const height = static_cast<int>(noise.GetSizeY());

        if (output.GetSizeX() != noise.GetSizeX() || output.GetSizeY() != noise.GetSizeY()) {
            output = ImageRGB(noise.GetSizeX(), noise.GetSizeY());
        }

        if (field.size.first == 0 || field.size.second == 0 || width == 0 || height == 0) return;

        auto SampleVector = [&](glm::vec2 const & pos) {
            float x = std::clamp(pos.x, 0.f, static_cast<float>(field.size.first - 1));
            float y = std::clamp(pos.y, 0.f, static_cast<float>(field.size.second - 1));

            int x0 = static_cast<int>(std::floor(x));
            int y0 = static_cast<int>(std::floor(y));
            int x1 = std::min(x0 + 1, static_cast<int>(field.size.first - 1));
            int y1 = std::min(y0 + 1, static_cast<int>(field.size.second - 1));

            float tx = x - static_cast<float>(x0);
            float ty = y - static_cast<float>(y0);

            glm::vec2 v00 = field.At(static_cast<uint32_t>(x0), static_cast<uint32_t>(y0));
            glm::vec2 v10 = field.At(static_cast<uint32_t>(x1), static_cast<uint32_t>(y0));
            glm::vec2 v01 = field.At(static_cast<uint32_t>(x0), static_cast<uint32_t>(y1));
            glm::vec2 v11 = field.At(static_cast<uint32_t>(x1), static_cast<uint32_t>(y1));

            glm::vec2 v0 = glm::mix(v00, v10, tx);
            glm::vec2 v1 = glm::mix(v01, v11, tx);
            return glm::mix(v0, v1, ty);
        };

        auto SampleNoise = [&](glm::vec2 const & pos) {
            float x = std::clamp(pos.x, 0.f, static_cast<float>(width - 1));
            float y = std::clamp(pos.y, 0.f, static_cast<float>(height - 1));

            int x0 = static_cast<int>(std::floor(x));
            int y0 = static_cast<int>(std::floor(y));
            int x1 = std::min(x0 + 1, width - 1);
            int y1 = std::min(y0 + 1, height - 1);

            float tx = x - static_cast<float>(x0);
            float ty = y - static_cast<float>(y0);

            glm::vec3 c00 = noise.At(static_cast<std::size_t>(x0), static_cast<std::size_t>(y0));
            glm::vec3 c10 = noise.At(static_cast<std::size_t>(x1), static_cast<std::size_t>(y0));
            glm::vec3 c01 = noise.At(static_cast<std::size_t>(x0), static_cast<std::size_t>(y1));
            glm::vec3 c11 = noise.At(static_cast<std::size_t>(x1), static_cast<std::size_t>(y1));

            glm::vec3 c0 = glm::mix(c00, c10, tx);
            glm::vec3 c1 = glm::mix(c01, c11, tx);
            glm::vec3 color = glm::mix(c0, c1, ty);
            constexpr glm::vec3 luminanceWeights(0.299f, 0.587f, 0.114f);
            return glm::dot(color, luminanceWeights);
        };

        auto Inside = [&](glm::vec2 const & pos) {
            return pos.x >= 0.f && pos.y >= 0.f && pos.x <= static_cast<float>(width - 1) && pos.y <= static_cast<float>(height - 1);
        };

        int kernelSteps = std::max(1, step);

        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) {
                glm::vec2 start(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                float      accum = 0.f;
                float      weightSum = 0.f;

                auto Accumulate = [&](float directionSign) {
                    glm::vec2 pos = start;
                    for (int i = 0; i < kernelSteps; ++i) {
                        glm::vec2 vec = SampleVector(pos);
                        float     len = glm::length(vec);
                        if (len < 1e-5f) break;
                        glm::vec2 dir = directionSign * vec / len;
                        glm::vec2 next = pos + dir;
                        if (! Inside(next)) break;
                        float w = static_cast<float>(kernelSteps - i);
                        accum += SampleNoise(next) * w;
                        weightSum += w;
                        pos = next;
                    }
                };

                float centerWeight = static_cast<float>(kernelSteps);
                accum += SampleNoise(start) * centerWeight;
                weightSum += centerWeight;
                Accumulate(1.f);
                Accumulate(-1.f);

                float value = weightSum > 0.f ? accum / weightSum : 0.f;
                output.At(static_cast<std::size_t>(x), static_cast<std::size_t>(y)) = glm::vec3(value);
            }
        }
    }
}; // namespace VCX::Labs::Visualization