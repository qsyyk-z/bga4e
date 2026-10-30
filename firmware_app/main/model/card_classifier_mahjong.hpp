#pragma once

#include "dl_layer_model.hpp"
#include "dl_layer_conv2d.hpp"
#include "dl_layer_depthwise_conv2d.hpp"
#include "dl_layer_global_avg_pool2d.hpp"
#include "dl_layer_fullyconnected.hpp"
#include "card_cls_mahjong.hpp"

#include <stdint.h>

using namespace dl;
using namespace layer;
using namespace card_cls;

/**
 * 麻将 42 类分类器（ESP-DL int8，ESP32-S3）。
 *
 * 对应训练代码 scripts/cardnet.py 的 CardNet：
 *   Conv3x3(3→32,s2) → [DW3x3 + PW1x1] ×9 → Conv1x1(256→512)
 *   → GlobalAvgPool → FC(512→42)
 *
 * 输入：96×96×3，HWC，int8，指数 -7（见量化输出）。
 * 输出：42 个 logits，指数 -4。
 */
class CardClassifierMahjong : public Model<int8_t>
{
private:
    Conv2D<int8_t, int16_t> stem;
    DepthwiseConv2D<int8_t, int16_t> dw0;
    Conv2D<int8_t, int16_t> pw0;
    DepthwiseConv2D<int8_t, int16_t> dw1;
    Conv2D<int8_t, int16_t> pw1;
    DepthwiseConv2D<int8_t, int16_t> dw2;
    Conv2D<int8_t, int16_t> pw2;
    DepthwiseConv2D<int8_t, int16_t> dw3;
    Conv2D<int8_t, int16_t> pw3;
    DepthwiseConv2D<int8_t, int16_t> dw4;
    Conv2D<int8_t, int16_t> pw4;
    DepthwiseConv2D<int8_t, int16_t> dw5;
    Conv2D<int8_t, int16_t> pw5;
    DepthwiseConv2D<int8_t, int16_t> dw6;
    Conv2D<int8_t, int16_t> pw6;
    DepthwiseConv2D<int8_t, int16_t> dw7;
    Conv2D<int8_t, int16_t> pw7;
    DepthwiseConv2D<int8_t, int16_t> dw8;
    Conv2D<int8_t, int16_t> pw8;
    Conv2D<int8_t, int16_t> head;
    GlobalAveragePool2D<int8_t> gap;
    FullyConnected<int8_t, int16_t> fc;

public:
    int8_t *get_output() { return this->fc.get_output().get_element_ptr(); }
    int get_output_exponent() { return this->fc.get_output().exponent; }

