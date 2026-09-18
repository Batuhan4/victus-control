#ifndef VICTUS_CHARTS_HPP
#define VICTUS_CHARTS_HPP

#include <cairo.h>
#include <deque>
#include <string>
#include <vector>

// Rolling telemetry history and the line chart that draws it.
//
// Temperature and fan speed are deliberately drawn as two separate charts
// rather than one with two y-scales: a shared axis between degrees and RPM
// would put two unrelated scales on one grid, which reads as a correlation
// that is not there.

// One series' samples, oldest first, capped at capacity.
class SeriesHistory {
public:
    explicit SeriesHistory(size_t capacity);

    void push(double value, bool valid);
    void clear();

    size_t size() const { return samples_.size(); }
    size_t capacity() const { return capacity_; }
    bool empty() const { return samples_.empty(); }

    // Invalid samples (sensor absent, GPU asleep) leave a gap rather than a
    // line dropping to zero, which would read as a real measurement.
    struct Sample {
        double value;
        bool valid;
    };
    const std::deque<Sample> &samples() const { return samples_; }

    double latest_valid(bool *found) const;
    double max_valid(double fallback) const;

private:
    std::deque<Sample> samples_;
    size_t capacity_;
};

struct ChartSeries {
    const SeriesHistory *history;
    std::string label;
    double r, g, b;   // categorical colour, validated for CVD separation
    bool visible = true;

    // Each series carries its own scale, because they are overlaid in one plot
    // and every line is normalised against its own axis.
    std::string unit;
    double min_span = 1.0;   // smallest range to show, so a flat trace stays calm
    double step = 1.0;       // axis bounds snap to multiples of this
    double floor_value = 0.0;

    // Legend hit box, written by draw_history_chart each frame so a click can
    // be matched against what was actually drawn rather than a recomputation
    // that could drift from it.
    double legend_x = 0.0, legend_y = 0.0, legend_w = 0.0, legend_h = 0.0;
};

// Draws one chart: recessive grid, 2px lines, a dot and a direct label on the
// latest point of each series. `y_max` is the top of the axis; the axis always
// starts at y_min so the shape is not exaggerated by a floating baseline.
//
// Every chart uses the same horizontal padding and the same sample window, so
// a column is the same instant in all of them - that is what makes the time
// axis shared. Each chart keeps its own y scale, because the alternative is
// putting two unrelated units on one grid.
//
// `draw_time_axis` labels the shared axis; pass it only for the bottom chart
// so the labels appear once under the stack.
void draw_history_chart(cairo_t *cr, int width, int height,
                        std::vector<ChartSeries> &series,
                        double y_min, double y_max,
                        const std::string &unit,
                        bool draw_time_axis,
                        double window_seconds);

// Fits the y range to the visible data instead of anchoring at zero: a fan
// sitting at 5300 of 6000 RPM draws as a flat line on a zero-based axis, which
// hides exactly the variation the chart exists to show. Guards against the
// opposite failure too - `min_span` keeps a steady reading from being magnified
// into dramatic noise, and the bounds snap to multiples of `step` so the axis
// labels stay round. Both ends are labelled, so a non-zero baseline is visible
// rather than implied.
void auto_y_range(const std::vector<ChartSeries> &series, double min_span,
                  double step, double floor_value,
                  double *y_min, double *y_max);

// Draws every visible series overlaid in one plot, each normalised to its own
// range, with those ranges shown as parallel axes down the left in the series'
// own colour, and one shared time axis along the bottom.
//
// The cost of overlaying differently-scaled lines is that a crossing point
// means nothing - it is an artefact of the scaling, not a real relationship.
// Colour-matching each axis to its line is what keeps which-scale-owns-which
// unambiguous; without it this arrangement is simply a misleading chart.
void draw_overlay_chart(cairo_t *cr, int width, int height,
                        std::vector<ChartSeries> &series,
                        double window_seconds);

// Index of the legend entry at (x, y) from the last draw, or -1.
int chart_legend_index_at(const std::vector<ChartSeries> &series,
                          double x, double y);

#endif // VICTUS_CHARTS_HPP
