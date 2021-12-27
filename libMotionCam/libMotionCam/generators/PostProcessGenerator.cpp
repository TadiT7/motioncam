#include <Halide.h>

#include <vector>
#include <functional>
#include <limits>

#include "Common.h"

using namespace Halide;
using namespace Halide::ConciseCasts;

using std::vector;
using std::function;
using std::pair;

//

class PostProcessBase {
protected:
    void deinterleave(Func& result, Func in, Expr stride, Expr rawFormat);
    void transform(Func& output, Func input, Func matrixSrgb);

    void rearrange(Func& output, Func input, Expr sensorArrangement);
    void rearrange(Func& output, Func in0, Func in1, Func in2, Func in3, Expr sensorArrangement);

    void blur(Func& output, Func& outputTmp, Func input);
    void blur2(Func& output, Func& outputTmp, Func input);
    void blur3(Func& output, Func& outputTmp, Func input);

    Func downsample(Func f, Func& temp);
    Func upsample(Func f, Func& temp);

    void RGBToYCbCr(Func& output, Func input);
    void YCbCrToRGB(Func& output, Func input);

    void cmpSwap(Expr& a, Expr& b);

    void RGBToHSV(Func& output, Func input);
    void HSVToBGR(Func& output, Func input);

    void shiftHues(
        Func& output, Func hsvInput, Expr blues, Expr greens, Expr saturation);

    void linearScale(Func& result, Func image, Expr fromWidth, Expr fromHeight, Expr toWidth, Expr toHeight);

    void warp(Func& output, const Func& in, const Func& m);

private:
    Func deinterleaveRaw16(Func in, Expr stride);
    Func deinterleaveRaw12(Func in, Expr stride);
    Func deinterleaveRaw10(Func in, Expr stride);

protected:
    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};

    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};
};

void PostProcessBase::warp(Func& output, const Func& in, const Func& m) {
    Func inputF32{"inputF32"};

    inputF32(v_x, v_y, v_c) = cast<float>(in(v_x, v_y, v_c));

    Expr fx = m(0, 0)*v_x + m(1, 0)*v_y + m(2, 0);
    Expr fy = m(0, 1)*v_x + m(1, 1)*v_y + m(2, 1);
    Expr fw = m(0, 2)*v_x + m(1, 2)*v_y + m(2, 2);

    fx = fx / fw;
    fy = fy / fw;

    Expr x = cast<int16_t>(fx);
    Expr y = cast<int16_t>(fy);
    
    Expr a = fx - x;
    Expr b = fy - y;
    
    Expr p0 = lerp(inputF32(x, y, v_c), inputF32(x + 1, y, v_c), a);
    Expr p1 = lerp(inputF32(x, y + 1, v_c), inputF32(x + 1, y + 1, v_c), a);
    
    output(v_x, v_y, v_c) = saturating_cast<uint16_t>(lerp(p0, p1, b) + 0.5f);
}

void PostProcessBase::deinterleave(Func& result, Func in, Expr stride, Expr rawFormat) {
    Func bayer{"bayer"};

    bayer(v_x, v_y) =
        select( rawFormat == static_cast<int>(RawFormat::RAW10), cast<uint16_t>(deinterleaveRaw10(in, stride)(v_x, v_y)),
                rawFormat == static_cast<int>(RawFormat::RAW12), cast<uint16_t>(deinterleaveRaw12(in, stride)(v_x, v_y)),
                rawFormat == static_cast<int>(RawFormat::RAW16), cast<uint16_t>(deinterleaveRaw16(in, stride)(v_x, v_y)),
                cast<uint16_t>(0));

    result(v_x, v_y, v_c) = select(
        v_c == 0, bayer(v_x*2,      v_y*2),
        v_c == 1, bayer(v_x*2 + 1,  v_y*2),
        v_c == 2, bayer(v_x*2,      v_y*2 + 1),
                  bayer(v_x*2 + 1,  v_y*2 + 1));
}

Func PostProcessBase::deinterleaveRaw10(Func in, Expr stride) {
    Func bayer{"bayer"};

    Expr X = (v_x / 4) * 4;
    Expr xoffset = (v_y * stride) + 10*X/8;
    
    Expr p = v_x - X;
    Expr shift = p * 2;

    bayer(v_x, v_y) = (cast<uint16_t>(in(xoffset + p)) << 2) | ((cast<uint16_t>(in(xoffset + 4)) >> shift) & 0x03);

    return bayer;
}

Func PostProcessBase::deinterleaveRaw12(Func in, Expr stride) {
    Func bayer{"bayer"};

    Expr X = (v_x / 2) * 2;
    Expr xoffset = (v_y*stride) + 12*X/8;

    Expr p = v_x - X;
    Expr shift = p * 4;

    bayer(v_x, v_y) = (cast<uint16_t>(in(xoffset + p)) << 4) | ((cast<uint16_t>(in(xoffset + 2)) >> shift) & 0x0F);

    return bayer;
}

Func PostProcessBase::deinterleaveRaw16(Func in, Expr stride) {
    Func bayer{"bayer"};
    
    Expr offset = (v_y*stride) + (v_x*2);

    bayer(v_x, v_y) = cast<uint16_t>(in(offset)) | (cast<uint16_t>(in(offset + 1)) << 8);

    return bayer;
}

void PostProcessBase::blur(Func& output, Func& outputTmp, Func input) {
    Func in32{"blur_in32"};

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    outputTmp(v_x, v_y) = (
        1 * in32(v_x - 1, v_y) +
        2 * in32(v_x    , v_y) +
        1 * in32(v_x + 1, v_y)
    ) / 4;

    output(v_x, v_y) =
        cast<uint16_t> (
            (
              1 * outputTmp(v_x, v_y - 1) +
              2 * outputTmp(v_x, v_y)     +
              1 * outputTmp(v_x, v_y + 1)             
            ) / 4
        );
}

void PostProcessBase::blur2(Func& output, Func& outputTmp, Func input) {
    Func in32{"blur2_in32"};

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));
    
    outputTmp(v_x, v_y) = (
        1 * in32(v_x - 2, v_y) +
        4 * in32(v_x - 1, v_y) +
        6 * in32(v_x,     v_y) +
        4 * in32(v_x + 1, v_y) +
        1 * in32(v_x + 2, v_y)
    ) / 16;

    output(v_x, v_y) =
        cast<uint16_t> (
            (
              1 * outputTmp(v_x, v_y - 2) +
              4 * outputTmp(v_x, v_y - 1) +
              6 * outputTmp(v_x, v_y)     +
              4 * outputTmp(v_x, v_y + 1) +
              1 * outputTmp(v_x, v_y + 2)             
            ) / 16
        );
}

void PostProcessBase::blur3(Func& output, Func& outputTmp, Func input) {
    Func in32{"blur3_in32"};

    in32(v_x, v_y) = cast<int32_t>(input(v_x, v_y));

    outputTmp(v_x, v_y) = (
        1  * in32(v_x - 4, v_y) +
        8  * in32(v_x - 3, v_y) +
        28 * in32(v_x - 2, v_y) +
        56 * in32(v_x - 1, v_y) +
        70 * in32(v_x,     v_y) +
        56 * in32(v_x + 1, v_y) +
        28 * in32(v_x + 2, v_y) +
        8  * in32(v_x + 3, v_y) +
        1  * in32(v_x + 4, v_y)
    ) / 256;

    output(v_x, v_y) =
        cast<uint16_t> ((
            1  * outputTmp(v_x, v_y - 4) +
            8  * outputTmp(v_x, v_y - 3) +
            28 * outputTmp(v_x, v_y - 2) +
            56 * outputTmp(v_x, v_y - 1) +
            70 * outputTmp(v_x, v_y)     +
            56 * outputTmp(v_x, v_y + 1) +
            28 * outputTmp(v_x, v_y + 2) +
            8  * outputTmp(v_x, v_y + 3) +
            1  * outputTmp(v_x, v_y + 4)
            ) / 256
        );
}

Func PostProcessBase::downsample(Func f, Func& temp) {
    using Halide::_;
    Func in, downx, downy;
    
    in(v_x, v_y, _) = cast<int32_t>(f(v_x, v_y, _));

    temp(v_x, v_y, _) = (
        1 * in(v_x*2 - 1, v_y, _) +
        2 * in(v_x*2,     v_y, _) +
        1 * in(v_x*2 + 1, v_y, _) ) >> 2;

    downy(v_x, v_y, _) = cast<uint16_t> (
       (
        1 * temp(v_x, v_y*2 - 1, _) +
        2 * temp(v_x, v_y*2,     _) +
        1 * temp(v_x, v_y*2 + 1, _)
       ) >> 2
    );
    
    return downy;
}

Func PostProcessBase::upsample(Func f, Func& temp) {
    using Halide::_;
    Func in, upx, upy;
    
    in(v_x, v_y, _) = cast<int32_t>(f(v_x, v_y, _));

    temp(v_x, v_y, _) = (
        1 * in(v_x/2 - 1, v_y, _) +
        2 * in(v_x/2,     v_y, _) +
        1 * in(v_x/2 + 1, v_y, _)) >> 2;

    upy(v_x, v_y, _) = cast<uint16_t> (
       (1 * temp(v_x, v_y/2 - 1, _) +
        2 * temp(v_x, v_y/2,     _) +
        1 * temp(v_x, v_y/2 + 1, _)) >> 2
    );

    return upy;
}

void PostProcessBase::transform(Func& output, Func input, Func m) {
    Expr ir = input(v_x, v_y, 0);
    Expr ig = input(v_x, v_y, 1);
    Expr ib = input(v_x, v_y, 2);

    // Color correct
    Expr r = m(0, 0) * ir + m(1, 0) * ig + m(2, 0) * ib;
    Expr g = m(0, 1) * ir + m(1, 1) * ig + m(2, 1) * ib;
    Expr b = m(0, 2) * ir + m(1, 2) * ig + m(2, 2) * ib;
    
    output(v_x, v_y, v_c) = select(v_c == 0, r,
                                   v_c == 1, g,
                                             b);
}

void PostProcessBase::rearrange(Func& output, Func in0, Func in1, Func in2, Func in3, Expr sensorArrangement) {
    output(v_x, v_y, v_c) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                select( v_c == 0, in0(v_x, v_y),
                        v_c == 1, in1(v_x, v_y),
                        v_c == 2, in2(v_x, v_y),
                                  in3(v_x, v_y) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                select( v_c == 0, in1(v_x, v_y),
                        v_c == 1, in0(v_x, v_y),
                        v_c == 2, in3(v_x, v_y),
                                  in2(v_x, v_y) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                select( v_c == 0, in2(v_x, v_y),
                        v_c == 1, in0(v_x, v_y),
                        v_c == 2, in3(v_x, v_y),
                                  in1(v_x, v_y) ),

                select( v_c == 0, in3(v_x, v_y),
                        v_c == 1, in1(v_x, v_y),
                        v_c == 2, in2(v_x, v_y),
                                  in0(v_x, v_y) ) );

}

void PostProcessBase::rearrange(Func& output, Func input, Expr sensorArrangement) {
    output(v_x, v_y, v_c) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                select( v_c == 0, input(v_x, v_y, 0),
                        v_c == 1, input(v_x, v_y, 1),
                        v_c == 2, input(v_x, v_y, 2),
                                  input(v_x, v_y, 3) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                select( v_c == 0, input(v_x, v_y, 1),
                        v_c == 1, input(v_x, v_y, 0),
                        v_c == 2, input(v_x, v_y, 3),
                                  input(v_x, v_y, 2) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                select( v_c == 0, input(v_x, v_y, 2),
                        v_c == 1, input(v_x, v_y, 0),
                        v_c == 2, input(v_x, v_y, 3),
                                  input(v_x, v_y, 1) ),

                select( v_c == 0, input(v_x, v_y, 3),
                        v_c == 1, input(v_x, v_y, 1),
                        v_c == 2, input(v_x, v_y, 2),
                                  input(v_x, v_y, 0) ) );

}

void PostProcessBase::RGBToYCbCr(Func& output, Func input) {
    Func toYCbCr{"YCbCr"};

    toYCbCr(v_x, v_y) = 0.0f;

    toYCbCr(0, 0) = 0.2126f;    toYCbCr(1, 0) = 0.7152f;    toYCbCr(2, 0) = 0.0722f;
    toYCbCr(0, 1) = -0.1146f;   toYCbCr(1, 1) = -0.3854f;   toYCbCr(2, 1) = 0.5f;
    toYCbCr(0, 2) = 0.5f;       toYCbCr(1, 2) = -0.4542f;   toYCbCr(2, 2) = -0.0458f;

    transform(output, input, toYCbCr);
}

void PostProcessBase::YCbCrToRGB(Func& output, Func input) {
    Func toRgb{"toRgb"};

    toRgb(v_x, v_y) = 0.0f;

    toRgb(0, 0) = 1.0f; toRgb(1, 0) = 0.0f;     toRgb(2, 0) = 1.5748f;
    toRgb(0, 1) = 1.0f; toRgb(1, 1) = -0.1873f; toRgb(2, 1) = -0.4681f;
    toRgb(0, 2) = 1.0f; toRgb(1, 2) = 1.8556f;  toRgb(2, 2) = 0.0f;

    transform(output, input, toRgb);
}

void PostProcessBase::RGBToHSV(Func& output, Func input) {
    const float eps = std::numeric_limits<float>::epsilon();

    Expr r = cast<float>(input(v_x, v_y, 0));
    Expr g = cast<float>(input(v_x, v_y, 1));
    Expr b = cast<float>(input(v_x, v_y, 2));

    Expr maxRgb = max(r, g, b);
    Expr min0gb = min(r, g, b);

    Expr delta = maxRgb - min0gb;
    
    Expr h = select(abs(delta) < eps, 0.0f,
                    maxRgb == r, ((g - b) / delta) % 6,
                    maxRgb == g, 2.0f + (b - r) / delta,
                                 4.0f + (r - g) / delta);

    Expr s = select(abs(maxRgb) < eps, 0.0f, delta / maxRgb);
    Expr v = maxRgb;

    output(v_x, v_y, v_c) = select(v_c == 0, 60.0f * h,
                                   v_c == 1, s,
                                             v);
}

void PostProcessBase::HSVToBGR(Func& output, Func input) {
    Expr H = cast<float>(input(v_x, v_y, 0));
    Expr S = cast<float>(input(v_x, v_y, 1));
    Expr V = cast<float>(input(v_x, v_y, 2));

    Expr h = H / 60.0f;
    Expr i = cast<int>(h);
    
    Expr f = h - i;
    Expr p = V * (1.0f - S);
    Expr q = V * (1.0f - S * f);
    Expr t = V * (1.0f - S * (1.0f - f));
    
    Expr r = select(i == 0, V,
                    i == 1, q,
                    i == 2, p,
                    i == 3, p,
                    i == 4, t,
                            V);
    
    Expr g = select(i == 0, t,
                    i == 1, V,
                    i == 2, V,
                    i == 3, q,
                    i == 4, p,
                            p);

    Expr b = select(i == 0, p,
                    i == 1, p,
                    i == 2, t,
                    i == 3, V,
                    i == 4, V,
                            q);

    output(v_x, v_y, v_c) = select(v_c == 0, clamp(b, 0.0f, 1.0f),
                                   v_c == 1, clamp(g, 0.0f, 1.0f),
                                             clamp(r, 0.0f, 1.0f));
}

void PostProcessBase::cmpSwap(Expr& a, Expr& b) {
    Expr tmp = min(a, b);
    b = max(a, b);
    a = tmp;
}

void PostProcessBase::linearScale(Func& result, Func image, Expr fromWidth, Expr fromHeight, Expr toWidth, Expr toHeight) {
    Expr scaleX = toWidth * fast_inverse(cast<float>(fromWidth));
    Expr scaleY = toHeight * fast_inverse(cast<float>(fromHeight));
    
    Expr fx = max(0.0f, (v_x + 0.5f) * fast_inverse(scaleX) - 0.5f);
    Expr fy = max(0.0f, (v_y + 0.5f) * fast_inverse(scaleY) - 0.5f);
    
    Expr x = cast<int16_t>(fx);
    Expr y = cast<int16_t>(fy);
    
    Expr a = fx - x;
    Expr b = fy - y;

    Expr x0 = clamp(x, 0, cast<int16_t>(fromWidth) - 1);
    Expr y0 = clamp(y, 0, cast<int16_t>(fromHeight) - 1);

    Expr x1 = clamp(x + 1, 0, cast<int16_t>(fromWidth) - 1);
    Expr y1 = clamp(y + 1, 0, cast<int16_t>(fromHeight) - 1);
    
    Expr p0 = lerp(cast<float>(image(x0, y0)), cast<float>(image(x1, y0)), a);
    Expr p1 = lerp(cast<float>(image(x0, y1)), cast<float>(image(x1, y1)), a);

    result(v_x, v_y) = lerp(p0, p1, b);
}

void PostProcessBase::shiftHues(
    Func& output, Func hsvInput, Expr blues, Expr greens, Expr saturation)
{
    Expr H = hsvInput(v_x, v_y, 0);
    Expr S = hsvInput(v_x, v_y, 1);

    Expr blueWeight   = exp(-(H - 180)*(H - 180) / 1000);
    Expr greenWeight  = exp(-(H - 90)*(H - 90) / 1000);

    output(v_x, v_y, v_c) =
        select(v_c == 0, H + blues*blueWeight + greens*greenWeight,
               v_c == 1, clamp(S * saturation, 0.0f, 1.0f),
                         hsvInput(v_x, v_y, v_c));
}

//
//
// Guided Image Filtering, by Kaiming He, Jian Sun, and Xiaoou Tang
//

class GuidedFilter : public Halide::Generator<GuidedFilter> {
public:
    GeneratorParam<int> radius{"radius", 51};

    Input<Func> input{"input", 3};
    Input<Func> eps {"eps", 2};

    Output<Func> output{"output", 2};
    
    GeneratorParam<Type> output_type{"output_type", UInt(16)};
    Input<uint16_t> width {"width"};
    Input<uint16_t> height {"height"};
    Input<uint16_t> channel {"channel"};
    
    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};

    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};

    void generate();

    void schedule();
    void schedule_for_cpu();
    void schedule_for_gpu();
    void apply_auto_schedule();

    Func I{"I"}, I2{"I2"};
    Func mean_I{"mean_I"}, mean_temp_I{"mean_temp_I"};
    Func mean_II{"mean_II"}, mean_temp_II{"mean_temp_II"};
    Func var_I{"var_I"};
    
    Func mean0{"mean0"}, mean1{"mean1"}, var{"var"};
    
    Func a{"a"}, b{"b"};
    Func mean_a{"mean_a"}, mean_temp_a{"mean_temp_a"};
    Func mean_b{"mean_b"}, mean_temp_b{"mean_temp_b"};

private:
    void boxFilter(Func& result, Func& intermediate, Func in);
};

