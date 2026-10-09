#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>

#include "gfx/stereo.hpp"
#include "gfx_fixture.hpp"

using namespace dc::test;

namespace {
constexpr float          angle = std::numbers::pi_v<float> / 4;
constexpr gfx::StereoFov fov{-angle, angle, angle, -angle};

gfx::ViewOverride Eye(float x = 0) {
    gfx::StereoPose pose;
    pose.position[0] = x;
    gfx::ViewOverride view;
    EXPECT_TRUE(gfx::MakeStereoView(pose, fov, 1, 0.1f, 100, 0, view));
    return view;
}

std::array<float, 3> Project(const float *matrix, float x, float y, float z) {
    float w = matrix[3] * x + matrix[7] * y + matrix[11] * z + matrix[15];
    return {(matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12]) / w,
            (matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13]) / w,
            (matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14]) / w};
}

void Triangle(float x, float z, int channel, const gfx::MeshTransform &transform) {
    std::array<gfx::Vertex3D, 3> vertices{};
    const float                  shape[3][2] = {
        {-.14f, -.12f},
        {.14f,  -.12f},
        {0,     .12f }
    };
    for (int i = 0; i < 3; ++i) {
        vertices[i].position[0] = x + shape[i][0];
        vertices[i].position[1] = shape[i][1];
        vertices[i].position[2] = z;
    }
    gfx::MeshConstants constants{};
    // Our test model and middle/local matrices are identity. Include the camera's X shift.
    std::copy_n(transform.projection, 16, constants.mvp);
    constants.mvp[12] = transform.projection[0] * transform.view[12];
    constants.diffuse[channel] = 1;
    constants.diffuse[3] = 1;
    gfx::DrawState state;
    state.depth_test = gfx::DepthTest::GEqual;
    state.depth_write = true;
    const uint32_t indices[] = {0, 1, 2};
    gfx::DrawMeshImmediate(vertices, indices, constants, {}, state, &transform);
}

gfx::DisplayListRef Scene(float camera_x = 0, bool sprites = false, gfx::TextureHandle texture = gfx::kNullTexture,
                          bool cut = false, bool two_cameras = false) {
    gfx::BeginRecording();
    if (cut) {
        gfx::CutInterpolation();
    }
    const uint8_t black[] = {0, 0, 0, 128};
    gfx::Clear(true, black, true, 0);
    auto transform = gfx::IdentityMeshTransform();
    auto view = Eye();
    std::copy_n(view.projection, 16, transform.projection);
    transform.view[12] = camera_x;
    gfx::SetInterpKey(1);
    Triangle(-.6f, 2, 0, transform);
    gfx::SetInterpKey(2);
    if (two_cameras) {
        transform.view[12] += .1f;
    }
    Triangle(1.2f, 4, 2, transform);
    gfx::SetInterpKey(0);
    if (sprites) {
        const float    depth = Project(view.projection, 0, 0, 2)[2];
        gfx::DrawState state;
        state.depth_test = gfx::DepthTest::GEqual;
        auto sprite = Quad(308, 300, 24, 24, {0, 128, 0, 128}, 0, 0, 0, 0, depth);
        gfx::Draw2D(gfx::Primitive::Quads, sprite, {}, state);
    }
    // HUD must not be moved with the eye.
    auto hud = Quad(10, 10, 20, 20, {128, 128, 128, 128});
    gfx::Draw2D(gfx::Primitive::Quads, hud, {}, {});
    gfx::ReadDepth(0, 223, 235, 4, 4);
    if (texture != gfx::kNullTexture) {
        uint32_t red = Rgba(128, 0, 0);
        gfx::UpdateTexture(texture, 0, 0, 0, 1, 1, &red);
    }
    return gfx::EndRecording();
}

double Center(const std::vector<uint8_t> &pixels, uint32_t width, int channel) {
    double sum = 0;
    size_t count = 0;
    for (size_t p = 0; p < pixels.size(); p += 4) {
        if (pixels[p + channel] > 100 && pixels[p + (channel + 1) % 3] < 10 && pixels[p + (channel + 2) % 3] < 10) {
            sum += (p / 4) % width;
            ++count;
        }
    }
    EXPECT_GT(count, 10u);
    return count ? sum / count : -1;
}

void Read(GfxFixture &fixture) {
    ASSERT_TRUE(gfx::ReadbackFrame(fixture.pixels, fixture.width, fixture.height));
}
} // namespace

