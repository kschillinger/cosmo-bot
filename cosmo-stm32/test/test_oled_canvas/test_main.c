#include <stdint.h>
#include <string.h>
#include <unity.h>

#include "oled_canvas.h"

static uint8_t s_framebuffer[OLED_CANVAS_BYTES];

void setUp(void)
{
    memset(s_framebuffer, 0, sizeof(s_framebuffer));
}

void tearDown(void)
{
}

static void test_set_and_get_pixel(void)
{
    oled_canvas_set_pixel(s_framebuffer, 10U, 20U, 1U);
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 10U, 20U));

    oled_canvas_set_pixel(s_framebuffer, 10U, 20U, 0U);
    TEST_ASSERT_EQUAL_UINT8(0U, oled_canvas_get_pixel(s_framebuffer, 10U, 20U));
}

static void test_render_idle_draws_face_features(void)
{
    oled_canvas_render_idle(s_framebuffer);

    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 55U, 25U));
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 69U, 25U));
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 64U, 44U));
}

static void test_render_processing_changes_with_frame(void)
{
    uint8_t frame0_pixel;
    uint8_t frame1_pixel;

    oled_canvas_render_processing(s_framebuffer, 0U);
    frame0_pixel = oled_canvas_get_pixel(s_framebuffer, 64U, 20U);

    oled_canvas_render_processing(s_framebuffer, 1U);
    frame1_pixel = oled_canvas_get_pixel(s_framebuffer, 64U, 20U);

    TEST_ASSERT_NOT_EQUAL(frame0_pixel, frame1_pixel);
}

static void test_render_listening_uses_samples(void)
{
    const uint16_t samples[4] = {0U, 4095U, 0U, 0U};

    oled_canvas_render_listening(s_framebuffer, samples, 4U);
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 43U, 34U));
}

static void test_render_error_draws_cross(void)
{
    oled_canvas_render_error(s_framebuffer);
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 28U, 16U));
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 99U, 16U));
    TEST_ASSERT_EQUAL_UINT8(1U, oled_canvas_get_pixel(s_framebuffer, 20U, 8U));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_set_and_get_pixel);
    RUN_TEST(test_render_idle_draws_face_features);
    RUN_TEST(test_render_processing_changes_with_frame);
    RUN_TEST(test_render_listening_uses_samples);
    RUN_TEST(test_render_error_draws_cross);

    return UNITY_END();
}