void GuidedFilter::apply_auto_schedule() {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;

    Var x = v_x;
    Var xi("xi");
    Var xii("xii");
    Var xiii("xiii");
    Var y = v_y;
    Var yi("yi");
    output
        .split(x, x, xi, 256, TailStrategy::ShiftInwards)
        .split(y, y, yi, 384, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 32, TailStrategy::ShiftInwards)
        .split(xii, xii, xiii, 16, TailStrategy::ShiftInwards)
        .vectorize(xiii)
        .compute_root()
        .reorder({xiii, xii, yi, xi, x, y})
        .fuse(x, y, x)
        .parallel(x);
    mean_temp_b
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(output, xi)
        .reorder({xi, x, y});
    b
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(mean_temp_b, y)
        .reorder({xi, x, y});
    mean_temp_a
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(output, xi)
        .reorder({xi, x, y});
    a
        .split(x, x, xi, 32, TailStrategy::RoundUp)
        .split(y, y, yi, 3, TailStrategy::RoundUp)
        .split(xi, xi, xii, 8, TailStrategy::RoundUp)
        .unroll(xi)
        .unroll(yi)
        .vectorize(xii)
        .compute_at(output, x)
        .reorder({xii, xi, yi, y, x});
    var_I
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .unroll(y)
        .vectorize(xi)
        .compute_at(a, y)
        .reorder({xi, x, y});
    mean_temp_II
        .store_in(MemoryType::Stack)
        .split(y, y, yi, 3, TailStrategy::RoundUp)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .unroll(yi)
        .vectorize(xi)
        .compute_at(a, x)
        .reorder({xi, x, yi, y});
    I2
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(mean_temp_II, y)
        .reorder({xi, x, y});
    mean_I
        .split(x, x, xi, 31, TailStrategy::RoundUp)
        .split(y, y, yi, 8, TailStrategy::RoundUp)
        .vectorize(yi)
        .compute_at(output, x)
        .reorder({yi, y, xi, x})
        .reorder_storage(y, x);
    mean_temp_I
        .store_in(MemoryType::Stack)
        .split(y, y, yi, 8, TailStrategy::RoundUp)
        .vectorize(yi)
        .compute_at(mean_I, x)
        .reorder({yi, y, x})
        .reorder_storage(y, x);
    I
        .split(y, y, yi, 16, TailStrategy::ShiftInwards)
        .vectorize(yi)
        .compute_at(output, x)
        .reorder({yi, y, x})
        .reorder_storage(y, x);
}

void GuidedFilter::boxFilter(Func& result, Func& intermediate, Func in) {
   const int R = radius;
   RDom r(-R/2, R);

   intermediate(v_x, v_y) = sum(in(v_x + r.x, v_y)) / R;
   result(v_x, v_y) = sum(intermediate(v_x, v_y + r.x)) / R;
        
    // Expr s = 0.0f;
    // Expr t = 0.0f;
    
    // for(int i = -R/2; i <= R/2; i++)
    //     s += in(v_x+i, v_y);
    
    // intermediate(v_x, v_y) = s/R;
    
    // for(int i = -R/2; i <= R/2; i++)
    //     t += intermediate(v_x, v_y+i);
    
    // result(v_x, v_y) = t/R;
}

void GuidedFilter::generate() {        
    I(v_x, v_y) = cast<float>(input(v_x, v_y, channel));
    I2(v_x, v_y) = I(v_x, v_y) * I(v_x, v_y);
    
    boxFilter(mean_I, mean_temp_I, I);
    boxFilter(mean_II, mean_temp_II, I2);
    
    var_I(v_x, v_y) = mean_II(v_x, v_y) - (mean_I(v_x, v_y) * mean_I(v_x, v_y));
    
    a(v_x, v_y) = var_I(v_x, v_y) / (var_I(v_x, v_y) + eps(v_x, v_y));
    b(v_x, v_y) = mean_I(v_x, v_y) - (a(v_x, v_y) * mean_I(v_x, v_y));
    
    boxFilter(mean_a, mean_temp_a, a);
    boxFilter(mean_b, mean_temp_b, b);
    
    output(v_x, v_y) = cast(output_type, clamp((mean_a(v_x, v_y) * I(v_x, v_y)) + mean_b(v_x, v_y), 0, ((Type)output_type).max()));

    if(!auto_schedule) {
        if(get_target().has_gpu_feature())
            schedule_for_gpu();
        else
            apply_auto_schedule();
    }

    input.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});
    output.set_estimates({{0, 4096}, {0, 3072}});
    width.set_estimate(4096);
    height.set_estimate(3072);
    channel.set_estimate(1);
}

void GuidedFilter::schedule() {    
}

void GuidedFilter::schedule_for_cpu() {
   output
        .compute_root()
        .reorder(v_x, v_y)
        .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 128, 128)
        .fuse(v_xo, v_yo, tile_idx)
        .tile(v_xi, v_yi, v_xio, v_yio, v_xii, v_yii, 64, 64)
        .fuse(v_xio, v_yio, subtile_idx)
        .parallel(tile_idx)
        .vectorize(v_xii, 8);
    
    mean_temp_I
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_temp_II
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    var_I
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_I
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);
    
    mean_II
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    a
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    b
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_temp_a
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_temp_b
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_a
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);

    mean_b
        .compute_at(output, subtile_idx)
        .store_at(output, tile_idx)
        .vectorize(v_x, 8);    
}

void GuidedFilter::schedule_for_gpu() {
    output
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);
    
    mean_temp_I
        .compute_at(mean_I, v_x)
        .gpu_threads(v_x, v_y);

    mean_temp_II
        .compute_at(mean_II, v_x)
        .gpu_threads(v_x, v_y);


    mean_I
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);
    
    mean_II
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    mean_temp_a
        .compute_at(mean_a, v_x)
        .gpu_threads(v_x, v_y);

    mean_temp_b
        .compute_at(mean_b, v_x)
        .gpu_threads(v_x, v_y);

    mean_a
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);

    mean_b
        .compute_root()
        .reorder(v_x, v_y)
        .gpu_tile(v_x, v_y, v_xi, v_yi, 16, 32);    
}

//

//
// Color filter array demosaicking: new method and performance measures
// W Lu, YP Tan - IEEE transactions on image processing, 2003 - ieeexplore.ieee.org
//
// Directional LMMSE Image Demosaicking, Image Processing On Line, 1 (2011), pp. 117–126.
// Pascal Getreuer, Zhang-Wu
//

class Demosaic : public Halide::Generator<Demosaic>, public PostProcessBase {
public:
    Input<Func> in0{"in0", UInt(16), 2 };
    Input<Func> in1{"in1", UInt(16), 2 };
    Input<Func> in2{"in2", UInt(16), 2 };
    Input<Func> in3{"in3", UInt(16), 2 };

    Input<Func> inShadingMap0{"inShadingMap0", Float(32), 2 };
    Input<Func> inShadingMap1{"inShadingMap1", Float(32), 2 };
    Input<Func> inShadingMap2{"inShadingMap2", Float(32), 2 };
    Input<Func> inShadingMap3{"inShadingMap3", Float(32), 2 };

    Input<int> width{"width"};
    Input<int> height{"height"};
    Input<int> shadingMapWidth{"shadingMapWidth"};
    Input<int> shadingMapHeight{"shadingMapHeight"};
    Input<float> range{"range"};
    Input<int> sensorArrangement{"sensorArrangement"};

    Input<float[3]> asShotVector{"asShotVector"};
    Input<Func> cameraToSrgb{"cameraToSrgb", Float(32), 2 };

    Output<Func> output{ "output", UInt(16), 3 };

    //
    
    Func clamped0{"clamped0"};
    Func clamped1{"clamped1"};
    Func clamped2{"clamped2"};
    Func clamped3{"clamped3"};

    Func combinedInput{"combinedInput"};
    Func mirroredInput{"mirroredInput"};
    Func bayerInput{"bayerInput"};
    
    Func shaded{"shaded"};
    Func shadingMap0{"shadingMap0"}, shadingMap1{"shadingMap1"}, shadingMap2{"shadingMap2"}, shadingMap3{"shadingMap3"};
    Func shadingMapArranged{"shadingMapArranged"};

    Func blueI{"blueI"}, blueBlurX{"blueBlurX"};
    Func blueFiltered{"blueFiltered"};

    Func redI{"redI"}, redBlurX{"redBlurX"};
    Func redFiltered{"redFiltered"};

    Func redIntermediate{"redIntermediate"};
    Func red{"red"};
    Func greenIntermediate{"greenIntermediate"};
    Func green{"green"};
    Func blueIntermediate{"blueIntermediate"};
    Func blue{"blue"};
    Func demosaicOutput{"demosaicOutput"};

    Func linear{"linear"};
    Func colorCorrectInput{"colorCorrectInput"};
    Func XYZ{"XYZ"};
    Func colorCorrected{"colorCorrected"};

    void generate();
    void schedule();
    void apply_auto_schedule();

    void cmpSwap(Expr& a, Expr& b);

    void calculateGreen(Func& output, Func input);
    void calculateGreen2(Func& output, Func input);
    void calculateGreen3(Func& output, Func input);

    void calculateRed(Func& output, Func input, Func green);
    void calculateBlue(Func& output, Func input, Func green);

    void medianFilter(Func& output, Func input);
    void weightedMedianFilter(Func& output, Func input);
};

void Demosaic::medianFilter(Func& output, Func input) {
    Expr p0 = input(v_x, v_y);
    Expr p1 = input(v_x, v_y-1);
    Expr p2 = input(v_x, v_y+1);
    Expr p3 = input(v_x-1, v_y);
    Expr p4 = input(v_x+1, v_y);    
    Expr p5 = input(v_x-1, v_y-1);
    Expr p6 = input(v_x-1, v_y+1);
    Expr p7 = input(v_x+1, v_y-1);
    Expr p8 = input(v_x+1, v_y+1);

    cmpSwap(p0, p1);
    cmpSwap(p2, p3);
    cmpSwap(p0, p2);
    cmpSwap(p1, p3);
    cmpSwap(p1, p2);
    cmpSwap(p4, p5);
    cmpSwap(p7, p8);
    cmpSwap(p6, p8);
    cmpSwap(p6, p7);
    cmpSwap(p4, p7);
    cmpSwap(p4, p6);
    cmpSwap(p5, p8);
    cmpSwap(p5, p7);
    cmpSwap(p5, p6);
    cmpSwap(p0, p5);
    cmpSwap(p0, p4);
    cmpSwap(p1, p6);
    cmpSwap(p1, p5);
    cmpSwap(p1, p4);
    cmpSwap(p2, p7);
    cmpSwap(p3, p8);
    cmpSwap(p3, p7);
    cmpSwap(p2, p5);
    cmpSwap(p2, p4);
    cmpSwap(p3, p6);
    cmpSwap(p3, p5);
    cmpSwap(p3, p4);

    output(v_x, v_y) = p4;
}

void Demosaic::weightedMedianFilter(Func& output, Func input) {
    Expr p0 = input(v_x, v_y);
    Expr p1 = input(v_x, v_y);
    Expr p2 = input(v_x, v_y);
    Expr p3 = input(v_x, v_y);
    Expr p4 = input(v_x, v_y-1);
    Expr p5 = input(v_x, v_y+1);
    Expr p6 = input(v_x-1, v_y);
    Expr p7 = input(v_x+1, v_y);
    Expr p8 = input(v_x-1, v_y-1);
    Expr p9 = input(v_x-1, v_y+1);
    Expr p10 = input(v_x+1, v_y-1);
    Expr p11 = input(v_x+1, v_y+1);
    
    cmpSwap(p1, p2);
    cmpSwap(p0, p2);
    cmpSwap(p0, p1);
    cmpSwap(p4, p5);
    cmpSwap(p3, p5);
    cmpSwap(p3, p4);
    cmpSwap(p0, p3);
    cmpSwap(p1, p4);
    cmpSwap(p2, p5);
    cmpSwap(p2, p4);
    cmpSwap(p1, p3);
    cmpSwap(p2, p3);
    cmpSwap(p7, p8);
    cmpSwap(p6, p8);
    cmpSwap(p6, p7);
    cmpSwap(p10,p11);
    cmpSwap(p9, p11);
    cmpSwap(p9, p10);
    cmpSwap(p6, p9);
    cmpSwap(p7, p10);
    cmpSwap(p8, p11);
    cmpSwap(p8, p10);
    cmpSwap(p7, p9);
    cmpSwap(p8, p9);
    cmpSwap(p0, p6);
    cmpSwap(p1, p7);
    cmpSwap(p2, p8);
    cmpSwap(p2, p7);
    cmpSwap(p1, p6);
    cmpSwap(p2, p6);
    cmpSwap(p3, p9);
    cmpSwap(p4, p10);
    cmpSwap(p5, p11);
    cmpSwap(p5, p10);
    cmpSwap(p4, p9);
    cmpSwap(p5, p9);
    cmpSwap(p3, p6);
    cmpSwap(p4, p7);
    cmpSwap(p5, p8);
    cmpSwap(p5, p7);
    cmpSwap(p4, p6);
    cmpSwap(p5, p6);

    output(v_x, v_y) = cast<int16_t>((cast<int32_t>(p5) + cast<int32_t>(p6)) / 2);
}

void Demosaic::cmpSwap(Expr& a, Expr& b) {
    Expr tmp = min(a, b);
    b = max(a, b);
    a = tmp;
}

void Demosaic::calculateGreen3(Func& output, Func input) {
    Func in;

    in(v_x, v_y) = cast<int32_t>(input(v_x, v_y));

    Expr g12 = in(v_x - 1, v_y - 2);
    Expr g14 = in(v_x + 1, v_y - 2);
    Expr g16 = in(v_x + 3, v_y - 2);
    
    Expr g21 = in(v_x - 2, v_y - 1);
    Expr g23 = in(v_x - 0, v_y - 1);
    Expr g25 = in(v_x + 2, v_y - 1);

    Expr g32 = in(v_x - 1, v_y + 0);
    Expr g34 = in(v_x + 1, v_y + 0);
    Expr g36 = in(v_x + 3, v_y + 0);

    Expr g41 = in(v_x - 2, v_y + 1);
    Expr g43 = in(v_x - 0, v_y + 1);
    Expr g45 = in(v_x + 2, v_y + 1);

    Expr g52 = in(v_x - 1, v_y + 2);
    Expr g54 = in(v_x + 1, v_y + 2);
    Expr g56 = in(v_x + 3, v_y + 2);

    Expr r11 = in(v_x - 2, v_y - 2);
    Expr r13 = in(v_x - 0, v_y - 2);
    Expr r15 = in(v_x + 2, v_y - 2);

    Expr r31 = in(v_x - 2, v_y - 0);
    Expr r33 = in(v_x - 0, v_y - 0);
    Expr r35 = in(v_x + 2, v_y - 0);

    Expr r51 = in(v_x - 2, v_y + 2);
    Expr r53 = in(v_x - 0, v_y + 2);
    Expr r55 = in(v_x + 2, v_y + 2);

    Expr b22 = in(v_x - 1, v_y - 1);
    Expr b24 = in(v_x + 1, v_y - 1);
    Expr b26 = in(v_x + 3, v_y - 1);

    Expr b42 = in(v_x - 1, v_y + 1);
    Expr b44 = in(v_x + 1, v_y + 1);
    Expr b46 = in(v_x + 3, v_y + 1);


    Expr N = abs(g23 - g43) + abs(r13 - r33) + abs(b22 - b42)/2 + abs(b24 - b44)/2 + abs(g12 - g32)/2 + abs(g14 - g34)/2;
    Expr E = abs(g32 - g34) + abs(r33 - r35) + abs(b22 - b24)/2 + abs(b42 - b44)/2 + abs(g23 - g25)/2 + abs(g43 - g45)/2;
    Expr S = abs(g23 - g43) + abs(r33 - r53) + abs(b22 - b42)/2 + abs(b24 - b44)/2 + abs(g32 - g52)/2 + abs(g34 - g54)/2;
    Expr W = abs(g32 - g34) + abs(r31 - r33) + abs(b22 - b24)/2 + abs(b42 - b44)/2 + abs(g21 - g23)/2 + abs(g41 - g43)/2;

    Expr NE = abs(b24 - b42) + abs(r15 - r33) + abs(g23 - g32)/2 + abs(g34 - g43)/2 + abs(g14 - g23)/2 + abs(g25 - g34)/2;
    Expr SE = abs(b22 - b44) + abs(r33 - r55) + abs(g23 - g34)/2 + abs(g32 - g43)/2 + abs(g34 - g45)/2 + abs(g43 - g54)/2;
    Expr NW = abs(b22 - b44) + abs(r11 - r33) + abs(g12 - g23)/2 + abs(g21 - g32)/2 + abs(g23 - g34)/2 + abs(g32 - g43)/2;
    Expr SW = abs(b24 - b42) + abs(r51 - r33) + abs(g23 - g32)/2 + abs(g34 - g43)/2 + abs(g32 - g41)/2 + abs(g43 - g52)/2;

    Expr Rn = (r13 + r33) / 2;
    Expr Re = (r33 + r35) / 2;
    Expr Rs = (r33 + r53) / 2;
    Expr Rw = (r31 + r33) / 2;

    Expr Rne = (r15 + r33) / 2;
    Expr Rse = (r33 + r55) / 2;
    Expr Rnw = (r11 + r33) / 2;
    Expr Rsw = (r33 + r51) / 2;

    Expr Gn = g23;
    Expr Ge = g34;
    Expr Gs = g43;
    Expr Gw = g32;

    Expr Gne = (g14 + g23 + g25 + g34) / 4;
    Expr Gse = (g34 + g43 + g45 + g54) / 4;
    Expr Gnw = (g12 + g21 + g23 + g32) / 4;
    Expr Gsw = (g32 + g41 + g43 + g52) / 4;

    Func gradient{"gradient"};
    Func threshold{"threshold"};
    Func selection{"selection"};
    Func red{"red"};
    Func green{"green"};
    Func blue{"blue"};
    RDom r(0, 8);

    gradient(v_x, v_y, v_c) = select(
        v_c == 0, N,
        v_c == 1, E,
        v_c == 2, S,
        v_c == 3, W,
        v_c == 4, NE,
        v_c == 5, SE,
        v_c == 6, NW,
                  SW);

    Expr gradientMin = min(
        gradient(v_x, v_y, 0),
        gradient(v_x, v_y, 1),
        gradient(v_x, v_y, 2),
        gradient(v_x, v_y, 3),
        gradient(v_x, v_y, 4),
        gradient(v_x, v_y, 5),
        gradient(v_x, v_y, 6),
        gradient(v_x, v_y, 7));

    Expr gradientMax = max(
        gradient(v_x, v_y, 0),
        gradient(v_x, v_y, 1),
        gradient(v_x, v_y, 2),
        gradient(v_x, v_y, 3),
        gradient(v_x, v_y, 4),
        gradient(v_x, v_y, 5),
        gradient(v_x, v_y, 6),
        gradient(v_x, v_y, 7));

    threshold(v_x, v_y) = 1.5f*gradientMin + 0.5f*(gradientMax - gradientMin);
    selection(v_x, v_y, v_c) = select(gradient(v_x, v_y, v_c) < threshold(v_x, v_y), 1.0f, 0.0f);

    red(v_x, v_y, v_c) = select(
        v_c == 0, Rn,
        v_c == 1, Re,
        v_c == 2, Rs,
        v_c == 3, Rw,
        v_c == 4, Rne,
        v_c == 5, Rse,
        v_c == 6, Rnw,
                  Rsw);

    green(v_x, v_y, v_c) = select(
        v_c == 0, Gn,
        v_c == 1, Ge,
        v_c == 2, Gs,
        v_c == 3, Gw,
        v_c == 4, Gne,
        v_c == 5, Gse,
        v_c == 6, Gnw,
                  Gsw);

    Func redAvg{"redAvg"};
    Func greenAvg{"greenAvg"};
    Func filtered{"greenFiltered"};

    redAvg(v_x, v_y) = sum(selection(v_x, v_y, r.x)*red(v_x, v_y, r.x)) / (1e-10f + sum(selection(v_x, v_y, r.x)));
    greenAvg(v_x, v_y) = sum(selection(v_x, v_y, r.x)*green(v_x, v_y, r.x)) / (1e-10f + sum(selection(v_x, v_y, r.x)));

    Expr Gout = r33 + (greenAvg(v_x, v_y) - redAvg(v_x, v_y));

    greenIntermediate(v_x, v_y) = select(
        ((v_x + v_y) & 1) == 1,
            input(v_x, v_y),
            saturating_cast<int16_t>(Gout + 0.5f));

    weightedMedianFilter(output, greenIntermediate);
}