TEST(StereoMath, AsymmetricFrustumEdgesAndReverseDepth) {
    gfx::ViewOverride view;
    gfx::StereoFov    asymmetric{-.7f, .9f, .8f, -.6f};
    ASSERT_TRUE(gfx::MakeStereoView({}, asymmetric, 10, .5f, 1000, 3, view));
    EXPECT_EQ(view.camera, 3u);
    EXPECT_NEAR(Project(view.projection, std::tan(asymmetric.left), 0, 1)[0], -1, 1e-6);
    EXPECT_NEAR(Project(view.projection, std::tan(asymmetric.right), 0, 1)[0], 1, 1e-6);
    EXPECT_NEAR(Project(view.projection, 0, -std::tan(asymmetric.up), 1)[1], -1, 1e-6);
    EXPECT_NEAR(Project(view.projection, 0, -std::tan(asymmetric.down), 1)[1], 1, 1e-6);
    EXPECT_NEAR(Project(view.projection, 0, 0, .5f)[2], 1, 1e-6);
    EXPECT_NEAR(Project(view.projection, 0, 0, 1000)[2], 0, 1e-6);
}

TEST(StereoMath, PoseUsesMetresAndCorrectHeadRotation) {
    gfx::StereoPose pose;
    pose.position[0] = .032f;
    pose.position[1] = .1f;
    pose.position[2] = -.2f;
    gfx::ViewOverride view;
    ASSERT_TRUE(gfx::MakeStereoView(pose, fov, 10, .5f, 100, 0, view));
    auto origin = Project(view.view_from_camera, .32f, -1, 2);
    for (float component : origin) {
        EXPECT_NEAR(component, 0, 1e-6);
    }
    pose = {};
    // +90 degrees around OpenXR Y turns forward (-Z) towards -X (the user's left).
    pose.orientation[1] = std::sqrt(.5f);
    pose.orientation[3] = std::sqrt(.5f);
    ASSERT_TRUE(gfx::MakeStereoView(pose, fov, 1, .1f, 100, 0, view));
    auto ahead = Project(view.view_from_camera, -1, 0, 0);
    EXPECT_NEAR(ahead[0], 0, 1e-6);
    EXPECT_NEAR(ahead[2], 1, 1e-6);
}

TEST(StereoMath, RejectsInvalidInputsWithoutChangingOutput) {
    auto out = Eye();
    auto before = out;
    EXPECT_FALSE(gfx::MakeStereoView({}, {0, 0, 0, 0}, 10, .5f, 100, 0, out));
    EXPECT_FALSE(gfx::MakeStereoView({}, fov, 0, .5f, 100, 0, out));
    EXPECT_FALSE(gfx::MakeStereoView({}, fov, 10, 100, .5f, 0, out));
    gfx::StereoPose pose;
    pose.orientation[3] = 0;
    EXPECT_FALSE(gfx::MakeStereoView(pose, fov, 10, .5f, 100, 0, out));
    pose = {};
    pose.position[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(gfx::MakeStereoView(pose, fov, 10, .5f, 100, 0, out));
    EXPECT_EQ(std::memcmp(&out, &before, sizeof(out)), 0);
}

TEST(GfxStereo, DepthDependentDisparityPreservesCanonicalHudAndState) {
    GfxFixture fixture;
    auto       texture = gfx::CreateTexture({1, 1, gfx::TextureFormat::Rgba8, 1, true});
    auto       list = Scene(0, false, texture);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.canonical = true}));
    const auto depth = gfx::DepthResult(0);
    ASSERT_TRUE(gfx::PresentCanonical());
    Read(fixture);
    const auto canonical = fixture.pixels;
    uint32_t   green = Rgba(0, 128, 0);
    ASSERT_TRUE(gfx::UpdateTexture(texture, 0, 0, 0, 1, 1, &green));
    double centers[2][2];
    for (int i = 0; i < 2; ++i) {
        auto eye = Eye(i == 0 ? -.064f : .064f);
        ASSERT_TRUE(gfx::RenderList(*list, 1, {.present = true, .view_override = &eye}));
        Read(fixture);
        centers[i][0] = Center(fixture.pixels, fixture.width, 0);
        centers[i][1] = Center(fixture.pixels, fixture.width, 2);
        EXPECT_TRUE(fixture.PixelNear(20, 20, 128, 128, 128));
        EXPECT_EQ(gfx::DepthResult(0), depth);
    }
    const double near_disparity = centers[0][0] - centers[1][0];
    const double far_disparity = centers[0][1] - centers[1][1];
    EXPECT_GT(far_disparity, 8);
    EXPECT_NEAR(near_disparity, 2 * far_disparity, 1.5);
    std::vector<uint8_t> texels;
    uint32_t             w, h;
    ASSERT_TRUE(gfx::ReadbackTexture(texture, texels, w, h));
    EXPECT_EQ(texels[0], 0);
    EXPECT_EQ(texels[1], 128); // canonical upload did not execute once per eye
    ASSERT_TRUE(gfx::PresentCanonical());
    Read(fixture);
    EXPECT_EQ(fixture.pixels, canonical);
    gfx::DestroyTexture(texture);
}