    CardClassifierMahjong()
        : stem(-5, get__stem_stem_0_conv_filter(), get__stem_stem_0_conv_bias(),
               get__stem_stem_0_conv_activation(), PADDING_SAME_END, {}, 2, 2, "stem"),
          dw0(-4, get__features_features_0_dw_dw_0_conv_filter(), get__features_features_0_dw_dw_0_conv_bias(),
              get__features_features_0_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 2, 2, "dw0"),
          pw0(-4, get__features_features_0_pw_pw_0_conv_filter(), get__features_features_0_pw_pw_0_conv_bias(),
              get__features_features_0_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw0"),
          dw1(-4, get__features_features_1_dw_dw_0_conv_filter(), get__features_features_1_dw_dw_0_conv_bias(),
              get__features_features_1_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 1, 1, "dw1"),
          pw1(-4, get__features_features_1_pw_pw_0_conv_filter(), get__features_features_1_pw_pw_0_conv_bias(),
              get__features_features_1_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw1"),
          dw2(-4, get__features_features_2_dw_dw_0_conv_filter(), get__features_features_2_dw_dw_0_conv_bias(),
              get__features_features_2_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 2, 2, "dw2"),
          pw2(-4, get__features_features_2_pw_pw_0_conv_filter(), get__features_features_2_pw_pw_0_conv_bias(),
              get__features_features_2_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw2"),
          dw3(-4, get__features_features_3_dw_dw_0_conv_filter(), get__features_features_3_dw_dw_0_conv_bias(),
              get__features_features_3_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 1, 1, "dw3"),
          pw3(-4, get__features_features_3_pw_pw_0_conv_filter(), get__features_features_3_pw_pw_0_conv_bias(),
              get__features_features_3_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw3"),
          dw4(-3, get__features_features_4_dw_dw_0_conv_filter(), get__features_features_4_dw_dw_0_conv_bias(),
              get__features_features_4_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 2, 2, "dw4"),
          pw4(-4, get__features_features_4_pw_pw_0_conv_filter(), get__features_features_4_pw_pw_0_conv_bias(),
              get__features_features_4_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw4"),
          dw5(-4, get__features_features_5_dw_dw_0_conv_filter(), get__features_features_5_dw_dw_0_conv_bias(),
              get__features_features_5_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 1, 1, "dw5"),
          pw5(-4, get__features_features_5_pw_pw_0_conv_filter(), get__features_features_5_pw_pw_0_conv_bias(),
              get__features_features_5_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw5"),
          dw6(-4, get__features_features_6_dw_dw_0_conv_filter(), get__features_features_6_dw_dw_0_conv_bias(),
              get__features_features_6_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 2, 2, "dw6"),
          pw6(-4, get__features_features_6_pw_pw_0_conv_filter(), get__features_features_6_pw_pw_0_conv_bias(),
              get__features_features_6_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw6"),
          dw7(-4, get__features_features_7_dw_dw_0_conv_filter(), get__features_features_7_dw_dw_0_conv_bias(),
              get__features_features_7_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 1, 1, "dw7"),
          pw7(-4, get__features_features_7_pw_pw_0_conv_filter(), get__features_features_7_pw_pw_0_conv_bias(),
              get__features_features_7_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw7"),
          dw8(-4, get__features_features_8_dw_dw_0_conv_filter(), get__features_features_8_dw_dw_0_conv_bias(),
              get__features_features_8_dw_dw_0_conv_activation(), PADDING_SAME_END, {}, 1, 1, "dw8"),
          pw8(-4, get__features_features_8_pw_pw_0_conv_filter(), get__features_features_8_pw_pw_0_conv_bias(),
              get__features_features_8_pw_pw_0_conv_activation(), PADDING_VALID, {}, 1, 1, "pw8"),
          head(-5, get__features_features_9_conv_filter(), get__features_features_9_conv_bias(),
               get__features_features_9_conv_activation(), PADDING_VALID, {}, 1, 1, "head"),
          gap(-5, "gap"),
          fc(-4, get__fc_gemm_filter(), get__fc_gemm_bias(), NULL, true, "fc")
    {
    }

    void build(Tensor<int8_t> &input)
    {
        this->stem.build(input);
        this->dw0.build(this->stem.get_output());
        this->pw0.build(this->dw0.get_output());
        this->dw1.build(this->pw0.get_output());
        this->pw1.build(this->dw1.get_output());
        this->dw2.build(this->pw1.get_output());
        this->pw2.build(this->dw2.get_output());
        this->dw3.build(this->pw2.get_output());
        this->pw3.build(this->dw3.get_output());
        this->dw4.build(this->pw3.get_output());
        this->pw4.build(this->dw4.get_output());
        this->dw5.build(this->pw4.get_output());
        this->pw5.build(this->dw5.get_output());
        this->dw6.build(this->pw5.get_output());
        this->pw6.build(this->dw6.get_output());
        this->dw7.build(this->pw6.get_output());
        this->pw7.build(this->dw7.get_output());
        this->dw8.build(this->pw7.get_output());
        this->pw8.build(this->dw8.get_output());
        this->head.build(this->pw8.get_output());
        this->gap.build(this->head.get_output());
        this->fc.build(this->gap.get_output());
    }

    void call(Tensor<int8_t> &input)
    {
        this->stem.call(input);
        this->dw0.call(this->stem.get_output());
        this->stem.get_output().free_element();

        this->pw0.call(this->dw0.get_output());
        this->dw0.get_output().free_element();
        this->dw1.call(this->pw0.get_output());
        this->pw0.get_output().free_element();
        this->pw1.call(this->dw1.get_output());
        this->dw1.get_output().free_element();
        this->dw2.call(this->pw1.get_output());
        this->pw1.get_output().free_element();
        this->pw2.call(this->dw2.get_output());
        this->dw2.get_output().free_element();
        this->dw3.call(this->pw2.get_output());
        this->pw2.get_output().free_element();
        this->pw3.call(this->dw3.get_output());
        this->dw3.get_output().free_element();
        this->dw4.call(this->pw3.get_output());
        this->pw3.get_output().free_element();
        this->pw4.call(this->dw4.get_output());
        this->dw4.get_output().free_element();
        this->dw5.call(this->pw4.get_output());
        this->pw4.get_output().free_element();
        this->pw5.call(this->dw5.get_output());
        this->dw5.get_output().free_element();
        this->dw6.call(this->pw5.get_output());
        this->pw5.get_output().free_element();
        this->pw6.call(this->dw6.get_output());
        this->dw6.get_output().free_element();
        this->dw7.call(this->pw6.get_output());
        this->pw6.get_output().free_element();
        this->pw7.call(this->dw7.get_output());
        this->dw7.get_output().free_element();
        this->dw8.call(this->pw7.get_output());
        this->pw7.get_output().free_element();
        this->pw8.call(this->dw8.get_output());
        this->dw8.get_output().free_element();
        this->head.call(this->pw8.get_output());
        this->pw8.get_output().free_element();
        this->gap.call(this->head.get_output());
        this->head.get_output().free_element();
        this->fc.call(this->gap.get_output());
    }
};