void Demosaic::calculateGreen2(Func& output, Func input) {
    const int M = 1;
    const float DivEpsilon = 0.1f/(1024.0f*1024.0f);

    Func filteredH, filteredV, diffH, diffV, smoothedH, smoothedV;    

    filteredH(v_x, v_y) = -0.25f*input(v_x-2, v_y) + 0.5f*input(v_x-1, v_y) + 0.5f*input(v_x, v_y) + 0.5f*input(v_x+1, v_y) - 0.25f*input(v_x+2, v_y);
    filteredV(v_x, v_y) = -0.25f*input(v_x, v_y-2) + 0.5f*input(v_x, v_y-1) + 0.5f*input(v_x, v_y) + 0.5f*input(v_x, v_y+1) - 0.25f*input(v_x, v_y+2);

    diffH(v_x, v_y) =
        select(((v_x + v_y) & 1) == 1,  input(v_x, v_y) - filteredH(v_x, v_y),
                                        filteredH(v_x, v_y) - input(v_x, v_y));

    diffV(v_x, v_y) =
        select(((v_x + v_y) & 1) == 1,  input(v_x, v_y) - filteredV(v_x, v_y),
                                        filteredV(v_x, v_y) - input(v_x, v_y));

    smoothedH(v_x, v_y) = 0.0312500f*diffH(v_x-4, v_y) + 0.0703125f*diffH(v_x-3, v_y) + 0.1171875f*diffH(v_x-2, v_y) +
                          0.1796875f*diffH(v_x-1, v_y) + 0.2031250f*diffH(v_x,   v_y) + 0.1796875f*diffH(v_x+1, v_y) +
                          0.1171875f*diffH(v_x+2, v_y) + 0.0703125f*diffH(v_x+3, v_y) + 0.0312500f*diffH(v_x+4, v_y);

    smoothedV(v_x, v_y) = 0.0312500f*diffV(v_x, v_y-4) + 0.0703125f*diffV(v_x, v_y-3) + 0.1171875f*diffV(v_x, v_y-2) +
                          0.1796875f*diffV(v_x, v_y-1) + 0.2031250f*diffV(v_x, v_y)   + 0.1796875f*diffV(v_x, v_y+1) +
                          0.1171875f*diffV(v_x, v_y+2) + 0.0703125f*diffV(v_x, v_y+3) + 0.0312500f*diffV(v_x, v_y+4);

    Expr momh1, ph, rh;
    Expr momv1, pv, rv;

    momh1 = ph = rh = 0;
    momv1 = pv = rv = 0;

    Expr mh = smoothedH(v_x, v_y);
    Expr mv = smoothedV(v_x, v_y);

    for(int m = -M; m <= M; m++) {
        momh1 += smoothedH(v_x+m, v_y);
        ph += smoothedH(v_x+m, v_y) * smoothedH(v_x+m, v_y);
        rh += (smoothedH(v_x+m, v_y) - diffH(v_x+m, v_y)) * (smoothedH(v_x+m, v_y) - diffH(v_x+m, v_y));

        momv1 += smoothedV(v_x+m, v_y);
        pv += smoothedV(v_x+m, v_y) * smoothedV(v_x+m, v_y);
        rv += (smoothedV(v_x+m, v_y) - diffV(v_x+m, v_y)) * (smoothedV(v_x+m, v_y) - diffV(v_x+m, v_y));
    }

    Expr Ph = ph / (2*M) - momh1*momh1 / (2*M*(2*M + 1));
    Expr Rh = rh / (2*M + 1) + DivEpsilon;
    Expr h = mh + (Ph / (Ph + Rh)) * (diffH(v_x, v_y) - mh);
    Expr H = Ph - (Ph / (Ph + Rh)) * Ph + DivEpsilon;

    Expr Pv = pv / (2*M) - momv1*momv1 / (2*M*(2*M + 1));
    Expr Rv = rv / (2*M + 1) + DivEpsilon;
    Expr v = mv + (Pv / (Pv + Rv)) * (diffV(v_x, v_y) - mv);
    Expr V = Pv - (Pv / (Pv + Rv)) * Pv + DivEpsilon;

    Expr interp = input(v_x, v_y) + (V*h + H*v) / (H + V);

    greenIntermediate(v_x, v_y) = select(
        ((v_x + v_y) & 1) == 1,
            input(v_x, v_y),
            saturating_cast<int16_t>(interp + 0.5f));

    Func filtered{"greenFiltered"};

    weightedMedianFilter(output, greenIntermediate);    
}

void Demosaic::calculateGreen(Func& output, Func input) {
    // Estimate green channel first
    Expr g14 = input(v_x + 0, v_y - 3);
    Expr g23 = input(v_x - 1, v_y - 2);
    Expr g25 = input(v_x + 1, v_y - 2);
    Expr g32 = input(v_x - 2, v_y - 1);
    Expr g34 = input(v_x + 0, v_y - 1);
    Expr g36 = input(v_x + 2, v_y - 1);
    Expr g41 = input(v_x - 3, v_y + 0);
    Expr g43 = input(v_x - 1, v_y + 0);
    
    Expr g45 = input(v_x + 1, v_y + 0);
    Expr g47 = input(v_x + 3, v_y + 0);
    Expr g52 = input(v_x - 2, v_y + 1);
    Expr g54 = input(v_x + 0, v_y + 1);
    Expr g56 = input(v_x + 2, v_y + 1);
    Expr g63 = input(v_x - 1, v_y + 2);
    Expr g65 = input(v_x + 1, v_y + 2);
    Expr g74 = input(v_x + 0, v_y + 3);
    
    Expr b24 = input(v_x + 0, v_y - 2);
    Expr b42 = input(v_x - 2, v_y + 0);
    Expr b44 = input(v_x + 0, v_y + 0);
    Expr b46 = input(v_x + 2, v_y + 0);
    Expr b64 = input(v_x + 0, v_y + 2);
    
    Expr w0 = 1.0f / (1.0f + abs(g54 - g34) + abs(g34 - g14) + abs(b44 - b24) + abs((g43 - g23) / 2) + abs((g45 - g25) / 2));
    Expr w1 = 1.0f / (1.0f + abs(g45 - g43) + abs(g43 - g41) + abs(b44 - b42) + abs((g34 - g32) / 2) + abs((g54 - g52) / 2));
    Expr w2 = 1.0f / (1.0f + abs(g43 - g45) + abs(g45 - g47) + abs(b44 - b46) + abs((g34 - g36) / 2) + abs((g54 - g56) / 2));
    Expr w3 = 1.0f / (1.0f + abs(g34 - g54) + abs(g54 - g74) + abs(b44 - b64) + abs((g43 - g63) / 2) + abs((g45 - g65) / 2));
    
    Expr g0 = g34 + (b44 - b24) / 2;
    Expr g1 = g43 + (b44 - b42) / 2;
    Expr g2 = g45 + (b44 - b46) / 2;
    Expr g3 = g54 + (b44 - b64) / 2;
    
    Expr interp = (w0*g0 + w1*g1 + w2*g2 + w3*g3) / (w0 + w1 + w2 + w3);
    
    greenIntermediate(v_x, v_y) = select(((v_x + v_y) & 1) == 1, input(v_x, v_y), cast<int16_t>(interp + 0.5f));

    Func filtered{"greenFiltered"};

    weightedMedianFilter(output, greenIntermediate);
}

void Demosaic::calculateRed(Func& output, Func input, Func green) {
    redI(v_x, v_y) = (select(v_y % 2 == 0,  select(v_x % 2 == 0, cast<int32_t>(input(v_x, v_y)) - cast<int32_t>(green(v_x, v_y)), 0),
                                            0));

    redBlurX(v_x, v_y) = (
        1 * redI(v_x - 1, v_y) +
        2 * redI(v_x    , v_y) +
        1 * redI(v_x + 1, v_y)
    );

    redIntermediate(v_x, v_y) =
        (
          1 * redBlurX(v_x, v_y - 1) +
          2 * redBlurX(v_x, v_y)     +
          1 * redBlurX(v_x, v_y + 1)             
        ) / 4;

    medianFilter(redFiltered, redIntermediate);

    output(v_x, v_y) = saturating_cast<int16_t>(green(v_x, v_y) + redFiltered(v_x, v_y));
}

void Demosaic::calculateBlue(Func& output, Func input, Func green) {
    blueI(v_x, v_y) = select(v_y % 2 == 0, 0,
                                           select(v_x % 2 == 0, 0, cast<int32_t>(input(v_x, v_y)) - cast<int32_t>(green(v_x, v_y))));

    blueBlurX(v_x, v_y) = (
        1 * blueI(v_x - 1, v_y) +
        2 * blueI(v_x    , v_y) +
        1 * blueI(v_x + 1, v_y)
    );

    blueIntermediate(v_x, v_y) =
        (
          1 * blueBlurX(v_x, v_y - 1) +
          2 * blueBlurX(v_x, v_y)     +
          1 * blueBlurX(v_x, v_y + 1)             
        ) / 4;

    medianFilter(blueFiltered, blueIntermediate);

    output(v_x, v_y) = saturating_cast<int16_t>(green(v_x, v_y) + blueFiltered(v_x, v_y));
}

void Demosaic::generate() {
    clamped0(v_x, v_y) = in0(clamp(v_x, 0, width - 1), clamp(v_y, 0, height - 1));
    clamped1(v_x, v_y) = in1(clamp(v_x, 0, width - 1), clamp(v_y, 0, height - 1));
    clamped2(v_x, v_y) = in2(clamp(v_x, 0, width - 1), clamp(v_y, 0, height - 1));
    clamped3(v_x, v_y) = in3(clamp(v_x, 0, width - 1), clamp(v_y, 0, height - 1));

    linearScale(shadingMap0, inShadingMap0, shadingMapWidth, shadingMapHeight, width, height);
    linearScale(shadingMap1, inShadingMap1, shadingMapWidth, shadingMapHeight, width, height);
    linearScale(shadingMap2, inShadingMap2, shadingMapWidth, shadingMapHeight, width, height);
    linearScale(shadingMap3, inShadingMap3, shadingMapWidth, shadingMapHeight, width, height);

    rearrange(shadingMapArranged, shadingMap0, shadingMap1, shadingMap2, shadingMap3, sensorArrangement);

    Func input{"input"};

    input(v_x, v_y, v_c) =
        mux(v_c,
            {   clamped0(v_x, v_y),
                clamped1(v_x, v_y),
                clamped2(v_x, v_y),
                clamped3(v_x, v_y) });

    // Suppress hot pixels
    Expr a0 = input(v_x - 1, v_y,       v_c);
    Expr a1 = input(v_x + 1, v_y,       v_c);
    Expr a2 = input(v_x,     v_y + 1,   v_c);
    Expr a3 = input(v_x,     v_y - 1,   v_c);

    cmpSwap(a0, a1);
    cmpSwap(a2, a3);
    cmpSwap(a0, a2);
    cmpSwap(a1, a3);
    cmpSwap(a1, a2);

    Expr threshold = 4*((cast<int32_t>(a1) + cast<int32_t>(a2)) / 2) / 2;

    shaded(v_x, v_y, v_c) = cast<int16_t>( clamp( clamp(cast<int32_t>(input(v_x, v_y, v_c)), 0, threshold) * shadingMapArranged(v_x, v_y, v_c) + 0.5f, 0, range) );

    // Combined image
    combinedInput(v_x, v_y) =
        select(v_y % 2 == 0,
               select(v_x % 2 == 0, shaded(v_x/2, v_y/2, 0), shaded(v_x/2, v_y/2, 1)),
               select(v_x % 2 == 0, shaded(v_x/2, v_y/2, 2), shaded(v_x/2, v_y/2, 3)));

    bayerInput(v_x, v_y) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                combinedInput(v_x, v_y),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                combinedInput(v_x - 1, v_y),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                combinedInput(v_x, v_y - 1),

                // BGGR
                combinedInput(v_x - 1, v_y - 1));

    calculateGreen(green, bayerInput);
    calculateRed(red, bayerInput, green);
    calculateBlue(blue, bayerInput, green);

    demosaicOutput(v_x, v_y, v_c) = select( v_c == 0, red(v_x, v_y),
                                            v_c == 1, green(v_x, v_y),
                                                      blue(v_x, v_y));

    // Transform to sRGB space
    linear(v_x, v_y, v_c) =  demosaicOutput(v_x, v_y, v_c) / cast<float>(range);

    colorCorrectInput(v_x, v_y, v_c) =
        select( v_c == 0, clamp( linear(v_x, v_y, 0), 0.0f, asShotVector[0] ),
                v_c == 1, clamp( linear(v_x, v_y, 1), 0.0f, asShotVector[1] ),
                          clamp( linear(v_x, v_y, 2), 0.0f, asShotVector[2] ));

    transform(colorCorrected, colorCorrectInput, cameraToSrgb);

    output(v_x, v_y, v_c) = saturating_cast<uint16_t>(colorCorrected(v_x, v_y, v_c) * 65535 + 0.5f);

    range.set_estimate(32767);
    sensorArrangement.set_estimate(0);

    in0.set_estimates({{0, 2048}, {0, 1536}});
    in1.set_estimates({{0, 2048}, {0, 1536}});
    in2.set_estimates({{0, 2048}, {0, 1536}});
    in3.set_estimates({{0, 2048}, {0, 1536}});

    inShadingMap0.set_estimates({{0, 17}, {0, 13}});
    inShadingMap1.set_estimates({{0, 17}, {0, 13}});
    inShadingMap2.set_estimates({{0, 17}, {0, 13}});
    inShadingMap3.set_estimates({{0, 17}, {0, 13}});

    width.set_estimate(2048);
    height.set_estimate(1536);
    shadingMapWidth.set_estimate(17);
    shadingMapHeight.set_estimate(13);

    cameraToSrgb.set_estimates({{0, 3}, {0, 3}});

    asShotVector.set_estimate(0, 1.0f);
    asShotVector.set_estimate(1, 1.0f);
    asShotVector.set_estimate(2, 1.0f);

    output.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});

    if(!auto_schedule)
        apply_auto_schedule();
}

void Demosaic::schedule() {
}

