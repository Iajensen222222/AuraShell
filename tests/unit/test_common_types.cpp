#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_all.hpp>
#include <cmath>

// Mock types for testing (before implementation)
namespace aura {

struct RGBAColor {
    uint8_t r, g, b, a;

    // Default constructor
    RGBAColor() : r(0), g(0), b(0), a(255) {}

    // Explicit constructor
    RGBAColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255)
        : r(r), g(g), b(b), a(a) {}

    // Equality
    bool operator==(const RGBAColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

struct Vector2D {
    float x, y;

    // Default constructor
    Vector2D() : x(0.0f), y(0.0f) {}

    // Explicit constructor
    Vector2D(float x, float y) : x(x), y(y) {}

    // Vector operations
    Vector2D operator+(const Vector2D& other) const {
        return Vector2D(x + other.x, y + other.y);
    }

    Vector2D operator-(const Vector2D& other) const {
        return Vector2D(x - other.x, y - other.y);
    }

    Vector2D operator*(float scalar) const {
        return Vector2D(x * scalar, y * scalar);
    }

    // Magnitude
    float magnitude() const {
        return std::sqrt(x * x + y * y);
    }

    // Distance to another vector
    float distance(const Vector2D& other) const {
        return (*this - other).magnitude();
    }

    // Equality (with floating point tolerance)
    bool equals(const Vector2D& other, float epsilon = 0.0001f) const {
        return std::abs(x - other.x) < epsilon && std::abs(y - other.y) < epsilon;
    }
};

struct AnimationState {
    float progress;         // 0.0 to 1.0
    uint64_t startTimeMs;
    uint64_t durationMs;

    // Default constructor
    AnimationState() : progress(0.0f), startTimeMs(0), durationMs(200) {}

    // Explicit constructor
    AnimationState(uint64_t startTime, uint64_t duration)
        : progress(0.0f), startTimeMs(startTime), durationMs(duration) {}

    // Check if animation is complete
    bool isComplete() const {
        return progress >= 1.0f;
    }

    // Update progress based on elapsed time
    void updateProgress(uint64_t currentTimeMs) {
        if (currentTimeMs < startTimeMs) {
            progress = 0.0f;
            return;
        }

        uint64_t elapsed = currentTimeMs - startTimeMs;
        if (elapsed >= durationMs) {
            progress = 1.0f;
        } else {
            progress = static_cast<float>(elapsed) / static_cast<float>(durationMs);
        }
    }
};

} // namespace aura

// ============================================================================
// TESTS: RGBAColor
// ============================================================================

TEST_CASE("RGBAColor::Construction", "[common_types]") {
    using namespace aura;

    SECTION("Default construction initializes to transparent black") {
        RGBAColor color;

        REQUIRE(color.r == 0);
        REQUIRE(color.g == 0);
        REQUIRE(color.b == 0);
        REQUIRE(color.a == 255);
    }

    SECTION("Explicit construction with RGB") {
        RGBAColor color(255, 128, 64);

        REQUIRE(color.r == 255);
        REQUIRE(color.g == 128);
        REQUIRE(color.b == 64);
        REQUIRE(color.a == 255);  // Default alpha
    }

    SECTION("Explicit construction with RGBA") {
        RGBAColor color(255, 128, 64, 200);

        REQUIRE(color.r == 255);
        REQUIRE(color.g == 128);
        REQUIRE(color.b == 64);
        REQUIRE(color.a == 200);
    }
}

TEST_CASE("RGBAColor::Comparison", "[common_types]") {
    using namespace aura;

    SECTION("Equal colors compare true") {
        RGBAColor color1(255, 128, 64, 200);
        RGBAColor color2(255, 128, 64, 200);

        REQUIRE(color1 == color2);
    }

    SECTION("Different colors compare false") {
        RGBAColor color1(255, 128, 64, 200);
        RGBAColor color2(255, 128, 65, 200);

        REQUIRE(!(color1 == color2));
    }

    SECTION("Different alpha values make colors unequal") {
        RGBAColor color1(255, 128, 64, 200);
        RGBAColor color2(255, 128, 64, 199);

        REQUIRE(!(color1 == color2));
    }
}

// ============================================================================
// TESTS: Vector2D
// ============================================================================

TEST_CASE("Vector2D::Construction", "[common_types]") {
    using namespace aura;

    SECTION("Default construction creates zero vector") {
        Vector2D vec;

        REQUIRE(vec.x == 0.0f);
        REQUIRE(vec.y == 0.0f);
    }

    SECTION("Explicit construction sets values") {
        Vector2D vec(3.0f, 4.0f);

        REQUIRE(vec.x == 3.0f);
        REQUIRE(vec.y == 4.0f);
    }
}

TEST_CASE("Vector2D::Operations", "[common_types]") {
    using namespace aura;

    SECTION("Vector addition") {
        Vector2D a(1.0f, 2.0f);
        Vector2D b(3.0f, 4.0f);
        Vector2D result = a + b;

        REQUIRE(result.x == 4.0f);
        REQUIRE(result.y == 6.0f);
    }

    SECTION("Vector subtraction") {
        Vector2D a(5.0f, 7.0f);
        Vector2D b(2.0f, 3.0f);
        Vector2D result = a - b;

        REQUIRE(result.x == 3.0f);
        REQUIRE(result.y == 4.0f);
    }

    SECTION("Scalar multiplication") {
        Vector2D a(2.0f, 3.0f);
        Vector2D result = a * 2.5f;

        REQUIRE_THAT(result.x, Catch::Matchers::WithinAbs(5.0f, 0.0001f));
        REQUIRE_THAT(result.y, Catch::Matchers::WithinAbs(7.5f, 0.0001f));
    }
}

TEST_CASE("Vector2D::Magnitude", "[common_types]") {
    using namespace aura;

    SECTION("Magnitude of 3-4-5 triangle") {
        Vector2D vec(3.0f, 4.0f);
        float mag = vec.magnitude();

        // Magnitude should be 5
        REQUIRE_THAT(mag, Catch::Matchers::WithinAbs(5.0f, 0.0001f));
    }

    SECTION("Zero vector has zero magnitude") {
        Vector2D vec(0.0f, 0.0f);

        REQUIRE_THAT(vec.magnitude(), Catch::Matchers::WithinAbs(0.0f, 0.0001f));
    }
}

TEST_CASE("Vector2D::Distance", "[common_types]") {
    using namespace aura;

    SECTION("Distance between two points") {
        Vector2D a(0.0f, 0.0f);
        Vector2D b(3.0f, 4.0f);
        float dist = a.distance(b);

        // Distance should be 5
        REQUIRE_THAT(dist, Catch::Matchers::WithinAbs(5.0f, 0.0001f));
    }

    SECTION("Distance from point to itself is zero") {
        Vector2D a(5.0f, 7.0f);

        REQUIRE_THAT(a.distance(a), Catch::Matchers::WithinAbs(0.0f, 0.0001f));
    }
}

TEST_CASE("Vector2D::Equality", "[common_types]") {
    using namespace aura;

    SECTION("Exact equal vectors") {
        Vector2D a(1.0f, 2.0f);
        Vector2D b(1.0f, 2.0f);

        REQUIRE(a.equals(b));
    }

    SECTION("Vectors within epsilon are equal") {
        Vector2D a(1.0f, 2.0f);
        Vector2D b(1.00001f, 2.00001f);

        REQUIRE(a.equals(b, 0.0001f));
    }

    SECTION("Vectors outside epsilon are not equal") {
        Vector2D a(1.0f, 2.0f);
        Vector2D b(1.1f, 2.0f);

        REQUIRE(!a.equals(b, 0.0001f));
    }
}

// ============================================================================
// TESTS: AnimationState
// ============================================================================

TEST_CASE("AnimationState::Construction", "[common_types]") {
    using namespace aura;

    SECTION("Default construction creates zero-progress animation") {
        AnimationState anim;

        REQUIRE(anim.progress == 0.0f);
        REQUIRE(anim.durationMs == 200);
        REQUIRE(!anim.isComplete());
    }

    SECTION("Explicit construction sets timing") {
        AnimationState anim(1000, 500);  // Start at 1000ms, 500ms duration

        REQUIRE(anim.progress == 0.0f);
        REQUIRE(anim.startTimeMs == 1000);
        REQUIRE(anim.durationMs == 500);
    }
}

TEST_CASE("AnimationState::ProgressTracking", "[common_types]") {
    using namespace aura;

    SECTION("Progress starts at 0") {
        AnimationState anim(1000, 200);

        REQUIRE(anim.progress == 0.0f);
    }

    SECTION("Progress updates based on elapsed time") {
        AnimationState anim(1000, 200);  // Start 1000ms, 200ms duration

        // At start time
        anim.updateProgress(1000);
        REQUIRE_THAT(anim.progress, Catch::Matchers::WithinAbs(0.0f, 0.01f));

        // At 50% time
        anim.updateProgress(1100);
        REQUIRE_THAT(anim.progress, Catch::Matchers::WithinAbs(0.5f, 0.01f));

        // At 100% time
        anim.updateProgress(1200);
        REQUIRE_THAT(anim.progress, Catch::Matchers::WithinAbs(1.0f, 0.01f));
    }

    SECTION("Progress clamps at 1.0") {
        AnimationState anim(1000, 200);

        // Update well past end time
        anim.updateProgress(2000);

        REQUIRE(anim.progress == 1.0f);
    }

    SECTION("isComplete returns true when progress >= 1.0") {
        AnimationState anim(1000, 200);

        anim.updateProgress(999);   // Before start
        REQUIRE(!anim.isComplete());

        anim.updateProgress(1200);  // At/past end
        REQUIRE(anim.isComplete());
    }
}

TEST_CASE("AnimationState::TimeHandling", "[common_types]") {
    using namespace aura;

    SECTION("Time before start sets progress to 0") {
        AnimationState anim(1000, 200);

        anim.updateProgress(500);

        REQUIRE_THAT(anim.progress, Catch::Matchers::WithinAbs(0.0f, 0.01f));
    }

    SECTION("Time after duration completes animation") {
        AnimationState anim(1000, 200);

        anim.updateProgress(1500);

        REQUIRE(anim.progress == 1.0f);
        REQUIRE(anim.isComplete());
    }
}