TEST(GfxStereo, ProjectionOnlyOverrideReprojectsWorldSpritesAndKeepsHud) {
    GfxFixture fixture;
    auto       list = Scene(0, true);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.canonical = true}));
    ASSERT_TRUE(gfx::PresentCanonical());
    Read(fixture);
    double before = Center(fixture.pixels, fixture.width, 1);
    auto   eye = Eye();
    eye.projection[8] = .2f;
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.present = true, .view_override = &eye}));
    Read(fixture);
    EXPECT_NEAR(Center(fixture.pixels, fixture.width, 1) - before, 64, 1);
    EXPECT_TRUE(fixture.PixelNear(20, 20, 128, 128, 128));
}

TEST(GfxStereo, EyeTransformComposesWithInterpolatedCameraAndSurvivesCuts) {
    GfxFixture fixture;
    auto       previous = Scene(-.2f);
    ASSERT_TRUE(gfx::RenderList(*previous, 1, {.canonical = true}));
    auto list = Scene(.2f);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.canonical = true}));
    auto eye = Eye(.1f);
    ASSERT_TRUE(gfx::RenderList(*list, .5f, {.previous = previous.get(), .present = true, .view_override = &eye}));
    Read(fixture);
    double interpolated = Center(fixture.pixels, fixture.width, 0);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.present = true, .view_override = &eye}));
    Read(fixture);
    EXPECT_NEAR(Center(fixture.pixels, fixture.width, 0) - interpolated, 32, 1);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.present = true}));
    Read(fixture);
    EXPECT_NEAR(Center(fixture.pixels, fixture.width, 0) - interpolated, 48, 1);
    auto cut = Scene(.2f, false, gfx::kNullTexture, true);
    ASSERT_TRUE(gfx::RenderList(*cut, 1, {.canonical = true}));
    ASSERT_TRUE(gfx::RenderList(*cut, .5f, {.previous = previous.get(), .present = true, .view_override = &eye}));
    Read(fixture);
    EXPECT_NEAR(Center(fixture.pixels, fixture.width, 0) - interpolated, 32, 1);
}

TEST(GfxStereo, OverrideSelectsOneCameraOnly) {
    GfxFixture fixture;
    auto       list = Scene(0, false, gfx::kNullTexture, false, true);
    ASSERT_EQ(gfx::ListStats(*list).cameras, 2u);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.canonical = true}));
    ASSERT_TRUE(gfx::PresentCanonical());
    Read(fixture);
    double red = Center(fixture.pixels, fixture.width, 0);
    double blue = Center(fixture.pixels, fixture.width, 2);
    auto   eye = Eye(.1f);
    eye.camera = 1;
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.present = true, .view_override = &eye}));
    Read(fixture);
    EXPECT_EQ(Center(fixture.pixels, fixture.width, 0), red);
    EXPECT_NEAR(Center(fixture.pixels, fixture.width, 2) - blue, -8, 1);
}

TEST(GfxStereo, SpriteBehindEyeDoesNotFallBackToRecordedScreenPosition) {
    GfxFixture fixture;
    auto       list = Scene(0, true);
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.canonical = true}));
    auto eye = Eye();
    eye.view_from_camera[14] = -3;
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.present = true, .view_override = &eye}));
    Read(fixture);
    size_t green = 0;
    for (size_t p = 0; p < fixture.pixels.size(); p += 4) {
        green += fixture.pixels[p] < 10 && fixture.pixels[p + 1] > 100 && fixture.pixels[p + 2] < 10;
    }
    EXPECT_EQ(green, 0u);
    EXPECT_TRUE(fixture.PixelNear(20, 20, 128, 128, 128));
}

TEST(GfxStereo, InvalidOrCanonicalEyeOverridesAreRefused) {
    GfxFixture fixture;
    auto       list = Scene();
    auto       eye = Eye();
    EXPECT_FALSE(gfx::RenderList(*list, 1, {.canonical = true, .view_override = &eye}));
    ASSERT_TRUE(gfx::RenderList(*list, 1, {.canonical = true}));
    eye.camera = 999;
    EXPECT_FALSE(gfx::RenderList(*list, 1, {.view_override = &eye}));
    eye = Eye();
    eye.projection[0] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(gfx::RenderList(*list, 1, {.view_override = &eye}));
    eye = Eye();
    eye.view_from_camera[0] = 0;
    EXPECT_FALSE(gfx::RenderList(*list, 1, {.view_override = &eye}));
    EXPECT_TRUE(gfx::RenderList(*list, 1, {.present = true}));
}