void Demosaic::apply_auto_schedule() {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;

    Var c = v_c;
    Var x = v_x;
    Var y = v_y;

    Var xi("xi");
    Var xii("xii");
    Var xiii("xiii");
    Var yi("yi");
    Var yii("yii");
    Var yiii("yiii");

    output
        .split(x, x, xi, 1024, TailStrategy::ShiftInwards)
        .split(y, y, yi, 96, TailStrategy::ShiftInwards)
        .split(yi, yi, yii, 12, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 256, TailStrategy::ShiftInwards)
        .split(yii, yii, yiii, 4, TailStrategy::ShiftInwards)
        .split(xii, xii, xiii, 16, TailStrategy::ShiftInwards)
        .vectorize(xiii)
        .compute_root()
        .reorder({xiii, xii, c, yiii, yii, xi, yi, x, y})
        .fuse(x, y, x)
        .parallel(x);
    colorCorrected
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 8, TailStrategy::ShiftInwards)
        .unroll(xi)
        .vectorize(xii)
        .compute_at(output, yiii)
        .reorder({xii, xi, c, x, y});
    // XYZ
    //     .store_in(MemoryType::Stack)
    //     .split(x, x, xi, 8, TailStrategy::ShiftInwards)
    //     .unroll(x)
    //     .unroll(c)
    //     .vectorize(xi)
    //     .compute_at(colorCorrected, c)
    //     .store_at(colorCorrected, x)
    //     .reorder({xi, x, y, c});
    colorCorrectInput
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(output, yiii)
        .store_at(output, yii)
        .reorder({xi, x, y, c});
    linear
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 128, TailStrategy::RoundUp)
        .split(xi, xi, xii, 16, TailStrategy::RoundUp)
        .unroll(xi)
        .vectorize(xii)
        .compute_at(output, yiii)
        .reorder({xii, xi, c, x, y});
    red
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .unroll(x)
        .vectorize(xi)
        .compute_at(linear, c)
        .store_at(linear, x)
        .reorder({xi, x, y});
    redIntermediate
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .split(y, y, yi, 3, TailStrategy::RoundUp)
        .unroll(yi)
        .vectorize(xi)
        .compute_at(output, yii)
        .store_at(output, xi)
        .reorder({xi, yi, x, y});
    redBlurX
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .unroll(y)
        .vectorize(xi)
        .compute_at(redIntermediate, x)
        .reorder({xi, x, y});
    redI
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(output, yii)
        .store_at(output, xi)
        .reorder({xi, x, y});
    blue
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(output, yi)
        .reorder({xi, x, y});
    blueIntermediate
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 32, TailStrategy::RoundUp)
        .split(y, y, yi, 2, TailStrategy::RoundUp)
        .split(xi, xi, xii, 16, TailStrategy::RoundUp)
        .unroll(xi)
        .vectorize(xii)
        .compute_at(output, yi)
        .store_at(output, x)
        .reorder({xii, xi, yi, y, x});
    blueBlurX
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .unroll(y)
        .vectorize(xi)
        .compute_at(blueIntermediate, yi)
        .store_at(blueIntermediate, y)
        .reorder({xi, x, y});
    blueI
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .unroll(x)
        .unroll(y)
        .vectorize(xi)
        .compute_at(blueIntermediate, y)
        .store_at(blueIntermediate, x)
        .reorder({xi, x, y});
    green
        .split(x, x, xi, 272, TailStrategy::RoundUp)
        .split(y, y, yi, 50, TailStrategy::RoundUp)
        .split(xi, xi, xii, 16, TailStrategy::RoundUp)
        .vectorize(xii)
        .compute_at(output, x)
        .reorder({xii, xi, yi, x, y});
    greenIntermediate
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(green, x)
        .reorder({xi, x, y});
    bayerInput
        .split(y, y, yi, 8, TailStrategy::RoundUp)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(output, x)
        .reorder({xi, x, yi, y});
    combinedInput
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::RoundUp)
        .vectorize(xi)
        .compute_at(bayerInput, y)
        .reorder({xi, x, y});
    shaded
        .split(x, x, xi, 32, TailStrategy::ShiftInwards)
        .split(xi, xi, xii, 16, TailStrategy::ShiftInwards)
        .unroll(c)
        .vectorize(xii)
        .compute_at(output, x)
        .reorder({xii, c, xi, y, x});
    shadingMapArranged
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 8, TailStrategy::RoundUp)
        .unroll(x)
        .unroll(c)
        .vectorize(xi)
        .compute_at(shaded, xi)
        .reorder({xi, x, y, c});
    shadingMap3
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, x)
        .reorder({xi, x, y});
    shadingMap2
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, x)
        .reorder({xi, x, y});
    shadingMap1
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, x)
        .reorder({xi, x, y});
    shadingMap0
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, x)
        .reorder({xi, x, y});
    clamped3
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, xi)
        .reorder({xi, x, y});
    clamped2
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, xi)
        .reorder({xi, x, y});
    clamped1
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, xi)
        .reorder({xi, x, y});
    clamped0
        .store_in(MemoryType::Stack)
        .split(x, x, xi, 16, TailStrategy::ShiftInwards)
        .vectorize(xi)
        .compute_at(shaded, xi)
        .reorder({xi, x, y});
}

//

class TonemapGenerator : public Halide::Generator<TonemapGenerator> {
public:
    // Inputs and outputs
    GeneratorParam<int> tonemap_levels {"tonemap_levels", 9};
    GeneratorParam<Type> output_type{"output_type", UInt(16)};

    Input<Func> input0{"input0", 3 };
    Input<Func> input1{"input1", 3 };

    Output<Func> output{ "output", 3 };

    Input<int> width {"width"};
    Input<int> height {"height"};

    Input<float> variance {"variance"};
    Input<float> gain {"gain"};
    
    // //

    Var v_i{"i"};
    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};
    
    Var v_xo{"xo"};
    Var v_xi{"xi"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};

    Var v_xio{"xio"};
    Var v_xii{"xii"};
    Var v_yio{"yio"};
    Var v_yii{"yii"};

    Var subtile_idx{"subtile_idx"};
    Var tile_idx{"tile_idx"};

    void pyramidUp(Func& output, Type outputType, Func& intermediate, Func input);
    void pyramidDown(Func& output, Type outputType, Func& intermediate, Func input);
    
    vector<pair<Func, Func>> buildPyramid(Func input, Type outputType, int maxlevel);

    void generate();
    void schedule();

    vector<pair<Func, Func>> tonemapPyramid;
    vector<pair<Func, Func>> weightsPyramid;
};

void TonemapGenerator::pyramidUp(Func& output, Type outputType, Func& intermediate, Func input) {
    using Halide::_;

    Func blurX("blurX");
    Func blurY("blurY");

    // Insert zeros and expand by factor of 2 in both dims
    Func expandedX{"expandedX"};
    Func expanded("expanded");

    Func pyramidUpInput{"pyramidUpInput"};

    pyramidUpInput(v_x, v_y, v_c, _) = cast<int32_t>(input(v_x, v_y, v_c, _));

    expandedX(v_x, v_y, v_c, _) = select((v_x % 2)==0, pyramidUpInput(v_x/2, v_y, v_c, _), 0);
    expanded(v_x, v_y, v_c, _)  = select((v_y % 2)==0, expandedX(v_x, v_y/2, v_c, _), 0);

    blurX(v_x, v_y, v_c, _) =
         (
          1 * expanded(v_x - 1, v_y, v_c, _) +
          2 * expanded(v_x,     v_y, v_c, _) +
          1 * expanded(v_x + 1, v_y, v_c, _)
          ) >> 2;

    blurY(v_x, v_y, v_c, _) =
         (
          1 * blurX(v_x, v_y - 1, v_c, _) +
          2 * blurX(v_x, v_y,     v_c, _) +
          1 * blurX(v_x, v_y + 1, v_c, _)
          ) >> 2;

    intermediate = blurX;
    output(v_x, v_y, v_c, _) = cast(outputType, 4 * blurY(v_x, v_y, v_c, _));
}

void TonemapGenerator::pyramidDown(Func& output, Type outputType, Func& intermediate, Func input) {
    using Halide::_;

    Func blurX{"pyramidDownBlurX"}, blurY{"pyramidDownBlurY"};

    blurX(v_x, v_y, v_c, _) =
         (
          1 * cast<int32_t>(input(v_x - 1, v_y, v_c, _)) +
          2 * cast<int32_t>(input(v_x,     v_y, v_c, _)) +
          1 * cast<int32_t>(input(v_x + 1, v_y, v_c, _))
          ) >> 2;

    blurY(v_x, v_y, v_c, _) = 
         (
          1 * blurX(v_x, v_y - 1, v_c, _) +
          2 * blurX(v_x, v_y,     v_c, _) +
          1 * blurX(v_x, v_y + 1, v_c, _)
          ) >> 2;

    intermediate = blurX;
    output(v_x, v_y, v_c, _) = cast(outputType, blurY(v_x * 2, v_y * 2, v_c, _));
}

vector<pair<Func, Func>> TonemapGenerator::buildPyramid(Func input, Type outputType, int maxlevel) {
    vector<pair<Func, Func>> pyramid;

    for(int level = 1; level <= maxlevel; level++) {
        Func pyramidDownOutput(input.name() + std::string("PyramidDownLvl") + std::to_string(level));
        Func pyramidDownIntermediate(input.name() + std::string("PyramidDownIntermediateLvl") + std::to_string(level));
        
        Func inClamped;

        if(level == 1) {
            inClamped = BoundaryConditions::repeat_edge(input, { {0, width}, {0, height} } );

            pyramid.push_back(std::make_pair(inClamped, inClamped));
        }
        else {
            inClamped = BoundaryConditions::repeat_edge(pyramid[level - 1].second, { {0, width >> (level-1)}, {0, height >> (level-1)} } );
        }

        pyramidDown(pyramidDownOutput, outputType, pyramidDownIntermediate, inClamped);
        
        pyramid.push_back(std::make_pair(pyramidDownIntermediate, pyramidDownOutput));
    }

    return pyramid;
}

void TonemapGenerator::generate() {
    Func gammaLut{"gammaLut"}, inverseGammaLut{"inverseGammaLut"};
    Expr type_max = ((Type)output_type).max();

    Expr h = v_x / 65535.0f;

    gammaLut(v_x) = saturating_cast(output_type, select(h < 0.0031308f, h * 12.92f, pow(h, 1.0f / 2.4f) * 1.055f - 0.055f) * type_max);
    inverseGammaLut(v_x) = saturating_cast(output_type, select(h < 0.04045f, h / 12.92f, pow((h + 0.055f) / 1.055f, 2.4f)) * type_max);

    if(!auto_schedule) {
        gammaLut.compute_root().vectorize(v_x, 16);
        inverseGammaLut.compute_root().vectorize(v_x, 16);
    }

    // Create exposures
    Func exposures{"exposures"};
    Func weightsLut{"weightsLut"};
    Func weights{"weights"};
    Func weightsNormalized{"weightsNormalized"};
    Func Yinput{"Yinput"};

    Expr ia = input0(v_x, v_y, v_c);
    Expr ib = cast(output_type, clamp(cast<float>(input0(v_x, v_y, v_c)) * gain, 0.0f, type_max));
    Expr ic = input1(v_x, v_y, v_c);

    exposures(v_x, v_y, v_c, v_i) = gammaLut(select(v_i == 0, ia,
                                                    v_i == 1, ib,
                                                              ic));

    // Create weights LUT based on well exposed pixels
    Expr wa = v_i / cast<float>(type_max) - 0.5f;
    Expr wb = -pow(wa, 2) / (2 * variance * variance);
    
    weightsLut(v_i) = cast<int16_t>(clamp(exp(wb) * 32767, -32767, 32767));
    
    if(!auto_schedule) {
        weightsLut.compute_root().vectorize(v_i, 8);
    }

    Yinput(v_x, v_y, v_i) = saturating_cast<uint16_t>(0.299f*exposures(v_x, v_y, 0, v_i) + 0.587f*exposures(v_x, v_y, 1, v_i) + 0.114f*exposures(v_x, v_y, 2, v_i));

    weights(v_x, v_y, v_i) = weightsLut(cast<uint16_t>(Yinput(v_x, v_y, v_i))) / 32767.0f;
    weightsNormalized(v_x, v_y, v_i) = cast<uint16_t>(16384.0f * weights(v_x, v_y, v_i) / (1e-12f + weights(v_x, v_y, 0) + weights(v_x, v_y, 1) + weights(v_x, v_y, 2)));

    if(!auto_schedule) {
        weightsNormalized
            .compute_root()
            .reorder(v_i, v_x, v_y)
            .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 64, 16)
            .fuse(v_xo, v_yo, tile_idx)
            .parallel(tile_idx)
            .unroll(v_i)
            .vectorize(v_xi, 8);

        exposures.in(Yinput)
            .compute_at(weightsNormalized, tile_idx)
            .vectorize(v_x, 8);
    }

    // Create pyramid input
    tonemapPyramid = buildPyramid(exposures, UInt(16), tonemap_levels);
    weightsPyramid = buildPyramid(weightsNormalized, UInt(16), tonemap_levels);

    if(!auto_schedule) {
        for(int level = 0; level < tonemap_levels; level++) {

            if(level == 0) {
                tonemapPyramid[0].second.in(tonemapPyramid[1].first)
                    .compute_at(tonemapPyramid[1].second, v_y)
                    .vectorize(v_x, 8)
                    .unroll(v_i)
                    .unroll(v_c);
            }
            else {
                tonemapPyramid[level].first
                    .compute_at(tonemapPyramid[level].second, v_y)
                    .unroll(v_c)
                    .unroll(_0)
                    .vectorize(v_x, 8);
            
                tonemapPyramid[level].second
                    .compute_root()
                    .vectorize(v_x, 8)
                    .unroll(v_c)
                    .unroll(_0)
                    .parallel(v_y);

                weightsPyramid[level].first
                    .compute_at(weightsPyramid[level].second, v_y)
                    .unroll(v_c)
                    .vectorize(v_x, 8);
    
                weightsPyramid[level].second
                    .compute_root()
                    .vectorize(v_x, 8)
                    .unroll(v_c)
                    .parallel(v_y);
            }
        }
    }

    //
    // Create laplacian pyramid
    //

    vector<Func> laplacianPyramid, combinedPyramid;
    
    for(int level = 0; level < tonemap_levels; level++) {
        Func up("laplacianUpLvl" + std::to_string(level));
        Func upIntermediate("laplacianUpIntermediateLvl" + std::to_string(level));
        Func laplacian("laplacianLvl" + std::to_string(level));

        pyramidUp(up, Int(32), upIntermediate, tonemapPyramid[level + 1].second);
        
        laplacian(v_x, v_y, v_c, v_i) = cast<int32_t>(tonemapPyramid[level].second(v_x, v_y, v_c, v_i)) - up(v_x, v_y, v_c, v_i);

        if(level > 2) {
            upIntermediate
                .compute_at(laplacian, tile_idx)
                .vectorize(v_x, 8);

            laplacian
                .compute_root()
                .reorder(v_i, v_c, v_x, v_y)
                .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 64, 16)
                .fuse(v_xo, v_yo, tile_idx)
                .parallel(tile_idx)
                .unroll(v_c)
                .unroll(v_i)
                .vectorize(v_xi, 8);
        }

        laplacianPyramid.push_back(laplacian);
    }

    laplacianPyramid.push_back(tonemapPyramid[tonemap_levels].second);

    //
    // Combine pyramids
    //

    for(int level = 0; level <= tonemap_levels; level++) {
        Func result("resultLvl" + std::to_string(level));

        result(v_x, v_y, v_c) = cast<int32_t>(0.5f + 
            (laplacianPyramid[level](v_x, v_y, v_c, 0) * 1.0f/16384.0f*weightsPyramid[level].second(v_x, v_y, 0)) +
            (laplacianPyramid[level](v_x, v_y, v_c, 1) * 1.0f/16384.0f*weightsPyramid[level].second(v_x, v_y, 1)) +
            (laplacianPyramid[level](v_x, v_y, v_c, 2) * 1.0f/16384.0f*weightsPyramid[level].second(v_x, v_y, 2)));

        combinedPyramid.push_back(result);
    }

    //
    // Create output pyramid
    //
    
    vector<Func> outputPyramid;

    for(int level = tonemap_levels; level > 0; level--) {
        Func up("outputUpLvl" + std::to_string(level));
        Func upIntermediate("outputUpIntermediateLvl" + std::to_string(level));
        Func outputLvl("outputLvl" + std::to_string(level));

        if(level == tonemap_levels) {
            pyramidUp(up, Int(32), upIntermediate, combinedPyramid[level]);
        }
        else {
            pyramidUp(up, Int(32), upIntermediate, outputPyramid[outputPyramid.size() - 1]);
            
        }

        outputLvl(v_x, v_y, v_c) = saturating_cast<uint16_t>(combinedPyramid[level - 1](v_x, v_y, v_c) + up(v_x, v_y, v_c));

        if(!auto_schedule) {
            combinedPyramid[level - 1].compute_at(outputLvl, tile_idx)
                .vectorize(v_x, 8);

            upIntermediate.compute_at(outputLvl, tile_idx)
                .vectorize(v_x, 8);

            outputLvl
                .compute_root()
                .reorder(v_c, v_x, v_y)
                .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 64, 16)
                .fuse(v_xo, v_yo, tile_idx)
                .parallel(tile_idx)
                .unroll(v_c)
                .vectorize(v_xi, 8);
        }

        outputPyramid.push_back(outputLvl);
    }

    // Inverse gamma correct tonemapped result
    output(v_x, v_y, v_c) = inverseGammaLut(cast(output_type, clamp(outputPyramid[tonemap_levels - 1](v_x, v_y, v_c), 0, type_max)));

    if(!auto_schedule) {
        output
            .compute_root()
            .bound(v_c, 0, 3)
            .parallel(v_y)
            .unroll(v_c)
            .vectorize(v_x, 8);
    }

    width.set_estimate(4096);
    height.set_estimate(3072);
    variance.set_estimate(0.25f);
    gain.set_estimate(8.0f);

    input0.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});
    input1.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});
    output.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});
}

void TonemapGenerator::schedule() {    
}

class EnhanceGenerator : public Halide::Generator<EnhanceGenerator>, public PostProcessBase {
public:
    Input<Func> input{"input", 3 };
    Input<Func> chromaDenoiseEps{"chromaDenoiseEps", 2 };

    Output<Func> output{ "enhanceOutput", 3 };

    GeneratorParam<int> popRadius{"popRadius", 25};

    GeneratorParam<bool> denoiseChroma{"denoiseChroma", true};
    GeneratorParam<bool> enableSharpen{"enableSharpen", true};

    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<float> blackPoint{"blackPoint"};
    Input<float> whitePoint{"whitePoint"};
    Input<float> contrast{"contrast"};
    Input<float> brightness{"brightness"};
    Input<float> blues{"blues"};
    Input<float> greens{"greens"};
    Input<float> saturation{"saturation"};
    Input<float> sharpen0{"sharpen0"};
    Input<float> sharpen1{"sharpen1"};
    Input<float> pop{"pop"};

    Func linearRgb{"linearRgb"};
    Func sharpenInput{"sharpenInput"};
    Func gammaCorrected{"gammaCorrected"};
    Func gammaCorrectedLut{"gammaCorrectedLut"};
    Func contrastCurve{"contrastCurve"};
    Func sharpened{"sharpened"};
    Func finalTonemap{"finalTonemap"};
    Func bgrInput{"bgrInput"};
    Func hsvInput{"hsvInput"};
    Func hsvOutput{"hsvOutput"};
    Func hsv{"hsv"};
    Func estimateInput{"estimateInput"};    
    Func saturationApplied{"saturationApplied"};
    Func finalRgb{"finalRgb"};
    Func contrastLut{"contrastLut"};
    Func gaussianDiff0{"gaussianDiff0"}, gaussianDiff1{"gaussianDiff1"}, gaussianDiff2{"gaussianDiff2"};
    Func m{"m"};
    Func M{"M"};
    Func N{"N"};
    Func S{"S"};
    Func blurOutput{"blurOutput"};
    Func blurOutputTmp{"blurOutputTmp"};
    Func blurOutput2{"blurOutput2"};
    Func blurOutput2Tmp{"blurOutput2Tmp"};    
    Func finalOutput{"finalOutput"};
    Func blackPointAdjusted{"blackPointAdjusted"};
    Func blackPointLut{"blackPointLut"};
    Func estimates{"estimates"};
    Func presetBlackPoint{"presetBlackPoint"};
    Func presetWhitePoint{"presetWhitePoint"};
    Func downsampledTmp{"downsampledTmp"};
    Func downsampled{"downsampled"};
    Func upsampleTmp{"upsampleTmp"};
    Func upsampled{"upsampled"};
    Func localContrast{"localContrast"};
    Func inverseGammaLut{"inverseGammaLut"};

    void generate();
    void schedule_for_cpu();

private:
    void sharpen(Func& output, Func input);
};

void EnhanceGenerator::sharpen(Func& output, Func input) {
    if(popRadius < 11) {
        blur(blurOutput, blurOutputTmp, input);
        blur(blurOutput2, blurOutput2Tmp, blurOutput);
    }
    else {
        blur2(blurOutput, blurOutputTmp, input);
        blur3(blurOutput2, blurOutput2Tmp, blurOutput);
    }

    gaussianDiff0(v_x, v_y) = cast<int32_t>(input(v_x, v_y)) - blurOutput(v_x, v_y);
    gaussianDiff1(v_x, v_y) = cast<int32_t>(blurOutput(v_x, v_y))  - blurOutput2(v_x, v_y);

    output(v_x, v_y) = saturating_cast<uint16_t>(
        blurOutput2(v_x, v_y) + sharpen0*gaussianDiff0(v_x, v_y) + sharpen1*gaussianDiff1(v_x, v_y) + 0.5f);
}

void EnhanceGenerator::generate() {

    // Apply black/white point + contrast curve
    {

        // Gamma
        Expr i = v_i / 65535.0f;
        Expr j = select(i < 0.0031308f, i * 12.92f, pow(i, 1.0f / 2.4f) * 1.055f - 0.055f);

        // Midtones
        Expr k = pow(j, 1.0f/brightness);

        // Contrast
        Expr K = max(1e-05f, contrast);

        Expr A = K*6.0f;
        Expr B = K*4.0f;

        Expr m = 1.0f / (1 + exp(B));
        Expr n = 1.0f / (1 + exp(-A + B)) - m;
        
        Expr s = 1.0f / (1.0f + exp(-A*k + B));
        Expr t = (s - m) / n;

        // Black/white point
        Expr u = clamp(t - blackPoint, 0.0f, 1.0f) * (1.0f / (1.0f - blackPoint + 1e-5f));
        Expr v = u / whitePoint;

        contrastLut(v_i) = saturating_cast<uint16_t>(v*65535.0f+0.5f);
        if(!auto_schedule)
            contrastLut.compute_root().vectorize(v_i, 8);
    }

    gammaCorrected(v_x, v_y, v_c) = contrastLut(input(v_x, v_y, v_c));

    // Denoise chroma
    if(denoiseChroma) {
        Func RGBInput{"RGBInput"};
        Func YCbCrOutput{"YCbCrOutput"};
        Func chromaInput{"chromaInput"};
        Func chromaDenoised{"chromaDenoised"};

        RGBInput(v_x, v_y, v_c) = gammaCorrected(v_x, v_y, v_c) / 65535.0f;

        RGBToYCbCr(YCbCrOutput, RGBInput);

        chromaInput(v_x, v_y, v_c) = saturating_cast<uint16_t>(
            select( v_c == 0, YCbCrOutput(v_x, v_y, v_c) * 65535.0f + 0.5f,
                    v_c == 1, (YCbCrOutput(v_x, v_y, v_c) + 0.5f) * 65535.0f + 0.5f,
                              (YCbCrOutput(v_x, v_y, v_c) + 0.5f) * 65535.0f + 0.5f));

        chromaInput
            .compute_root()
            .parallel(v_y)
            .vectorize(v_x, 8)
            .unroll(v_c);

        auto gf0 = create<GuidedFilter>();
        auto gf1 = create<GuidedFilter>();

        gf0->radius.set(15);
        gf0->output_type.set(UInt(16));
        gf0->apply(chromaInput, chromaDenoiseEps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(1));

        gf1->radius.set(15);
        gf1->output_type.set(UInt(16));
        gf1->apply(chromaInput, chromaDenoiseEps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(2));

        auto gf2 = create<GuidedFilter>();
        auto gf3 = create<GuidedFilter>();

        Func secondPass{"secondPass"};

        secondPass(v_x, v_y, v_c) = select(v_c == 0, gf0->output(v_x, v_y), gf1->output(v_x, v_y));

        gf2->radius.set(15);
        gf2->output_type.set(UInt(16));
        gf2->apply(secondPass, chromaDenoiseEps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(0));

        gf3->radius.set(15);
        gf3->output_type.set(UInt(16));
        gf3->apply(secondPass, chromaDenoiseEps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(1));

        auto gf4 = create<GuidedFilter>();
        auto gf5 = create<GuidedFilter>();

        Func thirdPass{"thirdPass"};

        thirdPass(v_x, v_y, v_c) = select(v_c == 0, gf2->output(v_x, v_y), gf3->output(v_x, v_y));

        gf4->radius.set(25);
        gf4->output_type.set(UInt(16));
        gf4->apply(thirdPass, chromaDenoiseEps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(0));

        gf5->radius.set(25);
        gf5->output_type.set(UInt(16));
        gf5->apply(thirdPass, chromaDenoiseEps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(1));

        chromaDenoised(v_x, v_y, v_c) = select(
            v_c == 0, YCbCrOutput(v_x, v_y, v_c),
            v_c == 1, gf4->output(v_x, v_y) / 65535.0f - 0.5f,
                      gf5->output(v_x, v_y) / 65535.0f - 0.5f);

        YCbCrToRGB(hsvInput, chromaDenoised);
    }
    else {
        hsvInput(v_x, v_y, v_c) = gammaCorrected(v_x, v_y, v_c) / 65535.0f;
    }

    RGBToHSV(hsvOutput, hsvInput);

    //
    // Saturation/hue
    //

    shiftHues(saturationApplied, hsvOutput, blues, greens, saturation);

    //
    // Sharpen/Local contrast
    //

    if(enableSharpen) {
        sharpenInput(v_x, v_y) = saturating_cast<uint16_t>(hsvOutput(v_x, v_y, 2) * 65535.0f);

        sharpen(sharpened, sharpenInput);
        
        downsampled(v_x, v_y, v_c) = select(v_c == 0, downsample(sharpened, downsampledTmp)(v_x, v_y), 0);

        auto gf = create<GuidedFilter>();
        Func eps{"eps"};

        eps(v_x, v_y) = 0.2f*0.2f*65535.0f*65535.0f;

        gf->radius.set(popRadius);
        gf->output_type.set(UInt(16));
        gf->apply(downsampled, eps, cast<uint16_t>(width), cast<uint16_t>(height), cast<uint16_t>(0));

        upsampled = upsample(gf->output, upsampleTmp);

        localContrast(v_x, v_y) = saturating_cast<uint16_t>(0.5f + cast<float>(upsampled(v_x, v_y)) + pop*(cast<float>(sharpened(v_x, v_y)) - upsampled(v_x, v_y)));

        bgrInput(v_x, v_y, v_c) = select(
            v_c == 0, saturationApplied(v_x, v_y, v_c),
            v_c == 1, saturationApplied(v_x, v_y, v_c),
                      localContrast(v_x, v_y) / 65535.0f);
    }
    else {
        bgrInput(v_x, v_y, v_c) = saturationApplied(v_x, v_y, v_c);
    }

    HSVToBGR(finalRgb, bgrInput);

    {
        // Invert gamma
        Expr a = v_i / 65535.0f;
        Expr b = select(a < 0.04045f, a / 12.92f, pow((a + 0.055f) / 1.055f, 2.4f));
    
        inverseGammaLut(v_i) = saturating_cast<uint16_t>(b*65535.0f+0.5f);
        
        if(!auto_schedule)
            inverseGammaLut.compute_root().vectorize(v_i, 8);
    }

    output(v_x, v_y, v_c) = inverseGammaLut(saturating_cast<uint16_t>(finalRgb(v_x, v_y, v_c) * 65535.0f + 0.5f));

    contrast.set_estimate(1.5f);
    blackPoint.set_estimate(0.01f);
    whitePoint.set_estimate(0.95f);
    blues.set_estimate(1.0f);
    saturation.set_estimate(1.0f);
    greens.set_estimate(1.0f);
    sharpen0.set_estimate(2.0f);
    width.set_estimate(4000);
    height.set_estimate(3000);

    input.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});
    output.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});

    if(!auto_schedule)
        schedule_for_cpu();
}

void EnhanceGenerator::schedule_for_cpu() {
    gammaCorrected
        .compute_root()
        .unroll(v_c)
        .vectorize(v_x, 8)
        .parallel(v_y);

    if(enableSharpen) {
        blurOutputTmp
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        blurOutput
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        blurOutput2Tmp
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        blurOutput2
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        gaussianDiff0
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        gaussianDiff1
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        sharpenInput
            .compute_at(sharpened, tile_idx)
            .vectorize(v_x, 8);

        sharpened
            .compute_root()
            .reorder(v_x, v_y)
            .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 128, 128)
            .fuse(v_xo, v_yo, tile_idx)
            .parallel(tile_idx)
            .vectorize(v_xi, 8);

        downsampledTmp
            .compute_at(downsampled, v_x)
            .vectorize(v_x, 8);

        downsampled
            .compute_root()
            .split(v_y, v_yo, v_yi, 64)
            .vectorize(v_x, 8)
            .parallel(v_yo);

        upsampleTmp
            .compute_at(localContrast, v_x)
            .vectorize(v_x, 8);

        upsampled
            .compute_at(localContrast, v_x)
            .vectorize(v_x, 8);

        localContrast
            .compute_root()
            .vectorize(v_x, 8)
            .parallel(v_y);
    }

    saturationApplied
        .compute_at(output, v_x)
        .unroll(v_c)
        .vectorize(v_x, 8);

    finalRgb
        .compute_at(output, v_x)
        .unroll(v_c)
        .vectorize(v_x, 8);

    bgrInput
        .compute_at(output, v_x)
        .unroll(v_c)
        .vectorize(v_x, 8);

    output
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .vectorize(v_x, 8)
        .parallel(v_y);
}


class PostProcessGenerator : public Halide::Generator<PostProcessGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> in0{"in0", 2 };
    Input<Buffer<uint16_t>> in1{"in1", 2 };
    Input<Buffer<uint16_t>> in2{"in2", 2 };
    Input<Buffer<uint16_t>> in3{"in3", 2 };

    Input<Buffer<uint8_t>> blueNoise{"blueNoise", 3 };
    Input<Buffer<uint16_t>> hdrInput{"hdrInput", 3 };

    Input<bool> useHdr{"useHdr"};

    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<Buffer<float>> inShadingMap0{"inShadingMap0", 2 };
    Input<Buffer<float>> inShadingMap1{"inShadingMap1", 2 };
    Input<Buffer<float>> inShadingMap2{"inShadingMap2", 2 };
    Input<Buffer<float>> inShadingMap3{"inShadingMap3", 2 };

    Input<uint16_t> range{"range"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<float> shadows{"shadows"};
    Input<float> hdrInputGain{"hdrInputGain"};
    Input<float> hdrScale{"hdrScale"};
    Input<float> tonemapVariance{"tonemapVariance"};
    Input<float> blackPoint{"blackPoint"};
    Input<float> exposure{"exposure"};
    Input<float> whitePoint{"whitePoint"};
    Input<float> contrast{"contrast"};
    Input<float> brightness{"brightness"};
    Input<float> blues{"blues"};
    Input<float> greens{"greens"};
    Input<float> saturation{"saturation"};
    Input<float> sharpen0{"sharpen0"};
    Input<float> sharpen1{"sharpen1"};
    Input<float> pop{"pop"};
    Input<float> chromaEps0{"chromaEps0"};
    Input<float> chromaEps1{"chromaEps1"};
    Input<float> chromaEps3{"chromaEps3"};

    Output<Buffer<uint8_t>> output{"output", 3};
    
    Func chromaEpsMap{"chromaEpsMap"}, chromaEps{"chromaEps"};
    Func Lmap{"Lmap"};
    Func LmapTmp0{"LmapTmp0"}, LmapTmp1{"LmapTmp1"}, LmapTmp2{"LmapTmp2"}, LmapTmp3{"LmapTmp3"};

    // Func defringeVertical{"defringeVertical"};
    // Func defringeVerticalTransposed{"defringeVerticalTransposed"};
    // Func defringeHorizontal{"defringeHorizontal"};
    // Func defringe{"defringe"};

    Func colorCorrected{"colorCorrected"};
    Func hdrTonemapInput{"hdrTonemapInput"};
    Func hdrMask{"hdrMask"};
    Func highlights{"highlights"};
    Func YCbCr{"YCbCr"};
    Func linearRgb{"linearRgb"};
    Func tonemapped{"tonemapped"};
    Func tonemapInput{"tonemapInput"};
    Func hdrTonemapped{"hdrTonemapped"};
    Func downsampleTemp{"downsampleTemp"};
    Func upsampleTemp0{"upsampleTemp0"};
    Func upsampleTemp1{"upsampleTemp1"};
    Func enhanceInput{"enhanceInput"};    
    Func noiseInput{"noiseInput"};
    Func noise{"noise"};
    Func gammaLut{"gammaLut"};

    std::unique_ptr<Demosaic> demosaic;
    std::unique_ptr<TonemapGenerator> tonemap;
    std::unique_ptr<EnhanceGenerator> enhance;
    
    void generate();
    void schedule_for_gpu();
    void schedule_for_cpu();
    
private:
    void sharpen(Func sharpenInputY);
};

void PostProcessGenerator::generate()
{
    std::vector<Expr> asShot{ asShotVector[0], asShotVector[1], asShotVector[2] };

    Expr WIDTH = in0.width();
    Expr HEIGHT = in0.height();

    // Demosaic image
    demosaic = create<Demosaic>();
    demosaic->apply(
        in0, in1, in2, in3,
        inShadingMap0, inShadingMap1, inShadingMap2, inShadingMap3,
        WIDTH, HEIGHT,
        inShadingMap0.width(), inShadingMap0.height(),
        cast<float>(range),
        sensorArrangement,
        asShot,
        cameraToSrgb);

    // Calculate chroma denoising map
    Lmap(v_x, v_y) = cast<uint8_t>(0.5f + 255.0f*pow(demosaic->output(v_x, v_y, 1)/65535.0f, 1.0f/2.2f));
    chromaEpsMap(v_x, v_y) = cast<uint8_t>(upsample(upsample(downsample(downsample(Lmap, LmapTmp0), LmapTmp1), LmapTmp2), LmapTmp3)(v_x, v_y));
    
    Expr X = chromaEpsMap(v_x, v_y)/255.0f;
    Expr W = chromaEps1*exp(-chromaEps0*(X*X)) + 1.0f;
    Expr eps = W*chromaEps3;

    chromaEps(v_x, v_y) = 65535.0f*65535.0f*eps*eps;

    //
    // Merge HDR images
    // Only merge parts of the image which is overexposed with the HDR image
    //

    tonemapInput(v_x, v_y, v_c) = saturating_cast<uint16_t>(0.5f + pow(2.0f, exposure) * demosaic->output(v_x, v_y, v_c));

    Func hdrInputRepeated{"hdrInputRepeated"};
    Func baseInput{"baseInput"};
    Func Linput{"Linput"};
    Func Minput{"Minput"};

    baseInput(v_x, v_y, v_c) = demosaic->output(v_x, v_y, v_c)/65535.0f;
    hdrInputRepeated(v_x, v_y, v_c) = Halide::BoundaryConditions::repeat_edge(hdrInput)(v_x, v_y, v_c)/65535.0f;

    Expr L0 = clamp(1.0f/hdrScale * (0.299f*hdrInputRepeated(v_x, v_y, 0) + 0.587f*hdrInputRepeated(v_x, v_y, 1) + 0.114f*hdrInputRepeated(v_x, v_y, 2)), 0.0f, 1.0f);
    Expr L1 = 0.299f*baseInput(v_x, v_y, 0) + 0.587f*baseInput(v_x, v_y, 1) + 0.114f*baseInput(v_x, v_y, 2);

    Linput(v_x, v_y, v_c) = select(v_c == 0, L0, L1);
    Minput(v_x, v_y, v_c) = exp(-16.0f * (Linput(v_x, v_y, v_c) - 1.0f) * (Linput(v_x, v_y, v_c) - 1.0f));

    hdrMask(v_x, v_y) = Minput(v_x, v_y, 0) * Minput(v_x, v_y, 1);

    highlights(v_x, v_y, v_c) = (hdrMask(v_x, v_y)*hdrInputRepeated(v_x, v_y, v_c)) + ((1.0f - hdrMask(v_x, v_y))*hdrScale*baseInput(v_x, v_y, v_c));

    hdrTonemapInput(v_x, v_y, v_c) = select(useHdr, 
        saturating_cast<uint16_t>(hdrInputGain * highlights(v_x, v_y, v_c) * 65535.0f),
        tonemapInput(v_x, v_y, v_c));

    //
    // Tonemap
    //

    tonemap = create<TonemapGenerator>();

    tonemap->output_type.set(UInt(16));
    tonemap->tonemap_levels.set(TONEMAP_LEVELS);
    tonemap->apply(tonemapInput, hdrTonemapInput, WIDTH * 2, HEIGHT * 2, tonemapVariance, shadows);

    // defringeVertical.define_extern("extern_defringe", { (Func) tonemap->output, WIDTH*2, HEIGHT*2 }, UInt(16), 3);
    // defringeVertical.compute_root();

    // defringeVerticalTransposed(v_x, v_y, v_c) = defringeVertical(v_y, v_x, v_c);

    // defringeHorizontal.define_extern("extern_defringe", { defringeVerticalTransposed, HEIGHT*2, WIDTH*2 }, UInt(16), 3);
    // defringeHorizontal.compute_root();

    // defringe(v_x, v_y, v_c) = defringeHorizontal(v_y, v_x, v_c);
    
    // Finalize output
    enhance = create<EnhanceGenerator>();

    enhance->denoiseChroma.set(true);
    enhance->enableSharpen.set(true);
    enhance->popRadius.set(25);

    enhance->apply(
        (Func) tonemap->output,
        chromaEps,
        WIDTH*2,
        HEIGHT*2,
        blackPoint,
        whitePoint,
        contrast,
        brightness,
        blues,
        greens,
        saturation,
        sharpen0,
        sharpen1,
        pop);

    // Finish with blue noise dithering + gamma
    Expr h = v_i / 255.0f;

    gammaLut(v_i) = saturating_cast<uint8_t>(select(h < 0.0031308f, h * 12.92f, pow(h, 1.0f / 2.4f) * 1.055f - 0.055f) * 255.0f + 0.5f);
    if(!auto_schedule)
        gammaLut.compute_root().vectorize(v_i, 8);

    // Dither using blue noise
    noiseInput(v_x, v_y, v_c) = BoundaryConditions::repeat_image(blueNoise)(v_x, v_y, v_c) * 2.0f/255.0f - 1.0f;

    Expr S = select(noiseInput(v_x, v_y, v_c) < 0.0f, -1.0f, 1.0f);
    noise(v_x, v_y, v_c) = S*(1.0f - sqrt(max(0.0f, 1.0f - abs(noiseInput(v_x, v_y, v_c)))));

    output(v_x, v_y, v_c) = gammaLut(saturating_cast<uint8_t>(0.5f + enhance->output(v_x, v_y, v_c) * 255.0f / 65535.0f + noise(v_x, v_y, v_c)));

    // Noise/output are interleaved
    blueNoise
        .dim(0).set_stride(4)
        .dim(2).set_stride(1);

    output
        .dim(0).set_stride(3)
        .dim(2).set_stride(1);
    
    range.set_estimate(16384);
    sensorArrangement.set_estimate(0);

    contrast.set_estimate(1.5f);
    shadows.set_estimate(2.0f);
    tonemapVariance.set_estimate(0.25f);
    blackPoint.set_estimate(0.01f);
    exposure.set_estimate(0.0f);
    whitePoint.set_estimate(0.95f);
    blues.set_estimate(1.0f);
    saturation.set_estimate(1.0f);
    greens.set_estimate(1.0f);
    sharpen0.set_estimate(2.0f);
    sharpen1.set_estimate(2.0f);
    chromaEps0.set_estimate(0.01f);
    chromaEps1.set_estimate(0.01f);
    
    cameraToSrgb.set_estimates({{0, 3}, {0, 3}});

    in0.set_estimates({{0, 2048}, {0, 1536}});
    in1.set_estimates({{0, 2048}, {0, 1536}});
    in2.set_estimates({{0, 2048}, {0, 1536}});
    in3.set_estimates({{0, 2048}, {0, 1536}});

    inShadingMap0.set_estimates({{0, 17}, {0, 13}});
    inShadingMap1.set_estimates({{0, 17}, {0, 13}});
    inShadingMap2.set_estimates({{0, 17}, {0, 13}});
    inShadingMap3.set_estimates({{0, 17}, {0, 13}});

    asShotVector.set_estimate(0, 1.0f);
    asShotVector.set_estimate(1, 1.0f);
    asShotVector.set_estimate(2, 1.0f);

    output.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});

    if(!auto_schedule) {
        if(get_target().has_gpu_feature())
            schedule_for_gpu();
        else
            schedule_for_cpu();
    }
}

void PostProcessGenerator::schedule_for_gpu() {
}

void PostProcessGenerator::schedule_for_cpu() { 
    int vector_size_u8 = natural_vector_size<uint8_t>();
    int vector_size_u16 = natural_vector_size<uint16_t>();

    Lmap
        .compute_at(chromaEpsMap, v_y)
        .vectorize(v_x, 8);

    LmapTmp0
        .compute_at(chromaEpsMap, v_y)
        .vectorize(v_x, 8);

    LmapTmp1
        .compute_at(chromaEpsMap, v_y)
        .vectorize(v_x, 8);

    LmapTmp2
        .compute_at(chromaEpsMap, v_y)
        .vectorize(v_x, 8);

    LmapTmp3
        .compute_at(chromaEpsMap, v_y)
        .vectorize(v_x, 8);

    chromaEpsMap
        .compute_root()
        .vectorize(v_y, 12)
        .parallel(v_y);

    // defringeVerticalTransposed
    //     .compute_root()
    //     .tile(v_x, v_y, v_xo, v_yo, v_x, v_y, 8, 8)
    //     .vectorize(v_x)
    //     .parallel(v_yo)
    //     .parallel(v_c);

    // defringe
    //     .compute_root()
    //     .tile(v_x, v_y, v_xo, v_yo, v_x, v_y, 8, 8)
    //     .vectorize(v_x)
    //     .parallel(v_yo)
    //     .parallel(v_c);

    hdrTonemapInput
        .compute_root()
            .bound(v_c, 0, 3)
            .reorder(v_c, v_x, v_y)
            .parallel(v_y)
            .unroll(v_c)
            .vectorize(v_x, vector_size_u16);

    output
        .compute_root()
        .bound(v_c, 0, 3)
        .reorder(v_c, v_x, v_y)
        .split(v_y, v_yo, v_yi, 64)
        .parallel(v_yo)
        .unroll(v_c)
        .vectorize(v_x, vector_size_u8);
}

//

class PreviewGenerator : public Halide::Generator<PreviewGenerator>, public PostProcessBase {
public:
    GeneratorParam<int> rotation{"rotation", 0};
    GeneratorParam<int> tonemapLevels{"tonemap_levels", 8};
    GeneratorParam<int> downscaleFactor{"downscale_factor", 1};
    GeneratorParam<bool> enableSharpen{"enable_sharpen", true};
    GeneratorParam<int> popRadius{"pop_radius", 25};

    Input<Buffer<uint8_t>> input{"input", 1};

    Input<Buffer<float>> inShadingMap0{"inShadingMap0", 2 };
    Input<Buffer<float>> inShadingMap1{"inShadingMap1", 2 };
    Input<Buffer<float>> inShadingMap2{"inShadingMap2", 2 };
    Input<Buffer<float>> inShadingMap3{"inShadingMap3", 2 };
    
    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<int> width{"width"};
    Input<int> height{"height"};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};

    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<int16_t> whiteLevel{"whiteLevel"};

    Input<float> shadows{"shadows"};
    Input<float> whitePoint{"whitePoint"};
    Input<float> tonemapVariance{"tonemapVariance"};
    Input<float> blackPoint{"blackPoint"};
    Input<float> exposure{"exposure"};
    Input<float> contrast{"contrast"};
    Input<float> brightness{"brightness"};
    Input<float> blues{"blues"};
    Input<float> greens{"greens"};
    Input<float> saturation{"saturation"};
    Input<float> sharpen0{"sharpen0"};
    Input<float> sharpen1{"sharpen1"};
    Input<float> pop{"pop"};

    Input<bool> flipped{"flipped"};

    Output<Buffer<uint8_t>> output{"output", 3};
    
    std::unique_ptr<TonemapGenerator> tonemap;
    std::unique_ptr<EnhanceGenerator> enhance;
    
    void generate();
    void schedule_for_gpu();
    void schedule_for_cpu();
    
private:
    Func downscale(Func f, Func& downx);

private:
    Func in[4];
    Func shadingMap[4];
    Func deinterleaved{"deinterleaved"};
    Func demosaicInput{"demosaicInput"};
    Func downscaledInput{"downscaledInput"};
    Func srgbInput{"srgbInput"};
    Func inMuxed{"inMuxed"};
    Func SRGB{"SRGB"};
    Func enhanceInput{"enhanceInput"};
    Func YCbCr{"YCbCr"};
    Func linearRgb{"linearRgb"};
    Func tonemapped{"tonemapped"};
    Func tonemapInput{"tonemapInput"};    
    Func gammaLut{"gammaContrastLut"};    
};

Func PreviewGenerator::downscale(Func f, Func& downx) {
    Func downy;

    downx(v_x, v_y, v_c) = (f(v_x*2 - 1, v_y, v_c) + 2.0f*f(v_x*2, v_y, v_c) + f(v_x*2 + 1, v_y, v_c)) / 4.0f;
    downy(v_x, v_y, v_c) = (downx(v_x, v_y*2 - 1, v_c) + 2.0f*downx(v_x, v_y*2, v_c) + downx(v_x, v_y*2 + 1, v_c)) / 4.0f;

    return downy;
}

void PreviewGenerator::generate() {
    deinterleave(inMuxed, input, stride, pixelFormat);
    
    Func inDownscale = Halide::BoundaryConditions::repeat_edge(inMuxed, { { 0, width * downscaleFactor - 1 }, { 0, height * downscaleFactor - 1 } } );

    int iterations = (int) (std::log((float)downscaleFactor) / std::log(2.0f));

    std::vector<Func> d;

    d.push_back(inDownscale);

    for(int i = 0; i < iterations; i++) {
        Func downscaledTemp{"downscaledTemp" + std::to_string(i)};
        Func downscaled = downscale(d[d.size()-1], downscaledTemp);

        d.push_back(downscaled);
    }

    // Shading map
    linearScale(shadingMap[0], inShadingMap0, inShadingMap0.width(), inShadingMap0.height(), width, height);
    linearScale(shadingMap[1], inShadingMap1, inShadingMap1.width(), inShadingMap1.height(), width, height);
    linearScale(shadingMap[2], inShadingMap2, inShadingMap2.width(), inShadingMap2.height(), width, height);
    linearScale(shadingMap[3], inShadingMap3, inShadingMap3.width(), inShadingMap3.height(), width, height);

    downscaledInput(v_x, v_y, v_c) = saturating_cast<uint16_t>(0.5f + d[d.size()-1](v_x, v_y, v_c));

    d[d.size()-1].compute_root()
        .vectorize(v_x, 8)
        .unroll(v_c)
        .parallel(v_y);

    for(int i = 0; i < iterations - 1; i++) {
        d[i].compute_at(d[d.size()-1], v_y)
            .unroll(v_c)
            .vectorize(v_x, 8);
    }

    rearrange(demosaicInput, downscaledInput, sensorArrangement);

    Expr c0 = (demosaicInput(v_x, v_y, 0) - blackLevel[0]) / (cast<float>(whiteLevel - blackLevel[0])) * shadingMap[0](v_x, v_y);
    Expr c1 = (demosaicInput(v_x, v_y, 1) - blackLevel[1]) / (cast<float>(whiteLevel - blackLevel[1])) * shadingMap[1](v_x, v_y);
    Expr c2 = (demosaicInput(v_x, v_y, 2) - blackLevel[2]) / (cast<float>(whiteLevel - blackLevel[2])) * shadingMap[2](v_x, v_y);
    Expr c3 = (demosaicInput(v_x, v_y, 3) - blackLevel[3]) / (cast<float>(whiteLevel - blackLevel[3])) * shadingMap[3](v_x, v_y);
    
    srgbInput(v_x, v_y, v_c) = select(v_c == 0,  clamp( c0,               0.0f, asShotVector[0] ),
                                      v_c == 1,  clamp( (c1 + c2) / 2,    0.0f, asShotVector[1] ),
                                                 clamp( c3,               0.0f, asShotVector[2] ));

    transform(SRGB, srgbInput, cameraToSrgb);

    tonemapInput(v_x, v_y, v_c) = saturating_cast<uint16_t>(SRGB(v_x, v_y, v_c) * pow(2.0f, exposure) * 65535.0f + 0.5f);

    tonemap = create<TonemapGenerator>();

    tonemap->output_type.set(UInt(16));
    tonemap->tonemap_levels.set(tonemapLevels);
    tonemap->apply(tonemapInput, tonemapInput, width, height, tonemapVariance, shadows);

    enhance = create<EnhanceGenerator>();
    
    enhance->denoiseChroma.set(false);
    enhance->enableSharpen.set(enableSharpen);
    enhance->popRadius.set(popRadius);

    Func eps{"eps"};

    eps(v_x, v_y) = 0;

    enhance->apply(
        tonemap->output,
        eps,
        width,
        height,
        blackPoint,
        whitePoint,
        contrast,
        brightness,
        blues,
        greens,
        saturation,
        sharpen0,
        sharpen1,
        pop);
           
    //
    // Finalize output
    //

    Expr M, N;

    switch(rotation) {
        case 90:
            M = width - v_y;
            N = select(flipped, height - v_x, v_x);
            break;

        case -90:
            M = v_y;
            N = select(flipped, v_x, height - v_x);
            break;

        case 180:
            M = v_x;
            N = height - v_y;
            break;

        default:
        case 0:
            M = select(flipped, width - v_x, v_x);
            N = v_y;
            break;
    }

    Func gammaLut{"gammaLut"};    
    Expr h = v_i / 255.0f;

    gammaLut(v_i) = saturating_cast<uint8_t>(select(h < 0.0031308f, h * 12.92f, pow(h, 1.0f / 2.4f) * 1.055f - 0.055f) * 255.0f);
    if(!auto_schedule)
        gammaLut.compute_root().vectorize(v_i, 8);

    output(v_x, v_y, v_c) = gammaLut(saturating_cast<uint8_t>(
        select( v_c == 0, enhance->output(M, N, 2) * 255.0f/65535.0f + 0.5f,
                v_c == 1, enhance->output(M, N, 1) * 255.0f/65535.0f + 0.5f,
                v_c == 2, enhance->output(M, N, 0) * 255.0f/65535.0f + 0.5f,
                255)));

    // Output interleaved
    output
        .dim(0).set_stride(4)
        .dim(2).set_stride(1);
    
    if(get_target().has_gpu_feature())
        schedule_for_gpu();
    else
        schedule_for_cpu();
}

void PreviewGenerator::schedule_for_gpu() {   
}

void PreviewGenerator::schedule_for_cpu() {
    int vector_size_u8 = natural_vector_size<uint8_t>();
    int vector_size_u16 = natural_vector_size<uint16_t>();    

    tonemapInput
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .unroll(v_c)
        .parallel(v_y)
        .vectorize(v_x, vector_size_u16);

    output
        .compute_root()
        .bound(v_c, 0, 4)
        .reorder(v_c, v_x, v_y)
        .tile(v_x, v_y, v_xo, v_yo, v_xi, v_yi, 32, 16)
        .fuse(v_xo, v_yo, tile_idx)
        .parallel(tile_idx)
        .unroll(v_c)
        .vectorize(v_xi, vector_size_u8);
}

class FastPreviewGenerator : public Halide::Generator<FastPreviewGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};

    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> sx{"sx"};
    Input<int> sy{"sy"};

    Input<int>    whiteLevel{"whiteLevel"};
    Input<int[4]> blackLevel{"blackLevel"};

    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Output<Buffer<uint8_t>> output{"output", 3};

    void generate();
    void schedule_for_cpu();
};

void FastPreviewGenerator::generate() {
    Func bayer{"bayer"};
    Func linear{"linear"};
    Func bayerInput{"bayerInput"};
    Func clamped{"clamped"};
    Func colorCorrected{"colorCorrected"};
    Func colorCorrectInput{"colorCorrectInput"};
    Func toLinear{"toLinear"};
    Func bl{"bl"};

    // Deinterleave
    clamped = BoundaryConditions::repeat_edge(input);

    deinterleave(bayer, clamped, stride, pixelFormat);

    toLinear(v_c) = select(
        v_c == 0, 1.0f / cast<float>(whiteLevel - blackLevel[0]),
        v_c == 1, 1.0f / cast<float>(whiteLevel - blackLevel[1]),
        v_c == 2, 1.0f / cast<float>(whiteLevel - blackLevel[2]),
                  1.0f / cast<float>(whiteLevel - blackLevel[3])
    );

    bl(v_c) = mux(v_c, {
        blackLevel[0],
        blackLevel[1],
        blackLevel[2],
        blackLevel[3]
    });

    linear(v_x, v_y, v_c) = (bayer(v_x * sx, v_y * sy, v_c) - bl(v_c)) * toLinear(v_c);

    bayerInput(v_x, v_y, v_c) =
        select(sensorArrangement == static_cast<int>(SensorArrangement::RGGB),
                select( v_c == 0, linear(v_x, v_y, 0),
                        v_c == 1, linear(v_x, v_y, 1),
                                  linear(v_x, v_y, 3) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GRBG),
                select( v_c == 0, linear(v_x, v_y, 1),
                        v_c == 1, linear(v_x, v_y, 0),
                                  linear(v_x, v_y, 2) ),

            sensorArrangement == static_cast<int>(SensorArrangement::GBRG),
                select( v_c == 0, linear(v_x, v_y, 2),
                        v_c == 1, linear(v_x, v_y, 0),
                                  linear(v_x, v_y, 1) ),

                select( v_c == 0, linear(v_x, v_y, 3),
                        v_c == 1, linear(v_x, v_y, 1),
                                  linear(v_x, v_y, 0) ) );


    colorCorrectInput(v_x, v_y, v_c) =
        select( v_c == 0, clamp( bayerInput(v_x, v_y, 0), 0.0f, asShotVector[0] ),
                v_c == 1, clamp( bayerInput(v_x, v_y, 1), 0.0f, asShotVector[1] ),
                          clamp( bayerInput(v_x, v_y, 2), 0.0f, asShotVector[2] ));

    transform(colorCorrected, colorCorrectInput, cameraToSrgb);

    Func gammaLut{"gammaLut"};    
    Expr h = v_i / 255.0f;

    gammaLut(v_i) = saturating_cast<uint8_t>(select(h < 0.0031308f, h * 12.92f, pow(h, 1.0f / 2.4f) * 1.055f - 0.055f) * 255.0f);
    if(!auto_schedule)
        gammaLut.compute_root().vectorize(v_i, 8);

    output(v_x, v_y, v_c) = gammaLut(saturating_cast<uint8_t>(
        select( v_c == 0, colorCorrected(v_x, v_y, 0) * 255.0f + 0.5f,
                v_c == 1, colorCorrected(v_x, v_y, 1) * 255.0f + 0.5f,
                v_c == 2, colorCorrected(v_x, v_y, 2) * 255.0f + 0.5f,
                255)));

    // Output interleaved
    output
        .dim(0).set_stride(4)
        .dim(2).set_stride(1);
    
    input.set_estimates({ {0, 18000000} });
    width.set_estimate(4000);
    height.set_estimate(3000);
    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);
    whiteLevel.set_estimate(1023);
    sx.set_estimate(2);
    sy.set_estimate(2);
    stride.set_estimate(4000);
    sensorArrangement.set_estimate(0);
    pixelFormat.set_estimate(0);
    cameraToSrgb.set_estimates({{0, 3}, {0, 3}});

    asShotVector.set_estimate(0, 1.0f);
    asShotVector.set_estimate(1, 1.0f);
    asShotVector.set_estimate(2, 1.0f);

    output.set_estimates({{0, 250}, {0, 150}, {0, 3} } );

    if(!get_auto_schedule()) {
        schedule_for_cpu();
    }
 }

void FastPreviewGenerator::schedule_for_cpu() {
    output.compute_root()
        .vectorize(v_x, 16)
        .parallel(v_c)
        .parallel(v_y);
}


//

class DeinterleaveRawGenerator : public Halide::Generator<DeinterleaveRawGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> offsetX{"offsetX"};
    Input<int> offsetY{"offsetY"};

    Input<int>    whiteLevel{"whiteLevel"};
    Input<int[4]> blackLevel{"blackLevel"};
    Input<float>  scale{"scale"};

    Output<Buffer<uint16_t>> output{"output", 3};
    Output<Buffer<uint8_t>> preview{"preview", 2};

    void generate();
    void schedule_for_cpu();
    void apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target);
};

void DeinterleaveRawGenerator::apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Var _0_vi("_0_vi");
    Var _0_vo("_0_vo");
    Var i_vi("i_vi");
    Var i_vo("i_vo");
    Var x_vi("x_vi");
    Var x_vo("x_vo");

    Func bayer_1 = pipeline.get_func(6);
    Func f0 = pipeline.get_func(9);
    Func mirror_image = pipeline.get_func(2);
    Func output = pipeline.get_func(8);
    Func preview = pipeline.get_func(10);

    {
        Var x = bayer_1.args()[0];
        Var y = bayer_1.args()[1];
        bayer_1
            .compute_root()
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi)
            .parallel(y);

        Var _0 = mirror_image.args()[0];
        mirror_image
            .compute_at(bayer_1, y)
            .split(_0, _0_vo, _0_vi, 32)
            .vectorize(_0_vi);            
    }
    {
        Var i = f0.args()[0];
        f0
            .compute_root()
            .split(i, i_vo, i_vi, 32)
            .vectorize(i_vi)
            .parallel(i_vo);
    }
    {
        Var x = output.args()[0];
        Var y = output.args()[1];
        Var c = output.args()[2];
        output
            .compute_root()
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
    }
    {
        Var x = preview.args()[0];
        Var y = preview.args()[1];
        preview
            .compute_root()
            .split(x, x_vo, x_vi, 32)
            .vectorize(x_vi)
            .parallel(y);
    }
}

void DeinterleaveRawGenerator::generate() {
    Func bayer{"bayer"};

    deinterleave(bayer, input, stride, pixelFormat);

    Func clamped = BoundaryConditions::mirror_image(bayer, { { 0, width - 1}, { 0, height - 1 } });
    
    // Gamma correct preview
    Func gammaLut;
    
    gammaLut(v_i) = cast<uint8_t>(clamp(pow(v_i / 255.0f, 1.0f / 2.2f) * 255, 0, 255));    

    if(!get_auto_schedule())
        gammaLut.compute_root();

    Expr x = v_x - offsetX;
    Expr y = v_y - offsetY;

    output(v_x, v_y, v_c) = clamped(x, y, v_c);

    Expr P = 0.25f * (clamped(x, y, 0) +
                      clamped(x, y, 1) +
                      clamped(x, y, 2) +
                      clamped(x, y, 3));

    Expr S = (P - blackLevel[0]) / (whiteLevel - blackLevel[0]);

    preview(v_x, v_y) =  gammaLut(cast<uint8_t>(clamp(S * scale * 255.0f + 0.5f, 0, 255)));

    input.set_estimates({ {0, 12000000} });
    width.set_estimate(4000);
    height.set_estimate(3000);
    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);
    whiteLevel.set_estimate(1023);
    offsetX.set_estimate(0);
    offsetY.set_estimate(0);
    scale.set_estimate(1.0f);
    stride.set_estimate(4000);
    sensorArrangement.set_estimate(0);
    pixelFormat.set_estimate(0);

    output.set_estimates({{0, 2000}, {0, 1500}, {0, 4} });
    preview.set_estimates({{0, 2000}, {0, 1500} });

    if(!get_auto_schedule()) {
        schedule_for_cpu();
        // apply_auto_schedule(get_pipeline(), get_target());
    }
 }

void DeinterleaveRawGenerator::schedule_for_cpu() {    
    output
        .compute_root()
        .reorder(v_c, v_x, v_y)
        .split(v_y, v_yo, v_yi, 16)
        .vectorize(v_x, 16)
        .parallel(v_yo)
        .unroll(v_c, 4);

    preview
        .compute_root()
        .split(v_y, v_yo, v_yi, 16)
        .vectorize(v_x, 16)
        .parallel(v_yo);
}


//

class MeasureImageGenerator : public Halide::Generator<MeasureImageGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> downscaleFactor{"downscaleFactor"};

    Input<int[4]> blackLevel{"blackLevel"};
    Input<int> whiteLevel{"whiteLevel"};

    Input<float[3]> asShotVector{"asShotVector"};
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<Buffer<float>[4]> inShadingMap{"shadingMap", 2};

    Input<int> sensorArrangement{"sensorArrangement"};

    Output<Buffer<uint32_t>> histogram{"histogram", 1};

    void generate();
};

void MeasureImageGenerator::generate() {
    Func shadingMap[4];
    Func inputRepeated{"inputRepeated"};
    Func bayer{"bayer"};
    Func downscaled{"downscaled"};
    Func result16u{"result16u"};
    Func colorCorrected{"colorCorrected"};
    Func downscaledInput{"downscaledInput"};
    Func demosaicInput{"demosaicInput"};

    // Deinterleave
    inputRepeated = BoundaryConditions::repeat_edge(input);

    // Deinterleave
    deinterleave(bayer, inputRepeated, stride, pixelFormat);

    Expr w = width / downscaleFactor;
    Expr h = height / downscaleFactor;
    
    downscaled(v_x, v_y, v_c) = bayer(v_x*downscaleFactor, v_y*downscaleFactor, v_c);

    // Shading map
    linearScale(shadingMap[0], inShadingMap[0], inShadingMap[0].width(), inShadingMap[0].height(), w, h);
    linearScale(shadingMap[1], inShadingMap[1], inShadingMap[1].width(), inShadingMap[1].height(), w, h);
    linearScale(shadingMap[2], inShadingMap[2], inShadingMap[2].width(), inShadingMap[2].height(), w, h);
    linearScale(shadingMap[3], inShadingMap[3], inShadingMap[3].width(), inShadingMap[3].height(), w, h);

    rearrange(demosaicInput, downscaled, sensorArrangement);

    Expr c0 = (demosaicInput(v_x, v_y, 0) - blackLevel[0]) / (cast<float>(whiteLevel - blackLevel[0])) * shadingMap[0](v_x, v_y);
    Expr c1 = (demosaicInput(v_x, v_y, 1) - blackLevel[1]) / (cast<float>(whiteLevel - blackLevel[1])) * shadingMap[1](v_x, v_y);
    Expr c2 = (demosaicInput(v_x, v_y, 2) - blackLevel[2]) / (cast<float>(whiteLevel - blackLevel[2])) * shadingMap[2](v_x, v_y);
    Expr c3 = (demosaicInput(v_x, v_y, 3) - blackLevel[3]) / (cast<float>(whiteLevel - blackLevel[3])) * shadingMap[3](v_x, v_y);
    
    downscaledInput(v_x, v_y, v_c) = select(v_c == 0,  clamp( c0,               0.0f, asShotVector[0] ),
                                            v_c == 1,  clamp( (c1 + c2) / 2,    0.0f, asShotVector[1] ),
                                                       clamp( c3,               0.0f, asShotVector[2] ));
    // Transform to SRGB space
    transform(colorCorrected, downscaledInput, cameraToSrgb);

    Expr L = 0.2989f*colorCorrected(v_x, v_y, 0) + 0.5870f*colorCorrected(v_x, v_y, 1) + 0.1140f*colorCorrected(v_x, v_y, 2);

    result16u(v_x, v_y) = saturating_cast<uint16_t>(L * 65535.0f + 0.5f);

    RDom r(0, w, 0, h);

    histogram(v_i) = cast<uint32_t>(0);
    histogram(result16u(r.x, r.y)) += cast<uint32_t>(1);

    // Schedule
    result16u
        .compute_root()
        .reorder(v_x, v_y)
        .parallel(v_y)
        .vectorize(v_x, 8);

    histogram
        .compute_root()
        .vectorize(v_i, 32);
}

//////////////

class GenerateEdgesGenerator : public Halide::Generator<GenerateEdgesGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Output<Buffer<uint16_t>> output{"output", 2};

    void generate();
};

void GenerateEdgesGenerator::generate() {
    Func bayer{"bayer"};
    Func sum{"sum"};

    deinterleave(bayer, input, stride, pixelFormat);
    
    sum(v_x, v_y) =
        cast<int32_t>(bayer(v_x, v_y, 0)) +
        cast<int32_t>(bayer(v_x, v_y, 1)) +
        cast<int32_t>(bayer(v_x, v_y, 2)) +
        cast<int32_t>(bayer(v_x, v_y, 3));

    Func bounded = BoundaryConditions::repeat_edge(sum, { { 0, width - 1}, {0, height - 1} });

    Func sobel_x_avg{"sobel_x_avg"}, sobel_y_avg{"sobel_y_avg"};
    Func sobel_x{"sobel_x"}, sobel_y{"sobel_y"};

    sobel_x_avg(v_x, v_y) = bounded(v_x - 1, v_y) + 2 * bounded(v_x, v_y) + bounded(v_x + 1, v_y);
    sobel_x(v_x, v_y) = absd(sobel_x_avg(v_x, v_y - 1), sobel_x_avg(v_x, v_y + 1));

    sobel_y_avg(v_x, v_y) = bounded(v_x, v_y - 1) + 2 * bounded(v_x, v_y) + bounded(v_x, v_y + 1);
    sobel_y(v_x, v_y) = absd(sobel_y_avg(v_x - 1, v_y), sobel_y_avg(v_x + 1, v_y));
    
    output(v_x, v_y) = cast<uint16_t>(clamp(sobel_x(v_x, v_y) + sobel_y(v_x, v_y), 0, 65535));

    sum
        .compute_at(output, v_yi)
        .store_at(output, v_yo)
        .vectorize(v_x, 8);

    output.compute_root()
        .vectorize(v_x, 8)
        .split(v_y, v_yo, v_yi, 32)
        .parallel(v_yo);
}

//////////////

class HdrMaskGenerator : public Halide::Generator<HdrMaskGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> input0{"input0", 3};
    Input<Buffer<uint16_t>> input1{"input1", 3};

    Input<Buffer<float>> warpMatrix{"warpMatrix", 2};

    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<uint16_t> whiteLevel{"whiteLevel"};

    Input<float> scale0{"scale0"};
    Input<float> scale1{"scale1"};

    Input<float> c{"c"};

    Output<Buffer<uint8_t>> outputGhost{"outputGhost", 2};

    void generate();

private:
    void schedule_for_cpu(::Halide::Pipeline pipeline, ::Halide::Target target);

    Var v_x{"x"};
    Var v_y{"y"};
    Var v_yo{"yo"};
    Var v_yi{"yi"};
};

void HdrMaskGenerator::generate() {
    Func inputf0{"inputf0"}, inputf1{"inputf1"};
    Func inMax0{"inMax0"}, inMax1{"inMax1"};
    Func warped{"warped"};
    Func mask0{"mask0"}, mask1{"mask1"};
    Func map0{"map0"}, map1{"map1"};
    Func ghostMap{"ghostMap"};
    Func bl{"bl"};

    bl(v_c) =
        mux(v_c,
            {   blackLevel[0],
                blackLevel[1],
                blackLevel[2],
                blackLevel[3] });

    warp(warped, BoundaryConditions::repeat_edge(input1), warpMatrix);

    inputf0(v_x, v_y, v_c) = clamp(scale0*(cast<float>(BoundaryConditions::repeat_edge(input0)(v_x, v_y, v_c))-bl(v_c)) / whiteLevel, 0.0f, 1.0f);
    inputf1(v_x, v_y, v_c) = clamp(scale1*(cast<float>(warped(v_x, v_y, v_c))-bl(v_c)) / whiteLevel, 0.0f, 1.0f);

    inMax0(v_x, v_y) = max(
        inputf0(v_x, v_y, 0),
        inputf0(v_x, v_y, 1),
        inputf0(v_x, v_y, 2),
        inputf0(v_x, v_y, 3));

    inMax1(v_x, v_y) = max(
        inputf1(v_x, v_y, 0),
        inputf1(v_x, v_y, 1),
        inputf1(v_x, v_y, 2),
        inputf1(v_x, v_y, 3));

    mask0(v_x, v_y) = exp(-c * (inMax0(v_x, v_y) - 1.0f) * (inMax0(v_x, v_y) - 1.0f));
    mask1(v_x, v_y) = exp(-c * (inMax1(v_x, v_y) - 1.0f) * (inMax1(v_x, v_y) - 1.0f));

    map0(v_x, v_y) = cast<bool>(select(mask0(v_x, v_y) > 0.05f, 1, 0));
    map1(v_x, v_y) = cast<bool>(select(mask1(v_x, v_y) > 0.05f, 1, 0));

    ghostMap(v_x, v_y) = map0(v_x, v_y) ^ map1(v_x, v_y);

    RDom r(-3, 3, -3, 3);

    outputGhost(v_x, v_y) = cast<uint8_t>(1);
    outputGhost(v_x, v_y) = outputGhost(v_x, v_y) & ghostMap(v_x + r.x, v_y + r.y);

    c.set_estimate(4.0f);

    input0.set_estimates({{0, 2048}, {0, 1536}, {0, 4}});
    input1.set_estimates({{0, 2048}, {0, 1536}, {0, 4}});
    
    warpMatrix.set_estimates({{0, 3}, {0, 3}});

    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);

    whiteLevel.set_estimate(1023);

    outputGhost.set_estimates({{0, 2048}, {0, 1536}});

    if(!auto_schedule) {
        schedule_for_cpu(get_pipeline(), get_target());
    }
}

void HdrMaskGenerator::schedule_for_cpu(::Halide::Pipeline pipeline, ::Halide::Target target) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Var x_vi("x_vi");
    Var x_vo("x_vo");

    Func outputGhost = pipeline.get_func(19);

    {
        Var x = outputGhost.args()[0];
        Var y = outputGhost.args()[1];
        RVar r51$x(outputGhost.update(0).get_schedule().rvars()[0].var);
        RVar r51$y(outputGhost.update(0).get_schedule().rvars()[1].var);
        outputGhost
            .compute_root()
            .split(x, x_vo, x_vi, 32)
            .vectorize(x_vi)
            .parallel(y);
        outputGhost.update(0)
            .reorder(r51$x, x, r51$y, y)
            .reorder(r51$x, r51$y, x, y)
            .split(x, x_vo, x_vi, 32, TailStrategy::GuardWithIf)
            .vectorize(x_vi)
            .parallel(y);
    }
}

//////////////

class LinearImageGenerator : public Halide::Generator<LinearImageGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> input{"input", 3};
    Input<Buffer<float>> warpMatrix{"warpMatrix", 2};

    Input<Buffer<float>> inShadingMap0{"inShadingMap0", 2 };
    Input<Buffer<float>> inShadingMap1{"inShadingMap1", 2 };
    Input<Buffer<float>> inShadingMap2{"inShadingMap2", 2 };
    Input<Buffer<float>> inShadingMap3{"inShadingMap3", 2 };

    Input<float[3]> asShotVector{"asShotVector"};    
    Input<Buffer<float>> cameraToSrgb{"cameraToSrgb", 2};

    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<int16_t> whiteLevel{"whiteLevel"};
    Input<float> range{"range"};

    Output<Buffer<uint16_t>> output{"output", 3};

    void generate();

private:
    std::unique_ptr<Demosaic> demosaic;
};

void LinearImageGenerator::generate() {
    Func inDemosaic[4];

    Func b{"b"};
    Func scaled{"scaled"};
    Func warped{"warped"};

    b(v_c) =
        mux(v_c,
            {   blackLevel[0],
                blackLevel[1],
                blackLevel[2],
                blackLevel[3] });

    warp(warped, BoundaryConditions::repeat_edge(input), warpMatrix);

    scaled(v_x, v_y, v_c) = cast<uint16_t>(clamp((cast<float>(warped(v_x, v_y, v_c)) - b(v_c)) / cast<float>(whiteLevel - b(v_c)) * range + 0.5f, 0, range));

    inDemosaic[0](v_x, v_y) = scaled(v_x, v_y, 0);
    inDemosaic[1](v_x, v_y) = scaled(v_x, v_y, 1);
    inDemosaic[2](v_x, v_y) = scaled(v_x, v_y, 2);
    inDemosaic[3](v_x, v_y) = scaled(v_x, v_y, 3);

    std::vector<Expr> asShot{ asShotVector[0], asShotVector[1], asShotVector[2] };

    demosaic = create<Demosaic>();

    demosaic->apply(
        inDemosaic[0], inDemosaic[1], inDemosaic[2], inDemosaic[3],
        inShadingMap0, inShadingMap1, inShadingMap2, inShadingMap3,
        input.width(), input.height(),
        inShadingMap0.width(), inShadingMap0.height(),
        cast<float>(range),
        sensorArrangement,
        asShot,
        cameraToSrgb);

    output(v_x, v_y, v_c) = cast<uint16_t>(clamp(cast<float>(demosaic->output(v_x, v_y, v_c)) + 0.5f, 0, 65535));

    scaled
        .compute_root()
        .vectorize(v_x, 8)
        .parallel(v_y)
        .unroll(v_c);

    input.set_estimates({{0, 2048}, {0, 1536}, {0, 4}});

    inShadingMap0.set_estimates({{0, 17}, {0, 13}});
    inShadingMap1.set_estimates({{0, 17}, {0, 13}});
    inShadingMap2.set_estimates({{0, 17}, {0, 13}});
    inShadingMap3.set_estimates({{0, 17}, {0, 13}});

    asShotVector.set_estimate(0, 1.0f);
    asShotVector.set_estimate(1, 1.0f);
    asShotVector.set_estimate(2, 1.0f);

    cameraToSrgb.set_estimates({{0, 3}, {0, 3}});
    sensorArrangement.set_estimate(0);

    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);
    whiteLevel.set_estimate(1023);
    width.set_estimate(2048);
    height.set_estimate(1536);

    output.set_estimates({{0, 4096}, {0, 3072}, {0, 3}});
}

//////////////

class BuildBayerGenerator : public Halide::Generator<BuildBayerGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1 };

    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};

    Output<Buffer<uint16_t>> output{"output", 2 };

    void generate() {
        Func inputDeinterleaved{"inputDeinterleaved"};

        deinterleave(inputDeinterleaved, BoundaryConditions::repeat_edge(input), stride, pixelFormat);

        output(v_x, v_y) =
            select(v_y % 2 == 0,
                   select(v_x % 2 == 0, inputDeinterleaved(v_x/2, v_y/2, 0), inputDeinterleaved(v_x/2, v_y/2, 1)),
                   select(v_x % 2 == 0, inputDeinterleaved(v_x/2, v_y/2, 2), inputDeinterleaved(v_x/2, v_y/2, 3)));

        input.set_estimates({ {0, 24000000} });
        stride.set_estimate(5008);
        pixelFormat.set_estimate(0);

        output.set_estimates({{0, 4000}, {0, 3000} });

        output.compute_root()
            .parallel(v_y)
            .vectorize(v_x, 8);
    }
};

class BuildBayerGenerator2 : public Halide::Generator<BuildBayerGenerator2>, public PostProcessBase {
public:
    Input<Buffer<float>> input{"input", 3 };

    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<int16_t> whiteLevel{"whiteLevel"};
    Input<float> scale{"scale"};

    Input<uint16_t> expandedRange{"expandedRange"};

    Output<Buffer<uint16_t>> output{"output", 2 };

    void generate();
};

void BuildBayerGenerator2::generate() {
    Func linear{"linear"};
    Func scaled{"scaled"};

    scaled(v_x, v_y, v_c) = input(v_x, v_y, v_c) * scale;

    linear(v_x, v_y, v_c) = cast<uint16_t>(0.5f +
            select( v_c == 0, (expandedRange / (whiteLevel - blackLevel[0])) * (scaled(v_x, v_y, 0) - blackLevel[0]),
                    v_c == 1, (expandedRange / (whiteLevel - blackLevel[1])) * (scaled(v_x, v_y, 1) - blackLevel[1]),
                    v_c == 2, (expandedRange / (whiteLevel - blackLevel[2])) * (scaled(v_x, v_y, 2) - blackLevel[2]),
                              (expandedRange / (whiteLevel - blackLevel[3])) * (scaled(v_x, v_y, 3) - blackLevel[3]) ) );        

    output(v_x, v_y) =
        select(v_y % 2 == 0,
               select(v_x % 2 == 0, linear(v_x/2, v_y/2, 0), linear(v_x/2, v_y/2, 1)),
               select(v_x % 2 == 0, linear(v_x/2, v_y/2, 2), linear(v_x/2, v_y/2, 3)));

    input.set_estimates({{0, 2000}, {0, 1500}, {0, 4} });
    output.set_estimates({{0, 4000}, {0, 3000} });

    output.compute_root()
        .parallel(v_y)
        .vectorize(v_x, 8);
}

class BuildBayerGenerator3 : public Halide::Generator<BuildBayerGenerator3>, public PostProcessBase {
public:
    Input<Buffer<uint16_t>> in0{"in0", 2 };
    Input<Buffer<uint16_t>> in1{"in1", 2 };
    Input<Buffer<uint16_t>> in2{"in2", 2 };
    Input<Buffer<uint16_t>> in3{"in3", 2 };

    Input<int16_t[4]> blackLevel{"blackLevel"};
    Input<int16_t> whiteLevel{"whiteLevel"};

    Input<uint16_t> expandedRange{"expandedRange"};

    Output<Buffer<uint16_t>> output{"output", 2 };

    void generate();
};

void BuildBayerGenerator3::generate() {
    Func linear{"linear"};

    linear(v_x, v_y, v_c) = cast<uint16_t>(0.5f +
            select( v_c == 0, (expandedRange / (whiteLevel - blackLevel[0])) * (input(v_x, v_y, 0) - blackLevel[0]),
                    v_c == 1, (expandedRange / (whiteLevel - blackLevel[1])) * (input(v_x, v_y, 1) - blackLevel[1]),
                    v_c == 2, (expandedRange / (whiteLevel - blackLevel[2])) * (input(v_x, v_y, 2) - blackLevel[2]),
                              (expandedRange / (whiteLevel - blackLevel[3])) * (input(v_x, v_y, 3) - blackLevel[3]) ) );        

    output(v_x, v_y) =
        select(v_y % 2 == 0,
               select(v_x % 2 == 0, linear(v_x/2, v_y/2, 0), linear(v_x/2, v_y/2, 1)),
               select(v_x % 2 == 0, linear(v_x/2, v_y/2, 2), linear(v_x/2, v_y/2, 3)));

    input.set_estimates({{0, 2000}, {0, 1500}, {0, 4} });
    output.set_estimates({{0, 4000}, {0, 3000} });

    output.compute_root()
        .parallel(v_y)
        .vectorize(v_x, 8);
}

///////////////

class MeasureNoiseGenerator : public Generator<MeasureNoiseGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};
    Output<Buffer<float>> output{"output", 3};
    Output<Buffer<float>> snr{"snr", 3};

    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    Input<int> blockSize{"blockSize"};

    void generate();
    void apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target);

    Var v_x{"x"};
    Var v_y{"y"};
    Var v_c{"c"};    
};

void MeasureNoiseGenerator::generate() {
    Func blocks{"blocks"};
    Func mean{"mean"};
    Func noise{"noise"};
    Func deinterleaved{"deinterleaved"};
    Func clamped{"clamped"};

    // Deinterleave
    clamped(v_x) = input(clamp(v_x, 0, input.width() - 1));

    deinterleave(deinterleaved, clamped, stride, pixelFormat);

    RDom r(0, blockSize, 0, blockSize);

    blocks(v_x, v_y, v_c) = cast<float>(deinterleaved(v_x, v_y, v_c));
    mean(v_x, v_y, v_c) = sum(blocks(v_x*blockSize+r.x, v_y*blockSize+r.y, v_c)) / (blockSize*blockSize);
    noise(v_x, v_y, v_c) = (deinterleaved(v_x, v_y, v_c) - mean(v_x/blockSize, v_y/blockSize, v_c))*(deinterleaved(v_x, v_y, v_c) - mean(v_x/blockSize, v_y/blockSize, v_c));

    output(v_x, v_y, v_c) = sqrt(sum(noise(v_x*blockSize+r.x, v_y*blockSize+r.y, v_c)) / (blockSize*blockSize));
    snr(v_x, v_y, v_c) = mean(v_x, v_y, v_c);

    input.set_estimates({ {0, 18000000} });
    stride.set_estimate(4000);
    pixelFormat.set_estimate(0);
    blockSize.set_estimate(16);
    output.set_estimates({{0, 128}, {0, 96}, {0, 4}});
    snr.set_estimates({{0, 128}, {0, 96}, {0, 4}});

    if (!auto_schedule) {
        apply_auto_schedule(get_pipeline(), get_target());
    }
}

void MeasureNoiseGenerator::apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target)
{
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Var x_i("x_i");
    Var x_i_vi("x_i_vi");
    Var x_i_vo("x_i_vo");
    Var x_o("x_o");
    Var x_vi("x_vi");
    Var x_vo("x_vo");
    Var y_i("y_i");
    Var y_o("y_o");

    Func bayer = pipeline.get_func(5);
    Func deinterleaved = pipeline.get_func(6);
    Func mean = pipeline.get_func(9);
    Func output = pipeline.get_func(12);
    Func snr = pipeline.get_func(13);
    Func sum = pipeline.get_func(8);
    Func sum_1 = pipeline.get_func(11);

    {
        Var x = bayer.args()[0];
        bayer
            .compute_at(deinterleaved, x_o)
            .split(x, x_vo, x_vi, 16)
            .vectorize(x_vi);
    }
    {
        Var x = deinterleaved.args()[0];
        Var y = deinterleaved.args()[1];
        Var c = deinterleaved.args()[2];
        deinterleaved
            .compute_root()
            .split(x, x_o, x_i, 64)
            .split(y, y_o, y_i, 4)
            .reorder(x_i, y_i, c, x_o, y_o)
            .split(x_i, x_i_vo, x_i_vi, 16)
            .vectorize(x_i_vi)
            .parallel(y_o);
    }
    {
        Var x = mean.args()[0];
        Var y = mean.args()[1];
        Var c = mean.args()[2];
        mean
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
    }
    {
        Var x = output.args()[0];
        Var y = output.args()[1];
        Var c = output.args()[2];
        output
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
    }
    {
        Var x = snr.args()[0];
        Var y = snr.args()[1];
        Var c = snr.args()[2];
        snr
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
    }
    {
        Var x = sum.args()[0];
        Var y = sum.args()[1];
        Var c = sum.args()[2];
        RVar r17$x(sum.update(0).get_schedule().rvars()[0].var);
        RVar r17$y(sum.update(0).get_schedule().rvars()[1].var);
        sum
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
        sum.update(0)
            .reorder(r17$x, x, r17$y, y, c)
            .split(x, x_vo, x_vi, 8, TailStrategy::GuardWithIf)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
    }
    {
        Var x = sum_1.args()[0];
        Var y = sum_1.args()[1];
        Var c = sum_1.args()[2];
        RVar r17$x(sum_1.update(0).get_schedule().rvars()[0].var);
        RVar r17$y(sum_1.update(0).get_schedule().rvars()[1].var);
        sum_1
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
        sum_1.update(0)
            .reorder(r17$x, x, r17$y, y, c)
            .split(x, x_vo, x_vi, 8, TailStrategy::GuardWithIf)
            .vectorize(x_vi)
            .parallel(c)
            .parallel(y);
    }
}

///////////////

class StatsGenerator : public Halide::Generator<StatsGenerator>, public PostProcessBase {
public:
    Input<Buffer<uint8_t>> input{"input", 1};

    Input<int> stride{"stride"};
    Input<int> pixelFormat{"pixelFormat"};
    Input<int> sensorArrangement{"sensorArrangement"};
    
    Input<int> width{"width"};
    Input<int> height{"height"};

    Input<int> sx{"sx"};
    Input<int> sy{"sy"};

    Input<int>    whiteLevel{"whiteLevel"};
    Input<int[4]> blackLevel{"blackLevel"};

    Input<float> weight{"weight"};

    Output<Buffer<uint8_t>> output{"output", 3};

    void generate();
    void apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target);
};

void StatsGenerator::generate() {
    Func bayer{"bayer"};
    Func linear{"linear"};
    Func peak{"peak"};
    Func clamped{"clamped"};
    Func toLinear{"toLinear"};
    Func bl{"bl"};

    // Deinterleave
    clamped = BoundaryConditions::repeat_edge(input);

    deinterleave(bayer, clamped, stride, pixelFormat);

    toLinear(v_c) = select(
        v_c == 0, 1.0f / cast<float>(whiteLevel - blackLevel[0]),
        v_c == 1, 1.0f / cast<float>(whiteLevel - blackLevel[1]),
        v_c == 2, 1.0f / cast<float>(whiteLevel - blackLevel[2]),
                  1.0f / cast<float>(whiteLevel - blackLevel[3])
    );

    bl(v_c) = mux(v_c, {
        blackLevel[0],
        blackLevel[1],
        blackLevel[2],
        blackLevel[3]
    });

    linear(v_x, v_y, v_c) = (bayer(v_x * sx, v_y * sy, v_c) - bl(v_c)) * toLinear(v_c);
    peak(v_x, v_y) = max(linear(v_x, v_y, 0), linear(v_x, v_y, 1), linear(v_x, v_y, 2), linear(v_x, v_y, 3)) - 1.0f;
    
    // Return in portrait
    Expr X = v_y;
    Expr Y = height - v_x;

    output(v_x, v_y, v_c) =
        select( v_c == 0, cast<uint8_t>(0),
                v_c == 1, cast<uint8_t>(0),
                v_c == 2, cast<uint8_t>(0),
                saturating_cast<uint8_t>(255.0f * exp(-weight * (peak(X, Y)*peak(X, Y)))));

    output
        .dim(0).set_stride(4)
        .dim(2).set_stride(1);

    input.set_estimates({ {0, 18000000} });
    width.set_estimate(4000);
    height.set_estimate(3000);
    blackLevel.set_estimate(0, 64);
    blackLevel.set_estimate(1, 64);
    blackLevel.set_estimate(2, 64);
    blackLevel.set_estimate(3, 64);
    whiteLevel.set_estimate(1023);
    sx.set_estimate(2);
    sy.set_estimate(2);
    stride.set_estimate(4000);
    sensorArrangement.set_estimate(0);
    pixelFormat.set_estimate(0);

    output.set_estimates({{0, 250}, {0, 150}, {0, 3} } );

    if(!get_auto_schedule()) {
        apply_auto_schedule(get_pipeline(), get_target());
    }
}

void StatsGenerator::apply_auto_schedule(::Halide::Pipeline pipeline, ::Halide::Target target) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
    Var x_vi("x_vi");
    Var x_vo("x_vo");
    Var y_vi("y_vi");
    Var y_vo("y_vo");

    Func output = pipeline.get_func(12);
    Func peak = pipeline.get_func(11);

    {
        Var x = output.args()[0];
        Var y = output.args()[1];
        Var c = output.args()[2];
        output
            .compute_root()
            .reorder(y, x, c)
            .split(y, y_vo, y_vi, 32)
            .vectorize(y_vi)
            .parallel(c)
            .parallel(x);
    }
    {
        Var x = peak.args()[0];
        Var y = peak.args()[1];
        peak
            .compute_root()
            .split(x, x_vo, x_vi, 8)
            .vectorize(x_vi)
            .parallel(y);
    }
}

HALIDE_REGISTER_GENERATOR(StatsGenerator, stats_generator)
HALIDE_REGISTER_GENERATOR(GenerateEdgesGenerator, generate_edges_generator)
HALIDE_REGISTER_GENERATOR(MeasureImageGenerator, measure_image_generator)
HALIDE_REGISTER_GENERATOR(MeasureNoiseGenerator, measure_noise_generator)
HALIDE_REGISTER_GENERATOR(DeinterleaveRawGenerator, deinterleave_raw_generator)
HALIDE_REGISTER_GENERATOR(PostProcessGenerator, postprocess_generator)
HALIDE_REGISTER_GENERATOR(FastPreviewGenerator, fast_preview_generator)
HALIDE_REGISTER_GENERATOR(GuidedFilter, guided_filter_generator)
HALIDE_REGISTER_GENERATOR(Demosaic, demosaic_generator)
HALIDE_REGISTER_GENERATOR(TonemapGenerator, tonemap_generator)
HALIDE_REGISTER_GENERATOR(EnhanceGenerator, enhance_generator)
HALIDE_REGISTER_GENERATOR(PreviewGenerator, preview_generator)
HALIDE_REGISTER_GENERATOR(HdrMaskGenerator, hdr_mask_generator)
HALIDE_REGISTER_GENERATOR(LinearImageGenerator, linear_image_generator)
HALIDE_REGISTER_GENERATOR(BuildBayerGenerator, build_bayer_generator)
HALIDE_REGISTER_GENERATOR(BuildBayerGenerator2, build_bayer_generator2)
HALIDE_REGISTER_GENERATOR(BuildBayerGenerator3, build_bayer_generator3)
